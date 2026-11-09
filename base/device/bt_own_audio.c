/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Bluetooth audio on the own stack (fm1.toml bt_audio = true, with ble_stack = "own": FM1B_BT_AUDIO): bt/br,
 * Henri David's BR/EDR stack from Optimist (docs/bt/BT-CLASSIC.md), an A2DP source to headphones and speakers: the
 * inquiry, the page, Secure Simple Pairing, L2CAP, AVDTP and AOSP's SBC encoder. The render's output goes to
 * bt_audio_feed (main_fm1.c), the main loop runs bti_poll. Part of bt_own.c. */
#define FELUCCA_BT_AUDIO 1
#define FELUCCA_FLASH 1
#ifndef FELUCCA_BT_AFH
#define FELUCCA_BT_AFH 1                          /* the channels a headset fails on leave the hopping (bt_afh.c) */
#endif
#define FELUCCA_BLE 1                             /* (the BLE stack is in this build: bt_own.c) */
#pragma push_macro("FELUCCA_CDC")              /* (unity.c's, FM1B_CDC: back after the stack, below) */
#undef FELUCCA_CDC
#define FELUCCA_CDC 0                             /* no console: the probe's printing compiles away */
#define FELUCCA_BT_DIAG 0
#define FELUCCA_DUAL 0                            /* (the second core is fm1-base's: it keeps to RAM, cpu2.c) */
#define IN_POOL PLAT_POOL
#ifndef BTI_RING
#define BTI_RING 12u                              /* receptions waiting for the main loop (Optimist's 32 overflowed only while
                                                   * its console printed; there is none here): 7.3 KB of pool less */
#endif
#ifndef BA_ATTR
#define BA_ATTR                                   /* the link's state (its receive frame, SDP) in RAM too */
#endif
#ifndef BP_MQ_ATTR
#define BP_MQ_ATTR                                /* the audio packets' queue (40 x 332 B) in RAM, not the pool: the pool
                                                   * is the tighter of the two in a big app (LOOM-8) */
#endif
/* the console Optimist's probe prints to: with FM1B_BT_LOG its lines go to the radio's log (radio.c shim_log, read by
 * tools/fm1_remote.py log), cut to the log's width; without it, nowhere */
#ifndef FM1B_BT_LOG
#define FM1B_BT_LOG 0
#endif
#if FM1B_BT_LOG
static char con_line[40];
static uint32_t con_n;
static void con_putc(char c)
{
    if (c == '\n' || c == '\r') {
        if (con_n) {
            con_line[con_n] = 0;
            shim_log(con_line);
            con_n = 0;
        }
        return;
    }
    if (con_n == sizeof con_line - 1u) {          /* (full: this part out, the rest on the next line) */
        con_line[con_n] = 0;
        shim_log(con_line);
        con_n = 0;
    }
    con_line[con_n++] = c;
    if (con_n == 8u && !memcmp(con_line, "btpage: ", 8))
        con_n = 0;                                /* (the probe's prefix: the log's width is short) */
}
static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}
static void con_hex(uint32_t v, uint32_t n)
{
    while (n--)
        con_putc("0123456789ABCDEF"[(v >> (4u * n)) & 15u]);
}
static void con_dec(uint32_t v)
{
    char t[12];
    put_dec(t, v);
    con_puts(t);
}
#else
static void con_puts(const char *s) { (void)s; }
static void con_putc(char c) { (void)c; }
static void con_hex(uint32_t v, uint32_t n) { (void)v, (void)n; }
static void con_dec(uint32_t v) { (void)v; }
#endif
#define SY_TPS_Q8 (24000000u * 256u / 44100u)    /* TIMER4 ticks a sample, Q8 (bt_acl.c's latency figure) */
#include "bt_audio.h"
static int own_bt_trims(struct ble_rf_trims *t);
#define BTI_RADIO_TRIMS own_bt_trims              /* (bt_inq.c bti_radio_up: our constants, the 187 measured) */
#include "bt_inq.c"
#pragma pop_macro("FELUCCA_CDC")

