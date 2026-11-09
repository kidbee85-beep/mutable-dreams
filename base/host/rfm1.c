/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* An fm1-base app as an .rfm1: the host simulator (host/plat_host.c) compiled to WebAssembly with the exports a host
 * app (RemoteFM1 for iOS first) drives it by. The format is RemoteFM1IOS's docs/RFM1-FORMAT.md, version 1: a module
 * with the fm1core descriptor (FM1VST's ABI 6), its panel and the rfm1_* functions that call the descriptor's. This
 * file is written from the format's description, for fm1-base; no FM1VST code is in it.
 *
 *   tools/build.py APP_DIR --rfm1 [--release X.Y]   ->  APP_DIR/build/rfm1/<id>-<version>.rfm1
 *
 * The host owns the clock: tick(ms) every device millisecond, then frame() (every 15 ms or so) or idle(), then
 * audio() for the 128-frame blocks that have fallen due at 44.1 kHz. The app renders APP_BLOCK frames at a time into
 * a ring here, which audio() drains. The panel (keys, buttons, encoders, MASTER, velocity and pressure in; the LEDs
 * and the screen out) is the module's, in its memory, where the host reads and writes it between calls.
 *
 * The flash image (flash_size bytes, the host keeps it): the app's saved objects at 0, packed (RF_MAGIC, a count,
 * then each object's number, length and bytes, a CRC-32 over it all), and the data area and its windows at their
 * device addresses (PLAT_XDATA0_LO .. PLAT_XDATA1_HI), where plat_data reads them in place. A fresh image (no staged
 * bytes, or not ours) is an empty store and an erased data area.
 *
 * SysEx: a screen reader (fm1-reader's protocol, device/announce.c) talks to the module over its MIDI: the frames
 * come in through midi_in and its words go out through midi_out_take, as on the FM-1's USB. */
#define PLAT_WEB 1                      /* plat_host.c as the browser's: no script, no main; the reader's words here */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "rfm1_info.h"                  /* (tools/build.py --rfm1) RFM1_ID, RFM1_NAME, RFM1_VERSION, .. */
static char *rf_getenv(const char *name) { (void)name; return 0; }
#define getenv rf_getenv                /* no environment in a host app (the format imports none): a host build's
                                         * settings from it (LOOM-8's LOOM_*) stay at their defaults */

#define RF_LCD_W 240
#define RF_LCD_H 240
#define RF_NKEY 27                      /* F3 (MIDI 53) .. G5, as plat's keys */
#define RF_NBTN 14                      /* matrix buttons 0..13 */
#define RF_NENC 7                       /* matrix encoders 0..6 */
#define RF_NCOL 11                      /* LED / matrix columns */
#define RF_NLINE 4                      /* LED lines a column: rows PA5..PA8 */
#define RF_NB 14                        /* labels, plat's B_* order (FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC
                                         * OCT- OCT+) */
#define RF_NE 8                         /* knob roles, plat's EN_* order, then MASTER */
#define RF_BLOCK 128                    /* the host's audio block */
#define RF_ABI 6u
#define RF_FLASH_SIZE 0x100000u         /* the FM-1's 1 MiB */
#define RF_STAGED "fm1core:staged"

/* the panel, the ABI's layout (rfm1_layout reports its offsets; the host checks them) */
typedef struct {
    volatile uint32_t keys, buttons;            /* in: bit k note key k held; bit i matrix button i held */
    volatile uint32_t keys_tap, buttons_tap;    /* in: down and up since the last tick: taken here */
    volatile int32_t enc[RF_NENC];              /* in: detents, matrix encoder order, + = clockwise: taken here */
    volatile int32_t master;                    /* in: MASTER 0..1023 */
    volatile uint8_t key_vel[RF_NKEY];          /* in: a held key's velocity 1..127, 0 none given */
    volatile uint8_t key_lean[RF_NKEY];         /* in: its pressure 0..100 % */
    uint8_t led[RF_NCOL][RF_NLINE];             /* out: each LED's brightness 0..255 */
    uint8_t led_play_green;                     /* out: PLAY's green LED */
    uint16_t lcd[RF_LCD_W * RF_LCD_H];          /* out: the screen, RGB565 high byte first (fb's own order) */
    volatile uint32_t lcd_writes;               /* out: moves with every change to lcd */
    uint8_t btn_id[RF_NB];                      /* label -> matrix button */
    uint8_t enc_id[RF_NE - 1];                  /* role -> matrix encoder */
    int8_t enc_dir[RF_NE - 1];                  /* +1: clockwise adds */
    uint8_t ready;                              /* 2 from the host: simulated time */
} rf_panel_t;

/* the descriptor, the ABI's layout: the host reads its data, never its functions (the exports call them) */
typedef struct {
    uint32_t abi_version;
    const char *id, *name, *version, *source_url;
    const char *family, *license, *gated;
    uint32_t flash_size;
    const char *button_names[RF_NB];
    const void *params;                         /* none: the host ignores them in version 1 */
    uint32_t nparams;
    rf_panel_t *panel;
    const int8_t *keymap;                       /* [6][RF_NCOL] key id at (row, column) */
    void (*options)(const char *flash_path, int no_flash, int save_on_exit, int headless);
    int (*boot_options)(int fail, const char *reason, int stage);
    void (*init)(int demo);
    void (*tick)(uint32_t ms);
    void (*frame)(void);
    void (*idle)(void);
    void (*audio)(int16_t *stereo, uint32_t frames);
    int (*midi_in)(uint32_t pkt);
    int (*midi_out_take)(uint32_t *pkt);
    void (*dump)(void);
    void (*stats)(uint32_t *shed, uint32_t *cpu_pct);
    void (*ui_info)(char *buf, uint32_t n);
    void (*shutdown)(void);
    int (*halted)(void);
    int (*flash_stage)(const uint8_t *bytes, uint32_t n);
    uint8_t *(*flash)(void);
    uint32_t (*flash_dirty)(void);
    int (*flash_sync)(void);
    uint32_t (*param_epoch)(void);
    int32_t (*knob_target)(int role);
    const char *credits;
} rf_core_t;

static rf_panel_t rf_panel;
static uint8_t rf_image[RF_FLASH_SIZE];

#define PLAT_FB (rf_panel.lcd)                                  /* the app draws straight into the panel */
#define PLAT_DATA_AREA (&rf_image[PLAT_XDATA0_LO])              /* plat_data reads the image in place */
static void web_audio(const int32_t *blk, uint32_t n) { (void)blk, (void)n; }   /* (run_ms's; not used here) */
#include "plat_host.c"

#undef __attribute__
#define RF_EXPORT(name) __attribute__((used, visibility("default"), export_name(name)))

/* the FM-1's matrix: key id at (row, column), 0..13 the buttons, 14..40 the note keys; LED line l is row l + 1
 * (felucca-bsp boards/fm1/fm1_input.h FM1_KEYMAP) */
static const int8_t RF_KEYMAP[6][RF_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
/* the stock panel's labels on the matrix (device/panel.c PANEL_DEFAULT): label -> button id, role -> encoder */
static const uint8_t RF_BTN_ID[RF_NB] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1};
static const uint8_t RF_ENC_ID[RF_NE - 1] = {0, 1, 6, 2, 3, 4, 5};

enum { RF_LED_FAINT = 12, RF_LED_DIM = 40, RF_LED_MID = 96, RF_LED_LIT = 255 };

/* ------------------------------------------------------------ the flash image --- */
#define RF_MAGIC 0x31424D46u            /* "FMB1": fm1-base's saved objects */
static uint8_t rf_staged, rf_booted, rf_halted;
static uint32_t rf_store_seen = ~0u;    /* store_writes when the image last took the store */

static uint32_t rf_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = ~0u;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}
static void rf_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static uint32_t rf_get32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the store into the image (only when it changed: store_writes moved) */
static void rf_store_out(void)
{
    uint32_t pos = 12, i, n = 0;
    store_ready();
    if (rf_store_seen == store_writes)
        return;
    rf_store_seen = store_writes;
    for (i = 0; i < OBJ_NOBJ; i++) {
        if (store_len[i] <= 0)
            continue;
        rf_put32(rf_image + pos, i);
        rf_put32(rf_image + pos + 4, (uint32_t)store_len[i]);
        memcpy(rf_image + pos + 8, store[i], (size_t)store_len[i]);
        pos += 8u + (uint32_t)store_len[i];
        n++;
    }
    rf_put32(rf_image, RF_MAGIC);
    rf_put32(rf_image + 4, n);
    rf_put32(rf_image + 8, pos - 12u);
    rf_put32(rf_image + pos, rf_crc32(rf_image, pos));
    memset(rf_image + pos + 4, 0xFF, PLAT_XDATA0_LO - pos - 4u);
}
/* the image's objects into the store; a damaged or foreign image: an empty store */
static void rf_store_in(void)
{
    uint32_t n = rf_get32(rf_image + 4), len = rf_get32(rf_image + 8), pos = 12, i;
    store_ready();
    for (i = 0; i < OBJ_NOBJ; i++)
        store_len[i] = -1;
    if (rf_get32(rf_image) != RF_MAGIC || len > PLAT_XDATA0_LO - 16u || rf_get32(rf_image + 12 + len) !=
        rf_crc32(rf_image, 12 + len))
        return;
    for (i = 0; i < n && pos + 8u <= 12u + len; i++) {
        uint32_t obj = rf_get32(rf_image + pos), l = rf_get32(rf_image + pos + 4);
        if (obj >= OBJ_NOBJ || l > PLAT_STORE_MAX || pos + 8u + l > 12u + len)
            break;
        memcpy(store[obj], rf_image + pos + 8, l);
        store_len[obj] = (int)l;
        pos += 8u + l;
    }
}

static int rf_flash_stage(const uint8_t *bytes, uint32_t n)
{
    memset(rf_image, 0xFF, sizeof rf_image);
    if (bytes && n)
        memcpy(rf_image, bytes, n > sizeof rf_image ? sizeof rf_image : n);
    rf_staged = 1;
    return 1;
}
static uint8_t *rf_flash(void) { rf_store_out(); return rf_image; }
static uint32_t rf_flash_dirty(void) { return store_writes + data_writes; }
static int rf_flash_sync(void) { rf_store_out(); return 1; }

/* -------------------------------------------------------------- the panel --- */
static uint8_t rf_led_pos[RF_NB + RF_NKEY];     /* (column << 2) | line, 0xFF none: plat's buttons then keys */
static void rf_led_pos_init(void)
{
    for (uint32_t i = 0; i < RF_NB + RF_NKEY; i++) {
        int id = i < RF_NB ? RF_BTN_ID[i] : 14 + (int)(i - RF_NB);
        rf_led_pos[i] = 0xFF;
        for (uint32_t r = 1; r <= RF_NLINE; r++)
            for (uint32_t c = 0; c < RF_NCOL; c++)
                if (RF_KEYMAP[r][c] == id)
                    rf_led_pos[i] = (uint8_t)(c << 2 | (r - 1u));
    }
}
/* plat_lights' picture into the panel's levels: lit, the mid level, the glow (DIM HI or DIM LO) */
static void rf_publish(void)
{
    uint8_t glow = lights_glow == 1 ? RF_LED_FAINT : RF_LED_DIM;
    uint32_t b = lights_b & ((1u << NB) - 1u), db = lights_glow > 0 ? lights_db : 0u;
    memset(rf_panel.led, 0, sizeof rf_panel.led);
    for (uint32_t i = 0; i < RF_NB + RF_NKEY; i++) {
        uint8_t q = rf_led_pos[i], v;
        int on = i < RF_NB ? (int)(b >> i & 1u) : (int)(lit_keys >> (i - RF_NB) & 1u);
        int mid = i >= RF_NB && (mid_keys >> (i - RF_NB) & 1u);
        int dim = i < RF_NB ? (int)(db >> i & 1u) : (int)(dim_keys >> (i - RF_NB) & 1u);
        if (q == 0xFF)
            continue;
        v = on ? RF_LED_LIT : mid ? RF_LED_MID : dim ? glow : 0;
        rf_panel.led[q >> 2][q & 3u] = v;
    }
    rf_panel.led_play_green = lights_b & LED_PLAY_GREEN ? RF_LED_LIT : db & LED_PLAY_GREEN ? glow : 0;
    rf_panel.lcd_writes = lcd_changes;
    rf_store_out();                     /* (the image current whenever the host looks) */
}
/* the panel's input into the simulator's, as the scan would hand it over */
static uint32_t rf_keys_was;
static void rf_input(void)
{
    uint32_t keys = rf_panel.keys | rf_panel.keys_tap, btns = rf_panel.buttons | rf_panel.buttons_tap, b = 0;
    int32_t m = rf_panel.master;
    rf_panel.keys_tap = rf_panel.buttons_tap = 0;           /* a tap: held for this millisecond */
    for (uint32_t i = 0; i < RF_NB; i++)
        if (btns >> RF_BTN_ID[i] & 1u)
            b |= 1u << i;
    held_btn = b;
    for (uint32_t e = 0; e < RF_NE - 1; e++) {
        int32_t n = rf_panel.enc[RF_ENC_ID[e]];
        if (n) {
            rf_panel.enc[RF_ENC_ID[e]] = 0;
            enc_acc[e] += n;
        }
    }
    master = (uint32_t)(m < 0 ? 0 : m > 1023 ? 1023 : m) * 4096u / 1023u;
    keys &= (1u << RF_NKEY) - 1u;
#ifdef APP_KEY_SENSE
    {   /* each key lands at its velocity (100 when none is given); its pressure as it changes */
        static uint8_t lean_was[RF_NKEY];
        for (uint32_t k = 0; k < RF_NKEY; k++) {
            uint32_t now = keys >> k & 1u, was = rf_keys_was >> k & 1u, vel = rf_panel.key_vel[k], lean = rf_panel.key_lean[k];
            if (lean > 100u)
                lean = 100u;
            if (now && (!was || lean != lean_was[k]))
                app_host_key(k, vel ? (vel > 127u ? 127u : vel) : 100u, lean);
            else if (!now && was)
                app_host_key(k, 0, 0);
            lean_was[k] = (uint8_t)(now ? lean : 0u);
        }
    }
#endif
    rf_keys_was = keys;                 /* (APP_KEY_SENSE: what changed) */
    (void)rf_keys_was;
    held_keys = keys;
}

/* ------------------------------------------------------------------ MIDI --- */
#define RF_OQ 1024u                     /* the app's MIDI out and the reader's frames, as USB-MIDI packets */
static uint32_t rf_oq[RF_OQ], rf_ow, rf_or;
static uint32_t rf_out_seen;            /* out_pkt[] (plat_host.c) taken up to here */
static void rf_out(uint32_t pkt)
{
    if (rf_ow - rf_or < RF_OQ)
        rf_oq[rf_ow++ % RF_OQ] = pkt;
}
static void rf_out_app(void)            /* what the app sent since the last look (plat_midi_send's log) */
{
    for (; rf_out_seen < n_out; rf_out_seen++)
        if (out_to[rf_out_seen] & TO_USB)
            rf_out(out_pkt[rf_out_seen]);
    n_out = rf_out_seen = 0;
}
/* a reader's words (whole F0 .. F7 frames) out as SysEx packets on cable 0 */
static void web_said(const uint8_t *p, uint32_t n)
{
    uint32_t i = 0;
    rf_out_app();                       /* (in the order they were made) */
    while (n - i > 3u) {
        rf_out(0x04u | p[i] << 8 | (uint32_t)p[i + 1] << 16 | (uint32_t)p[i + 2] << 24);
        i += 3;
    }
    if (n - i == 3u)
        rf_out(0x07u | p[i] << 8 | (uint32_t)p[i + 1] << 16 | (uint32_t)p[i + 2] << 24);
    else if (n - i == 2u)
        rf_out(0x06u | p[i] << 8 | (uint32_t)p[i + 1] << 16);
    else if (n - i == 1u)
        rf_out(0x05u | p[i] << 8);
}
/* SysEx in: gathered to F7, then the frame's body to the reader (announce.c) */
static uint8_t rf_sx[256];
static uint32_t rf_sx_n;
static void rf_sysex_in(uint32_t pkt)
{
    uint32_t cin = pkt & 0xFu, n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)(pkt >> (8u + 8u * i));
        if (c == 0xF0u)
            rf_sx_n = 0;
        if (rf_sx_n < sizeof rf_sx)
            rf_sx[rf_sx_n++] = c;
        if (c == 0xF7u) {
            if (rf_sx_n >= 2u && rf_sx[0] == 0xF0u && rf_sx_n < sizeof rf_sx) {
                const uint8_t *body = rf_sx + 1;
                uint32_t len = rf_sx_n - 2u;
                if (len >= 4u && body[0] == ANN_H0 && body[1] == ANN_H1 && body[2] == ANN_H2 && body[3] == ANN_HELLO)
                    sim_listen = 1;             /* (then again every 2 s, as a reader keeps saying it) */
                if (len >= 4u && body[0] == ANN_H0 && body[1] == ANN_H1 && body[2] == ANN_H2 && body[3] == ANN_BYE)
                    sim_listen = 0;
                ann_frame(body, len, now_ms);
            }
            rf_sx_n = 0;
        }
    }
}
static int rf_midi_in(uint32_t pkt)
{
    uint32_t cin = pkt & 0xFu;
    if (cin >= 4u && cin <= 7u) {
        rf_sysex_in(pkt);
        return 1;
    }
    if (mi_w - mi_r >= MQ)
        return 0;                       /* no room: the host tries again */
    min_q[mi_w % MQ] = pkt & ~0xF0u;
    min_port[mi_w++ % MQ] = PORT_USB;
    return 1;
}
static int rf_midi_out_take(uint32_t *pkt)
{
    rf_out_app();
    if (rf_or == rf_ow)
        return 0;
    *pkt = rf_oq[rf_or++ % RF_OQ];
    return 1;
}

