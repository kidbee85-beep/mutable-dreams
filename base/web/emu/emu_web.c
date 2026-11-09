/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* An fm1-base app in the browser: the host simulator (host/plat_host.c: the whole app, UI and sound, against a
 * simulated FM-1) compiled to WebAssembly and run in an AudioWorklet (from X0X's, by way of FoMni-1's). The worklet asks
 * for audio; producing it advances the device's clock one millisecond at a time, which runs the
 * app's render every APP_BLOCK samples (the I2S half buffer) and the UI every 16 ms, as on the FM-1. The page
 * sends input, and reads the screen, the lights and the saved objects back through these exports.
 *
 *   tools/build.py APP_DIR --web   ->  APP_DIR/build/emu/app.wasm (+ the page) */
#include <stdint.h>

static void web_audio(const int32_t *blk, uint32_t n);
#include "plat_host.c"

#undef __attribute__

/* output ring, stereo float, filled by run_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r;
static void web_audio(const int32_t *blk, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_w++ % RING) * 2u;
        ring[i] = (float)blk[2 * k] / 8388608.0f;
        ring[i + 1] = (float)blk[2 * k + 1] / 8388608.0f;
    }
}

static float out_l[1024], out_r[1024];
__attribute__((used, visibility("default"))) float *web_out_l(void) { return out_l; }
__attribute__((used, visibility("default"))) float *web_out_r(void) { return out_r; }

/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
__attribute__((used, visibility("default"))) void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        run_ms(1);
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

/* a screen reader (fm1-reader): the page writes a SysEx frame (the bytes between F0 and F7) to web_sysex_buf and
 * calls web_sysex(n); what the firmware says back, whole F0..F7 frames, from web_said_buf (web_said_len bytes;
 * web_said_clear when read) */
static uint8_t said_buf[4096], sx_in[64];
static uint32_t said_n;
static void web_said(const uint8_t *p, uint32_t n)
{
    if (said_n + n <= sizeof said_buf) {
        memcpy(said_buf + said_n, p, n);
        said_n += n;
    }
}
__attribute__((used, visibility("default"))) uint8_t *web_said_buf(void) { return said_buf; }
__attribute__((used, visibility("default"))) uint32_t web_said_len(void) { return said_n; }
__attribute__((used, visibility("default"))) void web_said_clear(void) { said_n = 0; }
__attribute__((used, visibility("default"))) uint8_t *web_sysex_buf(void) { return sx_in; }
__attribute__((used, visibility("default"))) void web_sysex(uint32_t n) { ann_frame(sx_in, n > sizeof sx_in ? sizeof sx_in : n, now_ms); }

/* the page's screen reader on (1) or off (0): on, the reader's HELLO now and every 2 s of the device's time (as the
 * simulator's `listen`), so it never lapses while the page is in the background; off, its BYE (an app's words then
 * come through web_said_buf) */
__attribute__((used, visibility("default"))) void web_reader(int on)
{
    static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER}, BYE[] = {ANN_H0, ANN_H1, ANN_H2, ANN_BYE};
    sim_listen = on != 0;
    if (on)
        ann_frame(HELLO, sizeof HELLO, now_ms);
    else
        ann_frame(BYE, sizeof BYE, now_ms);
}

#if FM1B_EMU_REBOOT
/* the update mode (the part "reboot"): as device/main_fm1.c's enter_uboot shows it, the sound stopped and the screen
 * black with the reason, until the page starts the app again. 1: the computer asked (UBOOT (USB)), 2: REC held at
 * power-on (UPDATE MODE (REC)) */
#include "sys_text.c"
static uint8_t update_why;
static void update_enter(uint32_t why)
{
    update_why = (uint8_t)why;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sys_box(0, 110, 240, SYS_S, why == 2u ? "UPDATE MODE (REC)" : "UBOOT (USB)", C_WHITE, 1);
    web_down = 1;
}
/* the page asks for the update mode (1), as a computer's request does; web_update is 0 in a running app */
__attribute__((used, visibility("default"))) void web_update_enter(uint32_t why) { update_enter(why == 2u ? 2u : 1u); }
__attribute__((used, visibility("default"))) uint32_t web_update(void) { return update_why; }
/* what a restart carries into the next start of the app: how many plat_restart calls there were (the part starts the
 * wasm again when it counts one), the held buttons and MASTER, and whether a screen reader was listening */