static int own_bt_trims(struct ble_rf_trims *t)
{
    *t = OWN_TRIMS;
    fm1_rf_cal_on = 1;
    return 1;
}

/* ---- what is kept: our BR/EDR address and the remembered devices with their link keys (OBJ_BLE, own_store) */
_Static_assert(sizeof own.bta_mem == sizeof bta.mem && sizeof own.bta_own == sizeof bta.own, "bt_own.c own_store: bta's");
static void own_bta_load(void)                    /* ble_start: from the store into bta */
{
    ble_cpy(bta.own, own.bta_own, sizeof bta.own);
    bta.own_set = own.bta_own_set;
    ble_cpy((uint8_t *)bta.mem, own.bta_mem, sizeof bta.mem);
    bta.nmem = own.bta_nmem <= BTA_MEMS ? own.bta_nmem : 0u;
}
static void own_bta_keep(void)                    /* ble_poll: a new key or address into the store, saved once quiet */
{
    if (!bta.key_new)
        return;
    bta.key_new = 0;
    ble_cpy(own.bta_own, bta.own, sizeof bta.own);
    own.bta_own_set = bta.own_set;
    ble_cpy(own.bta_mem, (const uint8_t *)bta.mem, sizeof bta.mem);
    own.bta_nmem = bta.nmem;
    own_dirty = 1;
}

/* ---- the main loop's part (bt_own.c ble_poll): after a fresh pairing the link is let go and the device paged again
 * with the new key (bt_acl.c ba.rejoin, as BlueZ does; Optimist's screen did it). A device that has just let go may not
 * answer at once (a SoundCore 2, 2026-10-26: no answer to three pages 1.5 s apart, one a minute later did): up to
 * OWN_BTA_TRIES pages, OWN_BTA_GAP_MS after each failed one */
#define OWN_BTA_TRIES 5u
#define OWN_BTA_GAP_MS 4000u
static uint8_t own_bta_tries;
static uint32_t own_bta_t;
static void own_bta_poll(void)
{
    static uint8_t was_scanning;
    uint32_t st;
    if (bta.mode == BTA_OFF)
        return;
    st = bta_state();
    if (was_scanning && st != BTA_S_SCANNING) {   /* a scan over: the answers heard (FHS) and the devices listed */
        char b[40], *q = b;
        for (const char *t = "audio scan: heard "; *t; )
            *q++ = *t++;
        q = put_dec(q, bti_st.fhs);
        for (const char *t = ", listed "; *t; )
            *q++ = *t++;
        put_dec(q, bta.n);
        shim_log(b);
    }
    was_scanning = (uint8_t)(st == BTA_S_SCANNING);
    if (bta_rejoin()) {                           /* (the link closed after the pairing: paged again) */
        own_bta_tries = 1;
        own_bta_t = fm1_ms;
        shim_log("audio: paired, connecting with the key");
        return;
    }
    if (own_bta_tries && bta_state() == BTA_S_STREAMING)
        own_bta_tries = 0;
    else if (own_bta_tries && bta_state() == BTA_S_FAILED && fm1_ms - own_bta_t >= OWN_BTA_GAP_MS) {
        if (own_bta_tries >= OWN_BTA_TRIES || !bta.nmem) {
            own_bta_tries = 0;
            shim_log("audio: no answer after the pairing");
        } else {
            own_bta_tries++;
            own_bta_t = fm1_ms;
            (void)bta_page(bta.mem[0].addr);
        }
    }
}

/* ---- the radio's BR/EDR interrupts: their vectors once (masked until a run), as BLE's */
static void own_bta_attach(void)
{
    uint32_t f;
    if (own_bta_attached)
        return;
    own_bta_attached = 1;
    f = irq_save();
    fm1_guard_unlock_top();
    bti_attach();
    fm1_guard_lock_top();
    irq_restore(f);
}

