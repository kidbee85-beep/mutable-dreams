/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Bluetooth MIDI on Optimist's own stack (fm1.toml ble_stack = "own": FM1B_BLE_OWN): bt/ble, Henri David's, from the
 * Bluetooth Core and BLE-MIDI specifications (docs/bt/BLE-STACK.md), and felucca-bsp's chip/fm1_ble.h for the radio.
 * No JieLi library, no shim, no tasks: the stack runs in the radio's two interrupts (IRQ 45 event, IRQ 29 RX, at
 * priority 2, below the audio and the tick) and talks to the main loop through single-producer rings.
 *
 * What this file gives radio.c (the MIDI rings, the clock smoothing, the log and plat.h stay radio.c's): ble_start,
 * ble_poll, ble_midi_send_many, ble_midi_connected and the plat_ble_* calls. One link at a time: the computer's
 * (link 0, the FM-1 the peripheral) or, with ble_central, a device's (link 1, the FM-1 the central).
 *
 * The radio is calibrated at every start, by our own measurement (felucca-bsp chip/fm1_rf_cal.h, after JieLi's SDK;
 * docs/bt/RF-CALIBRATION.md): nothing stored is needed, so a unit whose stock calibration was overwritten (the SDK's
 * VM at 0x0E8000 lies in PLAT_XDATA1) works the same. Records 106, 107 and 108 are the SDK board's constants.
 *
 * Part of radio.c (the unity build). */
#ifndef BLE_DEVICE_NAME
#define BLE_DEVICE_NAME "FM-1"                    /* "FM-1 XXXX", XXXX this FM-1's own (own_addr), as JieLi's build named it */
#endif
#if FM1B_BLE_CENTRAL && !defined(BLE_CENTRAL)
#define BLE_CENTRAL 1
#endif
#include "ble_stack.c"
#if FM1B_BLE_MIDI_TIME
#include "ble_time.c"
#endif
#if BLE_DIAG
#include "ble_diag.c"
#endif

#if FM1B_BT_AUDIO
static void own_bta_load(void);
static void own_bta_keep(void);
static void own_bta_poll(void);
static void bti_poll(void);
static uint8_t ba_live;
static uint8_t own_bta_attached;
#endif
/* ---- what is kept (OBJ_BLE): the address, the radio calibration's copy, the bond */
#define OWN_MAGIC 0x4F574E33u                     /* "OWN3": the JieLi build's records in OBJ_BLE read as none */
static struct own_store {
    uint32_t magic;
    uint8_t addr[6], have_bond, pad;
    uint8_t bond_rand[8], bond_ltk[16];
    uint16_t bond_ediv, pad2;
    uint8_t bta_own[6], bta_own_set, bta_nmem;    /* Bluetooth audio (bt_own_audio.c): our BR/EDR address, the devices */
    uint8_t bta_mem[4 * 40];                      /* (struct bta_mem x BTA_MEMS, bt_audio.h) */
} own;
static volatile uint8_t own_dirty;               /* the main loop saves it (a flash write: never from an interrupt) */
#ifdef APP_BLE_STORE
/* kept in the app's own data (app.h APP_BLE_STORE): own as plat.h's record, field by field */
_Static_assert(sizeof own.bta_mem == sizeof ((struct plat_ble_rec *)0)->bta, "plat.h plat_ble_rec bta: bt_audio.h's bta_mem x 4");
static int own_load(void)
{
    struct plat_ble_rec r;
    ble_zero((uint8_t *)&r, sizeof r);
    if (!app_ble_load(&r) || (r.addr[5] >> 6) != 3u)
        return 0;                                 /* (none, or not a static random address) */
    ble_zero((uint8_t *)&own, sizeof own);
    own.magic = OWN_MAGIC;
    ble_cpy(own.addr, r.addr, 6);
    own.have_bond = (uint8_t)(r.bonded != 0u);
    own.bond_ediv = r.bond_ediv;
    ble_cpy(own.bond_rand, r.bond_rand, 8);
    ble_cpy(own.bond_ltk, r.bond_ltk, 16);
    ble_cpy(own.bta_own, r.bta_addr, 6);
    own.bta_own_set = (uint8_t)(r.bta_addr_set != 0u);
    own.bta_nmem = r.bta_n <= 4u ? r.bta_n : 0u;
    ble_cpy(own.bta_mem, (const uint8_t *)r.bta, sizeof own.bta_mem);
    return 1;
}
static int own_save(void)
{
    struct plat_ble_rec r;
    ble_zero((uint8_t *)&r, sizeof r);
    ble_cpy(r.addr, own.addr, 6);
    r.bonded = own.have_bond;
    r.bond_ediv = own.bond_ediv;
    ble_cpy(r.bond_rand, own.bond_rand, 8);
    ble_cpy(r.bond_ltk, own.bond_ltk, 16);
    ble_cpy(r.bta_addr, own.bta_own, 6);
    r.bta_addr_set = own.bta_own_set;
    r.bta_n = own.bta_nmem;
    ble_cpy((uint8_t *)r.bta, own.bta_mem, sizeof own.bta_mem);
    return app_ble_save(&r);
}
#endif

/* the address: a static random one (Core Vol 6 Part B 1.3.2.1: the top two bits 11) from the flash's unique ID, its two
 * low octets the hash the JieLi build named itself by (radio.c ble_midi_name), so the name stays "FM-1 XXXX" */
static void own_addr(uint8_t a[6])
{
    const uint8_t *u = plat_chip_uid();
    uint32_t h = 2166136261u, g = 0x9E3779B9u, i = 1;
    while (u && i < 16u && u[i] == u[0])
        i++;
    if (u && i < 16u) {
        for (i = 0; i < 16u; i++) {
            h = (h ^ u[i]) * 16777619u;
            g = (g ^ u[i]) * 2246822519u;
        }
        h ^= h >> 16;
    } else {                                      /* no ID: a random one, kept */
        h = fm1_ticks();
        g = fm1_ticks() * 2654435761u;
    }
    a[0] = (uint8_t)h;
    a[1] = (uint8_t)(h >> 8);
    a[2] = (uint8_t)g;
    a[3] = (uint8_t)(g >> 8);
    a[4] = (uint8_t)(g >> 16);
    a[5] = (uint8_t)((g >> 24) | 0xC0u);
}

/* ---- the stack's MIDI, both ways: two single-producer rings */
#define OQ 64u                                    /* out: the main loop writes, the BLE interrupts read */
static struct { uint32_t pkt; uint16_t ms; } own_out[OQ];
static volatile uint32_t oo_w, oo_r;
#define IQ 64u                                    /* in: the BLE interrupts write, the main loop reads */
#if FM1B_BLE_MIDI_TIME
static struct { uint32_t pkt, t24; } own_in[IQ];   /* t24: the time it was sent (ble_in_time), plat_ticks24's */
static struct ble_in_clock own_clk;
#else
static struct { uint32_t pkt; uint16_t ts; } own_in[IQ];
#endif
static volatile uint32_t oi_w, oi_r, oi_lost;
static volatile uint8_t own_role;                 /* 0 none, 1 a computer connected to us, 2 we to a device */
static volatile uint32_t own_events;              /* ble_app_state calls (the main loop logs what changed) */

static int own_q_peek(uint32_t *pkt, uint32_t *t_ms)
{
    if (oo_r == oo_w)
        return 0;
    *pkt = own_out[oo_r % OQ].pkt;
    *t_ms = own_out[oo_r % OQ].ms;
    return 1;
}
static void own_q_pop(void)
{
    if (oo_r != oo_w)
        oo_r++;
}
static int own_stream_on(void) { return own_role == 1u && ble_midi_ready(); }   /* (the computer's link, listening) */
static uint32_t own_stream_ms(void) { return fm1_ms; }
#include "bt_own_stream.h"                        /* ble_app_midi_peek / pop: the queue and the app's stream */
BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts)
{
#if !FM1B_BLE_MIDI_TIME
    (void)last_ts;
#endif
    if (oi_w - oi_r >= IQ) {
        oi_lost++;
        return;
    }
    own_in[oi_w % IQ].pkt = own_role == 2u ? (pkt & ~0xF0u) | 0x10u : pkt & ~0xF0u;   /* a device's: cable 1 */
#if FM1B_BLE_MIDI_TIME
    own_in[oi_w % IQ].t24 = ble_in_time(&own_clk, fm1_ticks(), 1000u * FM1_TICKS_PER_US, ts, last_ts);
#else
    own_in[oi_w % IQ].ts = ts;
#endif
    RING_PUBLISH();
    oi_w++;
}
BLE_API void ble_app_state(void)
{
#if BLE_CENTRAL
    own_role = ble_central_state() != BLE_CS_IDLE ? 2u : ble_connected() ? 1u : 0u;
#else
    own_role = ble_connected() ? 1u : 0u;
#endif
    own_events++;
}
#if BLE_SMP_LEGACY
static uint8_t own_bond_new;
BLE_API void ble_app_bond(const uint8_t rand[8], uint16_t ediv, const uint8_t ltk[16])
{
    ble_cpy(own.bond_rand, rand, 8);
    ble_cpy(own.bond_ltk, ltk, 16);
    own.bond_ediv = ediv;
    own.have_bond = 1;
    own_bond_new = 1;
    own_dirty = 1;
}
BLE_API void ble_app_peer_id(const uint8_t irk[16], const uint8_t addr[6], uint8_t addr_rand)
{
    (void)irk, (void)addr, (void)addr_rand;       /* (a returning central is found by EDIV / Rand) */
}
#endif