__attribute__((used, visibility("default"))) uint32_t web_restarts(void) { return restarts; }
__attribute__((used, visibility("default"))) uint32_t web_buttons_get(void) { return held_btn; }
__attribute__((used, visibility("default"))) uint32_t web_master_get(void) { return master; }
__attribute__((used, visibility("default"))) uint32_t web_listening(void) { return sim_listen != 0; }
#endif
__attribute__((used, visibility("default"))) void web_boot(void)
{
#if FM1B_EMU_REBOOT
    if (held_btn & (1u << B_REC)) {                 /* REC held at power-on: the chip's update mode (main_fm1.c) */
        update_enter(2u);
        return;
    }
#endif
    boot();
}
__attribute__((used, visibility("default"))) void web_buttons(uint32_t m) { held_btn = m; }
__attribute__((used, visibility("default"))) void web_keys(uint32_t m)
{
#ifdef APP_KEY_SENSE
    uint32_t k, ch = m ^ held_keys;       /* each key landing at velocity 100 (the app makes its readings) */
    for (k = 0; k < NKEYS; k++)
        if (ch >> k & 1u)
            app_host_key(k, m >> k & 1u ? 100u : 0u, 0);
#endif
    held_keys = m;
}
/* one key at velocity vel 1..127 (0 = up), pressed press 0..1000 more (APP_KEY_SENSE; else just down or up) */
__attribute__((used, visibility("default"))) void web_key(uint32_t k, uint32_t vel, uint32_t press)
{
    if (k >= NKEYS)
        return;
#ifdef APP_KEY_SENSE
    app_host_key(k, vel > 127u ? 127u : vel, (press > 1000u ? 1000u : press) / 10u);
#endif
    if (vel)
        held_keys |= 1u << k;
    else
        held_keys &= ~(1u << k);
}
__attribute__((used, visibility("default"))) void web_enc(int role, int32_t n)
{
    if (role >= 0 && role < NE)
        enc_acc[role] += n;
}
__attribute__((used, visibility("default"))) void web_master(uint32_t v) { master = v > 4096u ? 4096u : v; }
__attribute__((used, visibility("default"))) uint32_t web_lit_buttons(void) { return lit_btn; }
__attribute__((used, visibility("default"))) uint32_t web_lit_keys(void) { return lit_keys; }
__attribute__((used, visibility("default"))) uint32_t web_dim_buttons(void) { return dim_btn; }
__attribute__((used, visibility("default"))) uint32_t web_dim_keys(void) { return dim_keys | mid_keys; }
#ifdef APP_DIM_LEVELS
/* the dim lights' pulse a scan frame in ns (plat_dim_ns, else the glow's), for a page that draws their brightness */
__attribute__((used, visibility("default"))) uint32_t web_dim_ns(void) { return dim_pulse(); }
#endif

/* MIDI: in from the page (port PORT_*: a Web MIDI input is USB), out as the app sent it (USB-MIDI packets) */
__attribute__((used, visibility("default"))) void web_midi_in(uint32_t port, uint32_t pkt)
{
    if (port < NPORTS && mi_w - mi_r < MQ) {
        min_q[mi_w % MQ] = pkt;
        min_port[mi_w++ % MQ] = port;
    }
}
__attribute__((used, visibility("default"))) uint32_t web_out_n(void) { return n_out; }
__attribute__((used, visibility("default"))) uint32_t *web_out_pkts(void) { return out_pkt; }
__attribute__((used, visibility("default"))) void web_out_clear(void) { n_out = 0; }
__attribute__((used, visibility("default"))) uint16_t *web_fb(void) { return fb; }
__attribute__((used, visibility("default"))) uint32_t web_blits(void) { return blits; }
__attribute__((used, visibility("default"))) uint32_t web_now(void) { return now_ms; }
__attribute__((used, visibility("default"))) uint32_t web_cpu(void) { return cpu_pct; }

/* saved objects: the page keeps them (localStorage) and gives them back before boot */
__attribute__((used, visibility("default"))) uint8_t *web_store(uint32_t obj) { store_ready(); return obj < OBJ_NOBJ ? store[obj] : 0; }
__attribute__((used, visibility("default"))) int web_store_len(uint32_t obj) { store_ready(); return obj < OBJ_NOBJ ? store_len[obj] : -1; }
__attribute__((used, visibility("default"))) void web_store_set_len(uint32_t obj, int n)
{
    store_ready();
    if (obj < OBJ_NOBJ)
        store_len[obj] = n < 0 ? -1 : n > (int)PLAT_STORE_MAX ? (int)PLAT_STORE_MAX : n;
}
__attribute__((used, visibility("default"))) uint32_t web_store_writes(void) { return store_writes; }
__attribute__((used, visibility("default"))) uint32_t web_nobj(void) { return OBJ_NOBJ; }
__attribute__((used, visibility("default"))) uint32_t web_store_max(void) { return PLAT_STORE_MAX; }