/* ---- the render's output (main_fm1.c render_block, the audio interrupt): 24-bit in int32 -> 16-bit for the SBC
 * encoder, a block of REND_FRAMES in pieces (o 0: silence, the app's app_bt_frames). 1: it streams (the FM-1's own
 * output is muted then) */
static int own_bta_feed(const int32_t *o, uint32_t n)
{
    int32_t q[2u * 32u];
    uint32_t i, k, m;
    int on = 0;
    for (k = 0; k < n; k += m) {
        m = n - k > 32u ? 32u : n - k;
        for (i = 0; i < 2u * m; i++)
            q[i] = o ? (o[2u * k + i] + 128) >> 8 : 0;
        on = bt_audio_feed(q, m);
        if (!on)
            break;
    }
    return on;
}

/* ---- plat.h */
uint32_t plat_bta_mem_bytes(void)
{
#if FM1B_BTA_MEM
    return BP_MEM_BYTES;
#else
    return 0;
#endif
}
int plat_bta_mem(void *p)                         /* (the main loop) */
{
#if FM1B_BTA_MEM
    uint32_t f;
    if (!p && (bta.mode != BTA_OFF || bti_busy()))
        return -1;                                /* (still read: the mode on, or the run being let go, bta_stop) */
    if ((uintptr_t)p & 3u)
        return -1;
    f = irq_save();                               /* (neither the audio's interrupt nor the radio's mid-way) */
    bp_mem_set(p);
    irq_restore(f);
    return 0;
#else
    return p ? -1 : 0;
#endif
}
void plat_bta_on(int on)
{
#if FM1B_BTA_MEM
    if (on && !BTI_MEM_HAVE)
        return;                                   /* (no block lent: off, as asked for without one) */
#endif
    if (on) {
        if (ble_state == BLE_OFF)
            ble_start();                          /* (the radio's start-up and calibration are BLE's: rf_share.h) */
        own_bta_attach();
        if (bta.mode == BTA_OFF)
            bta_start(BTA_MAN);
    } else if (bta.mode != BTA_OFF)
        bta_stop();
}
void plat_bta_scan(int on)
{
    if (bta.mode != BTA_OFF)
        bta_scan(on == 2 ? 2 : on ? 1 : 0);       /* (2: the inquiry again, the list kept: bt_audio.c) */
}
int plat_bta_found(int i, char *name)             /* found device i: its name (17 bytes) -> 0: none */
{
    if (i < 0 || (uint32_t)i >= bta.n)
        return 0;
    if (name)
        str_cpy(name, bta_dev_name((uint32_t)i), 17u);
    return 1;
}
int plat_bta_known(int i, char *name)             /* remembered device i (0 the last one) -> 0: none */
{
    if (i < 0 || (uint32_t)i >= bta.nmem)
        return 0;
    if (name)
        str_cpy(name, bta.mem[i].name, 17u);
    return 1;
}
void plat_bta_pick(int i)                         /* found device i: connect (it becomes the last one) */
{
    if (bta.mode != BTA_OFF && i >= 0)
        bta_pick((uint32_t)i);
}
int plat_bta_reconnect(int i)                     /* remembered device i (0 the last one): connect */
{
    uint32_t st;
    if (bta.mode == BTA_OFF || i < 0 || (uint32_t)i >= bta.nmem)
        return -1;
    st = bta_state();
    if ((st == BTA_S_LINKED || st == BTA_S_SETUP || st == BTA_S_STREAMING || st == BTA_S_PAGING) &&
        ble_eq(bp.remote, bta.mem[i].addr, 6))
        return 0;                                 /* (that device already: paging it again would end its link) */
    if (i)
        bta_front((uint32_t)i);                   /* it becomes the last one: the next power-on reconnects it */
    return bta_page(bta.mem[0].addr) ? 0 : -1;
}
void plat_bta_forget(int i)
{
    if (i >= 0)
        bta_forget((uint32_t)i);
    bta.key_new = 1;
}
void plat_bta_hangup(void) { bta_hangup(); }
int plat_bta_state(void) { return (int)bta_state(); }   /* PLAT_BTA_S_* (the same values as bt_audio.h's BTA_S_*) */
void plat_bta_output(int to_device) { bta_output(to_device != 0); }   /* (streaming only: bt_audio.c) */
int plat_bta_out_device(void) { return bta_out_bt(); }
int plat_bta_lost(void)                           /* (PLAT_BTA_LOST_*: bt_audio.c's BTA_W_*, in the same order) */
{
    uint32_t w = bta_seen;
    return bta_lost() ? (int)(w % 7u) : PLAT_BTA_LOST_NONE;
}
int plat_bta_timed_out(void) { return bta_state() == BTA_S_REFUSED && bta_gave_up(); }
int32_t plat_bta_latency_us(void)
{
    int32_t t = bta_lat_t();                      /* (TIMER4 ticks: bt_acl.c) */
    return t < 0 ? -1 : t / (int32_t)FM1_TICKS_PER_US;
}
_Static_assert(PLAT_BTA_LOST_LINK == BTA_W_LINK && PLAT_BTA_LOST_REFUSED == BTA_W_REFUSED, "plat.h PLAT_BTA_LOST_*: bt_audio.c's BTA_W_*");
_Static_assert(PLAT_BTA_STREAMING == BTA_S_STREAMING && PLAT_BTA_READY == BTA_S_READY, "plat.h PLAT_BTA_*: bt_audio.h's BTA_S_*");