/* ---- radio.c's side */
int ble_midi_connected(int link)
{
    if (ble_state != BLE_READY)
        return 0;
    return link == 0 ? own_role == 1u && ble_midi_ready() : own_role == 2u && ble_midi_ready();
}
int ble_midi_send_many(int link, const uint32_t *pkts, const uint16_t *ms, int n)
{
    int k = 0;
    if (!ble_midi_connected(link))
        return 0;
    while (k < n && oo_w - oo_r < OQ) {
        own_out[oo_w % OQ].pkt = pkts[k];
        own_out[oo_w % OQ].ms = ms[k];
        RING_PUBLISH();
        oo_w++;
        k++;
    }
    return k;
}
int ble_midi_peripheral_connected(void) { return own_role == 1u && ble_midi_ready(); }
int ble_midi_peripheral_linked(void) { return own_role == 1u; }

static void own_held(int hold)                    /* the BLE interrupts held while the main loop changes the stack */
{
    fm1_ble_irqs_hold(hold);
}

/* the SDK board's values for the three constant records (RF-CALIBRATION.md §3.1, §3.3, §3.4): crystal load, PA bias,
 * Wi-Fi per-rate gains (zero: the defaults kept). 187 is measured (fm1_rf_cal_on) */
static const struct ble_rf_trims OWN_TRIMS = {{0x0B, 0x0B}, {1, 7, 4, 7, 11, 1, 7}, {0}, {0}};