/* ---------------------------------------------------------- the entry points --- */
static void rf_options(const char *path, int no_flash, int save, int headless)
{
    (void)no_flash, (void)save, (void)headless;
    if (!path || strcmp(path, RF_STAGED) || !rf_staged)
        rf_flash_stage(0, 0);           /* no file system: anything but the staged bytes is a fresh flash */
}
static int rf_boot_options(int fail, const char *reason, int stage) { (void)fail, (void)reason, (void)stage; return 1; }
static void rf_init(int demo)
{
    (void)demo;
    if (!rf_staged)
        rf_flash_stage(0, 0);
    data_ready = 1;                     /* the data area is the image's, as staged */
    rf_store_in();
    rf_store_seen = store_writes;
    rf_led_pos_init();
    memcpy(rf_panel.btn_id, RF_BTN_ID, sizeof rf_panel.btn_id);
    memcpy(rf_panel.enc_id, RF_ENC_ID, sizeof rf_panel.enc_id);
    memset(rf_panel.enc_dir, 1, sizeof rf_panel.enc_dir);
    if (!rf_panel.master)
        rf_panel.master = 512;          /* MASTER at half, as the simulator starts */
    rf_input();
    boot();
    rf_booted = 1;
    rf_publish();
}
static void rf_tick(uint32_t ms)
{
    if (!rf_booted || rf_halted)
        return;
    rf_input();
    sim_ms_begin(ms);
}
static void rf_frame(void)
{
    if (!rf_booted || rf_halted)
        return;
    sim_ms_end(1);
    rf_publish();
}
static void rf_idle(void)
{
    if (!rf_booted || rf_halted)
        return;
    sim_ms_end(0);
    rf_publish();
}
/* frames (a multiple of 128) of s16 stereo: the app's APP_BLOCK renders, through a ring */
#define RF_RING (APP_BLOCK > 1024u ? 2u * APP_BLOCK : 2048u)
static int32_t rf_ring[RF_RING * 2u], rf_blk[2u * APP_BLOCK];
static uint32_t rf_rw, rf_rr;
static void rf_audio(int16_t *out, uint32_t frames)
{
    for (uint32_t k = 0; k < frames; k++) {
        int32_t l, r;
        if (rf_rw == rf_rr && rf_booted && !rf_halted) {
            sim_render(rf_blk);
            for (uint32_t j = 0; j < APP_BLOCK; j++) {
                uint32_t w = (rf_rw++ % RF_RING) * 2u;
                rf_ring[w] = rf_blk[2u * j];
                rf_ring[w + 1u] = rf_blk[2u * j + 1u];
            }
        }
        if (rf_rw == rf_rr) {
            out[2u * k] = out[2u * k + 1u] = 0;
            continue;
        }
        l = rf_ring[(rf_rr % RF_RING) * 2u] >> 8;           /* 24-bit samples to 16 */
        r = rf_ring[(rf_rr++ % RF_RING) * 2u + 1u] >> 8;
        out[2u * k] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        out[2u * k + 1u] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
}
static void rf_dump(void) {}
static void rf_stats(uint32_t *shed, uint32_t *cpu)
{
    if (shed)
        *shed = 0;
    if (cpu)
        *cpu = cpu_pct;
}
static void rf_ui_info(char *buf, uint32_t n)
{
    if (buf && n)
        snprintf(buf, n, "%s %s", RFM1_NAME, RFM1_VERSION);
}
static void rf_shutdown(void) { rf_store_out(); }
static int rf_halted_fn(void) { return rf_halted ? 0x100 : 0; }

#ifndef APP_BUTTON_NAMES                /* (app.h may name the buttons as its panel does) */
#define APP_BUTTON_NAMES {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", \
                          "OCT-", "OCT+"}