/* ---- the USB remote (main_fm1.c; tools/fm1_remote.py bta ...): F0 7D 4C 41 cmd [arg] F7
 *   1 on  2 off  3 scan  4 status ("<state> <found> <known>")  5 the found devices, a line each  6 i: connect found i
 *   7 i: connect remembered i  8 the remembered devices  9 hang up  10 i: forget remembered i */
static void bta_line(const char *t)
{
    uint8_t f[64];
    uint32_t n = 0;
    f[n++] = 0xF0; f[n++] = 0x7D; f[n++] = 0x4C; f[n++] = 0x41; f[n++] = 0x10;
    while (*t && n < sizeof f - 1u)
        f[n++] = (uint8_t)(*t++ & 0x7Fu);
    f[n++] = 0xF7;
    ota_wire_send(f, n);
}
static int bta_frame(const uint8_t *p, uint32_t n)
{
    static const char *const ST[] = {"off", "scanning", "found", "none", "paging", "linked", "setup", "streaming",
                                     "refused", "no-last", "failed", "ready"};
    static const uint8_t end[6] = {0xF0, 0x7D, 0x4C, 0x41, 0x11, 0xF7};
    char b[48], *q;
    int i, arg = n >= 5u ? p[4] : 0;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x41u)
        return 0;
    switch (p[3]) {
    case 1: plat_bta_on(1); break;
    case 2: plat_bta_on(0); break;
    case 3: plat_bta_scan(1); break;
    case 4: {
        int st = plat_bta_state();
        q = b;
        for (const char *t = st >= 0 && st <= BTA_S_READY ? ST[st] : "?"; *t; )
            *q++ = *t++;
        *q++ = ' ';
        q = put_dec(q, bta.n);
        *q++ = ' ';
        q = put_dec(q, bta.nmem);
        *q++ = ' ';
        put_dec(q, bp_mq.w - bp_mq.r);
        bta_line(b);
        break;
    }
    case 5:
        for (i = 0; plat_bta_found(i, b); i++)
            bta_line(b);
        break;
    case 6: plat_bta_pick(arg); break;
    case 7: (void)plat_bta_reconnect(arg); break;
    case 8:
        for (i = 0; plat_bta_known(i, b); i++)
            bta_line(b);
        break;
    case 9: plat_bta_hangup(); break;
    case 10: plat_bta_forget(arg); break;
    default: break;
    }
    ota_wire_send(end, sizeof end);
    return 1;
}