static void own_cal_log(void)                     /* what the calibration measured, on the log */
{
    char b[40], *q = b;
    for (const char *t = "cal "; *t; )
        *q++ = *t++;
    hex8(q, (uint32_t)fm1_rfc_scan_rc << 24 | (uint32_t)(fm1_rfc_trim_rc & 0xFF) << 16 | fm1_rfc.mid << 8 | fm1_rfc.bank121);
    q += 8;
    *q++ = ' ';
    hex8(q, (uint32_t)fm1_rfc.flt[0] << 16 | fm1_rfc.flt[1] << 8 | fm1_rfc.flt[2]);
    q += 8;
    *q++ = ' ';
    hex8(q, (uint32_t)fm1_rfc.txlo_i << 8 | fm1_rfc.txlo_q);
    q[8] = 0;
    shim_log(b);
    q = b;
    for (const char *t = "cal us "; *t; )
        *q++ = *t++;
    q = put_dec(q, fm1_rfc.us_scan);
    *q++ = ' ';
    q = put_dec(q, fm1_rfc.us_filter);
    *q++ = ' ';
    q = put_dec(q, fm1_rfc.us_txlo);
    *q++ = ' ';
    q = put_dec(q, fm1_rfc.us_rxdc);
    *q++ = ' ';
    q = put_dec(q, fm1_rfc.to_dem);
    shim_log(b);
}