#endif
static const rf_core_t rf_core = {
    RF_ABI, RFM1_ID, RFM1_NAME, RFM1_VERSION, RFM1_SOURCE_URL, "fm1base", RFM1_LICENSE, 0, RF_FLASH_SIZE,
    APP_BUTTON_NAMES, 0, 0, &rf_panel, &RF_KEYMAP[0][0],
    rf_options, rf_boot_options, rf_init, rf_tick, rf_frame, rf_idle, rf_audio, rf_midi_in, rf_midi_out_take, rf_dump,
    rf_stats, rf_ui_info, rf_shutdown, rf_halted_fn, rf_flash_stage, rf_flash, rf_flash_dirty, rf_flash_sync, 0, 0,
    RFM1_CREDITS,
};

/* ------------------------------------------------------------------ exports --- */
RF_EXPORT("rfm1_version") int32_t rfm1_version(void) { return 1; }
RF_EXPORT("rfm1_get") const rf_core_t *rfm1_get(uint32_t abi) { return abi == RF_ABI ? &rf_core : 0; }
RF_EXPORT("rfm1_layout") int32_t rfm1_layout(int32_t i)
{
#define P(f) (int32_t)__builtin_offsetof(rf_panel_t, f)
#define C(f) (int32_t)__builtin_offsetof(rf_core_t, f)
    static const int32_t L[] = {
        sizeof(rf_panel_t), P(keys), P(buttons), P(keys_tap), P(buttons_tap), P(enc), P(master), P(key_vel),
        P(key_lean), P(led), P(led_play_green), P(lcd), P(lcd_writes), P(btn_id), P(enc_id), P(enc_dir), P(ready),
        sizeof(rf_core_t), C(abi_version), C(id), C(name), C(version), C(source_url), C(family), C(license), C(gated),
        C(flash_size), C(button_names), C(params), C(nparams), C(panel), C(keymap), C(options), C(credits),
    };
#undef P
#undef C
    return i >= 0 && i < (int32_t)(sizeof L / sizeof L[0]) ? L[i] : -1;
}
RF_EXPORT("rfm1_alloc") void *rfm1_alloc(uint32_t n) { return malloc(n ? n : 1u); }
RF_EXPORT("rfm1_free") void rfm1_free(void *p) { free(p); }
RF_EXPORT("rfm1_options") void rfm1_options(const char *path, int no_flash, int save, int headless) { rf_options(path, no_flash, save, headless); }
RF_EXPORT("rfm1_boot_options") int32_t rfm1_boot_options(int fail, const char *reason, int stage) { return rf_boot_options(fail, reason, stage); }
RF_EXPORT("rfm1_init") void rfm1_init(int demo) { rf_init(demo); }
RF_EXPORT("rfm1_tick") void rfm1_tick(uint32_t ms) { rf_tick(ms); }
RF_EXPORT("rfm1_frame") void rfm1_frame(void) { rf_frame(); }
RF_EXPORT("rfm1_idle") void rfm1_idle(void) { rf_idle(); }
RF_EXPORT("rfm1_audio") void rfm1_audio(int16_t *out, uint32_t frames) { rf_audio(out, frames); }
RF_EXPORT("rfm1_midi_in") int32_t rfm1_midi_in(uint32_t pkt) { return rf_midi_in(pkt); }
RF_EXPORT("rfm1_midi_out_take") int32_t rfm1_midi_out_take(uint32_t *pkt) { return rf_midi_out_take(pkt); }
RF_EXPORT("rfm1_halted") int32_t rfm1_halted(void) { return rf_halted_fn(); }
RF_EXPORT("rfm1_shutdown") void rfm1_shutdown(void) { rf_shutdown(); }
RF_EXPORT("rfm1_flash_stage") int32_t rfm1_flash_stage(const uint8_t *bytes, uint32_t n) { return rf_flash_stage(bytes, n); }
RF_EXPORT("rfm1_flash") uint8_t *rfm1_flash(void) { return rf_flash(); }
RF_EXPORT("rfm1_flash_dirty") int32_t rfm1_flash_dirty(void) { return (int32_t)rf_flash_dirty(); }
RF_EXPORT("rfm1_flash_sync") int32_t rfm1_flash_sync(void) { return rf_flash_sync(); }
RF_EXPORT("rfm1_stats") void rfm1_stats(uint32_t *shed, uint32_t *cpu) { rf_stats(shed, cpu); }
RF_EXPORT("rfm1_ui_info") void rfm1_ui_info(char *buf, uint32_t n) { rf_ui_info(buf, n); }