#if defined(APP_SYSEX) && FM1B_EMU_MIDI
/* SysEx with the app (APP_SYSEX), as over USB, with the emulator's part "midi" (which carries it to and from the page;
 * without it, none of this: the 8 KB buffer and the exports): the page writes a frame from the computer (F0 .. F7,
 * whole) to web_sx_in_buf (web_sx_in_max bytes at most) and gives it with web_sx_in(n). As felucca-bsp's usb.c hands a
 * frame to the main loop: 7-bit, 640 bytes at most between F0 and F7, to app_sysex; else dropped (with APP_SYSEX_BYTE
 * every byte to app_sysex_byte first, any length, up to a status byte inside). What the app sent (plat_sysex_send, a
 * stream), F0 .. F7 frames one after another (a stream cut short ends at the next F0), from web_sx_out_buf
 * (web_sx_out_len bytes; web_sx_out_clear when read) */
#define WEB_SX_IN 8192u
#define WEB_SX_MAX 640u
static uint8_t wsx_in[WEB_SX_IN];
__attribute__((used, visibility("default"))) uint8_t *web_sx_in_buf(void) { return wsx_in; }
__attribute__((used, visibility("default"))) uint32_t web_sx_in_max(void) { return WEB_SX_IN; }
__attribute__((used, visibility("default"))) int web_sx_in(uint32_t n)
{
    uint32_t i, ok = 1;
    if (n < 2u || n > WEB_SX_IN || wsx_in[0] != 0xF0u || wsx_in[n - 1u] != 0xF7u)
        return -1;                                /* (not a frame) */
    for (i = 1; i + 1u < n; i++)
        ok &= wsx_in[i] < 0x80u;
#ifdef APP_SYSEX_BYTE
    for (i = 0; i < n && (i == 0 || i + 1u == n || wsx_in[i] < 0x80u); i++)
        app_sysex_byte(wsx_in[i]);
#endif
    if (!ok || n - 2u > WEB_SX_MAX)
        return 0;                                 /* (a status byte inside, or too long: the device drops it) */
    (void)app_sysex(wsx_in + 1, n - 2u);
    return 1;
}
__attribute__((used, visibility("default"))) uint8_t *web_sx_out_buf(void) { return hsx_out; }
__attribute__((used, visibility("default"))) uint32_t web_sx_out_len(void) { return hsx_n; }
__attribute__((used, visibility("default"))) void web_sx_out_clear(void) { hsx_n = hsx_lost = 0; }
#endif

#ifdef FM1B_EMU_DATA
/* the data area (plat_data, its windows: plat.h) for the page to keep (the emulator's data part, fm1.toml emu): its
 * web_data_len bytes from web_data(), the device's address web_data_base() first; written before boot, it is what
 * the app finds */
__attribute__((used, visibility("default"))) uint8_t *web_data(void) { data_init(); return data_area; }
__attribute__((used, visibility("default"))) uint32_t web_data_len(void) { return sizeof data_area; }
__attribute__((used, visibility("default"))) uint32_t web_data_base(void) { return DATA_BASE; }
#endif

#if FM1B_EMU_BACKUP && FM1B_BACKUP
/* a backup in the file format of web/backup/index.html and tools/fm1_backup.py (device/backup.c's image: the magic,
 * version, count, flags, then each saved object, every part and the whole checked by CRC-32): web_backup_make makes it
 * (the unsaved saved first unless the app is playing: then flag bit 0) into web_backup_buf, its length returned;
 * web_backup_restore(n) takes the n bytes the page put in web_backup_buf as the FM-1 does after its question is
 * answered yes: checked whole before anything changes, then 0, or 10 playing (stop first), 3 not this app's or
 * version's, 4..6 damaged */
__attribute__((used, visibility("default"))) uint8_t *web_backup_buf(void) { return sim_img; }
__attribute__((used, visibility("default"))) uint32_t web_backup_max(void) { return sizeof sim_img; }
__attribute__((used, visibility("default"))) uint32_t web_backup_make(void) { return (uint32_t)sim_backup_image(); }
__attribute__((used, visibility("default"))) int web_backup_restore(uint32_t n)
{
    restore_rc = n > sizeof sim_img ? 3 : sim_restore_image(n);
    if (!restore_rc)
        store_writes++;                             /* (the page keeps the objects it is told changed) */
    return restore_rc;
}
#endif

#ifdef APP_WEB
#include "web.c"                        /* the app's own exports for its page (app.h APP_WEB) */
#endif