static void ble_start(void)
{
    char b[40];
    uint32_t f;
    if (ble_state != BLE_OFF)
        return;
    ble_state = BLE_STARTING;
    radio_begin("BT: starting (own stack)");
#ifdef APP_BLE_STORE
    if (!own_load()) {
#else
    if (plat_store_load(OBJ_BLE, &own, sizeof own) != (int)sizeof own || own.magic != OWN_MAGIC) {
#endif
        ble_zero((uint8_t *)&own, sizeof own);
        own.magic = OWN_MAGIC;
        own_addr(own.addr);
        own_dirty = 1;
    }
#if FM1B_BT_AUDIO
    own_bta_load();
#endif
    ble_init(own.addr, 1);
#if BLE_SMP_LEGACY
    if (own.have_bond)
        ble_host_set_key(own.bond_rand, own.bond_ediv, own.bond_ltk);
#endif
    f = irq_save();
    fm1_guard_unlock_top();
    fm1_ble_irq_attach(BLE_HW_IRQ_PRIO);          /* the two vectors, masked until the radio runs */
    fm1_ble_irqs_hold(1);
    fm1_guard_lock_top();
    irq_restore(f);
    fm1_rf_cal_on = 1;                            /* (our own measurement, not a stored 187) */
    ble_hw_wl82_start(&OWN_TRIMS);                /* the radio's start-up, its calibration (crystal ramp from RAM) */
    own_cal_log();
    radio_cal_log();
    {
        const uint8_t *p;
        uint8_t n = ble_gap_name(&p), i;
        for (i = 0; i < n && i < 28u; i++)
            b[11 + i] = (char)p[i];
        b[11 + i] = 0;
        b[0] = 'a'; b[1] = 'd'; b[2] = 'v'; b[3] = 'e'; b[4] = 'r'; b[5] = 't'; b[6] = 'i'; b[7] = 's'; b[8] = 'e';
        b[9] = ':'; b[10] = ' ';
        shim_log(b);
    }
    own_held(1);
    ble_enable(1);
    own_held(0);
    ble_state = BLE_READY;
}

#if BLE_DIAG
static void own_diag_line(void)                   /* the link layer's counters, a line on the log */
{
    char b[40], *q = b;
    q = put_dec(q, ble_dg.adv_events);
    *q++ = ' ';
    q = put_dec(q, ble_dg.cind_ok);
    *q++ = '/';
    q = put_dec(q, ble_dg.cind_rej);
    *q++ = ' ';
    q = put_dec(q, ble_dg.rx_good);
    *q++ = '/';
    q = put_dec(q, ble_dg.rx_crc_bad);
    *q++ = ' ';
    q = put_dec(q, ble_dg.closes);
    *q++ = ':';
    q = put_dec(q, ble_dg.close_reason);
    *q++ = ' ';
    q = put_dec(q, ble_dg.isr_max_us);
    *q++ = ' ';
    *q++ = 'i';                                   /* MIDI messages in from the link, out to it, in lost */
    q = put_dec(q, oi_w);
    *q++ = ' ';
    *q++ = 'o';
    q = put_dec(q, oo_r);
    *q++ = ' ';
    put_dec(q, oi_lost);
    shim_log(b);
}
#endif

#if FM1B_BLE_CENTRAL
static void own_central_poll(void);
#endif

static void ble_poll(void)
{
    static uint32_t seen_events;
#if BLE_DIAG
    static uint32_t last_diag;
#endif
    static uint8_t role_said = 0xFF, ready_said;
    uint8_t conn = 0;
    int l;
    if (ble_state == BLE_OFF)
        return;
    radio_settle(1);
    if (ble_state != BLE_READY)
        return;
#if FM1B_BLE_CENTRAL
    own_central_poll();
#endif
#if FM1B_BT_AUDIO
    if (own_bta_attached) {
        bti_poll();                               /* Bluetooth audio: the page, the link, the stream's packets */
        own_bta_poll();
        own_bta_keep();
        own_held(1);                              /* while it streams, the MIDI link asked to slow to 30-45 ms (the
                                                   * audio's air; ble_host.c ble_sig_audio: at 7.5-15 ms half the MIDI
                                                   * packets failed CRC beside the stream, 2026-10-26) */
        ble_sig_audio(ba_live);
        own_held(0);
    }
#endif
    while (oi_r != oi_w) {                        /* MIDI in, to radio.c's rings (and its clock smoothing) */
#if FM1B_BLE_MIDI_TIME
        ble_midi_receive_at(own_in[oi_r % IQ].pkt, own_in[oi_r % IQ].t24);
#else
        ble_midi_receive(own_in[oi_r % IQ].pkt, own_in[oi_r % IQ].ts);
#endif
        RING_PUBLISH();
        oi_r++;
    }
    for (l = 0; l < BLINKS; l++)
        if (ble_midi_connected(l))
            conn |= (uint8_t)(1u << l);
    ble_conn = conn;
    for (l = 0; l < BLINKS; l++) {
        if (!(conn >> l & 1u)) {
            ble_out[l].r = ble_out[l].w;          /* a link gone: nothing stale for the next one */
            ble_out[l].cr = ble_out[l].cw;
        }
        ble_midi_drain(l);
    }
    if (seen_events != own_events || ready_said != (uint8_t)ble_midi_ready()) {
        seen_events = own_events;
        ready_said = (uint8_t)ble_midi_ready();
        if (role_said != own_role || ready_said)
            shim_log(own_role == 1u ? (ready_said ? "computer: MIDI on" : "computer connected")
                     : own_role == 2u ? (ready_said ? "device: MIDI on" : "device connected") : "no link: advertising");
        role_said = own_role;
    }
#if BLE_DIAG
    if (shim_ms() - last_diag > 10000u) {         /* every 10 s: the counters (fm1_remote.py log) */
        last_diag = shim_ms();
        own_diag_line();
    }
#endif
    if (own_dirty && !(plat_ble_connected() && plat_midi_count(PORT_BLE, 0) && plat_midi_ago(PORT_BLE, 0) < 2000u)) {
        own_dirty = 0;                            /* (kept once MIDI from Bluetooth pauses: a write holds the CPU) */
#ifdef APP_BLE_STORE
        if (own_save())
#else
        if (plat_store_save(OBJ_BLE, &own, sizeof own))
#endif
            shim_log("store: not saved");
#if BLE_SMP_LEGACY
        else if (own_bond_new) {
            own_bond_new = 0;
            shim_log("bonded: key kept");
        }
#endif
    }
}

/* ---- plat.h */
static uint8_t ble_off;                           /* switched off by the player (plat_ble_enable) */
int plat_ble_state(void) { return ble_off ? BLE_OFF : ble_state; }
int plat_ble_connected(void) { return !ble_off && ble_state == BLE_READY && ble_midi_peripheral_connected(); }
int plat_ble_linked(void) { return !ble_off && ble_state == BLE_READY && ble_midi_peripheral_linked(); }
void plat_ble_link_info(uint32_t v[7])            /* the computer's link as agreed (0 each: none) */
{
    int on = !ble_off && ble_state == BLE_READY && own_role == 1u, i;
    v[0] = on ? ble_ll_tx_octets() : 0u;
    v[1] = on ? ble_att_mtu() : 0u;
    v[2] = on ? ble_ll_interval() : 0u;
    for (i = 0; i < 4; i++)
        v[3 + i] = on ? ble_ll_peer_len((uint32_t)i) : 0u;
}
void plat_ble_enable(int on)
{
    ble_off = (uint8_t)!on;
    if (on && ble_state == BLE_OFF)
        ble_start();
    else if (ble_state == BLE_READY) {
        own_held(1);
        ble_enable(on);
        own_held(0);
    }
    if (!on)
        shim_log("switched off");
}
#if FM1B_BT_AUDIO
#include "bt_own_audio.c"
#endif
#if FM1B_BLE_CENTRAL
#include "bt_own_central.c"
#else
void plat_ble_scan(int on) { (void)on; }
int plat_ble_found(int i, char *name, int *rssi) { (void)i, (void)name, (void)rssi; return 0; }
int plat_ble_connect(int i) { (void)i; return -1; }
void plat_ble_disconnect(void) {}
int plat_ble_device(void) { return BLE_DEV_OFF; }
int plat_ble_passkey(void) { return -1; }
#include "ble_peers_none.c"
#endif
void plat_ble_start(void) { ble_start(); }
