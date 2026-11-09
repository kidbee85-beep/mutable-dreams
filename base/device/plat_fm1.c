/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* fm1-base on the FM-1: plat.h over felucca-bsp's chip and board layers, the panel map, the USB rings and storage.
 * Part of the unity build (unity.c), after usb.c, storage.c and panel.c. */

uint32_t plat_ms(void) { return fm1_ms; }

/* remote control over USB-MIDI (main_fm1.c remote_frame, tools/fm1_remote.py): buttons and keys held, turns and
 * MASTER, added to the panel's own, so a program can drive the app as a player would */
static struct {
    volatile uint32_t btn, keys;
    volatile int32_t enc[NE];
    volatile int32_t master;                     /* -1: the pot's own */
} remote = {0, 0, {0}, -1};

uint32_t plat_buttons(void)
{
    uint32_t raw = fm1_in.buttons, out = 0, i;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            out |= 1u << i;
    return out | remote.btn | mirror_in_buttons();
}

uint32_t plat_keys(void) { return (fm1_in.notes | remote.keys | mirror_in_keys()) & ((1u << NKEYS) - 1u); }
static uint32_t plat_remote_keys(void) { return remote.keys; }

int32_t plat_enc(int role)
{
    int32_t r;
    if (role < 0 || role >= NE)
        return 0;
    r = remote.enc[role];
    remote.enc[role] = 0;
    r += fm1_enc_take(panel.enc[role]) * panel.dir[role] + mirror_in_enc(role);
    if (r)
        mirror_enc(role, r);                     /* (the mirror's input reports show the turns) */
    return r;
}

static volatile uint32_t master_pos = 2048;   /* MASTER's position, 0..4096 (main_fm1.c reads the pot; the remote's
                                               * or the mirror's when they set it) */
uint32_t plat_master(void) { return master_pos; }

/* the battery: ADC ch3 (its divider), a slow average by the main loop. The stock firmware's thresholds, as X0X, melodee
 * and dxsloop use them, give 0-3 bars; there is no charger line, so a USB host means power */
static int32_t batt_raw;
static int8_t batt_bars = -1;
int plat_battery(void)                           /* (a level moves once the reading is 4 past its threshold) */
{
    static const int16_t AT[3] = {531, 561, 591};
    if (usb.config && !usb.suspended)
        return 4;
    if (!batt_raw)
        return -1;
    if (batt_bars < 0)
        batt_bars = (int8_t)((batt_raw >= AT[0]) + (batt_raw >= AT[1]) + (batt_raw >= AT[2]));
    while (batt_bars < 3 && batt_raw >= AT[batt_bars] + 4)
        batt_bars++;
    while (batt_bars > 0 && batt_raw < AT[batt_bars - 1] - 4)
        batt_bars--;
    return batt_bars;
}
#ifdef APP_BATTERY_RAW
int32_t plat_battery_raw(void) { return batt_raw; }
#endif

#if FM1B_HEADPHONES
#include "plat_hp.c"                       /* the headphone jack, debounced (plat_headphones) */
#else
int plat_headphones(void) { return -1; }  /* (fm1.toml headphones = true reads it) */
#endif

/* LEDs: the picture is built off-line and copied one byte per column (Felucca: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker) */
static uint8_t led_pos[FM1_NKEY];
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < FM1_NKEY; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
#ifdef APP_DIM_LEVELS
static uint32_t dim_ns, dim_glow;                   /* the app's pulse (0: the glow's), the glow plat_lights last got */
static int dim_lvl = -1;                            /* the glow's strength the scan has */
void plat_dim_ns(uint32_t ns)                       /* any width of the dim pulse (fm1_led_dim_ns clamps it) */
{
    dim_ns = ns;
    if (ns)
        fm1_led_dim_ns(ns);
    else if (dim_glow > 0) {
        fm1_led_dim_level(dim_glow == 1 ? 1u : 0u); /* handed back: the glow's strength again */
        dim_lvl = (int)dim_glow;
    } else {
        dim_lvl = -1;                               /* (no glow now: the next one sets its strength) */
    }
}
#endif
/* the dim ones: felucca-bsp's glow, a short pulse on every scan frame, in two strengths (fm1_led_dim_level) */
void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys, int glow)
{
#ifndef APP_DIM_LEVELS
    static int level = -1;
#endif
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t i, c;
#ifdef APP_DIM_LEVELS
    dim_glow = (uint32_t)glow;
    if (glow > 0 && !dim_ns && glow != dim_lvl) {   /* (the app's own pulse stays until plat_dim_ns(0)) */
        fm1_led_dim_level(glow == 1 ? 1u : 0u);
        dim_lvl = glow;
    }
#else
    if (glow > 0 && glow != level) {
        fm1_led_dim_level(glow == 1 ? 1u : 0u);     /* 1: DIM LO, 2: DIM HI */
        level = glow;
    }
#endif
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        int on = i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u;
        int dim = glow > 0 && (i < NB ? (dim_buttons >> i) & 1u : (dim_keys >> (i - NB)) & 1u);
        uint8_t q = led_pos[id];
        if (q == 0xFF)
            continue;
        if (on)
            nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
        else if (dim)
            dl[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    /* PLAY's second, green LED: the keyless matrix position column 8, row PA5 (found on the device) */
    if (buttons & LED_PLAY_GREEN)
        nl[8] |= 1u << 1;
    else if (glow > 0 && (dim_buttons & LED_PLAY_GREEN))
        dl[8] |= 1u << 1;
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
    }
    {   /* the mirror's view: PLAY lit (either LED), and bit 15 PLAY lit red; the dim ones the same way */
        uint32_t play = 1u << B_PLAY, lit = buttons & ((1u << NB) - 1u);
        uint32_t db = glow > 0 ? dim_buttons : 0u, dk = glow > 0 ? dim_keys : 0u;
        mirror_leds((lit & ~play) | (buttons & (play | LED_PLAY_GREEN) ? play : 0u) | (buttons & play ? 1u << 15 : 0u),
                    keys, (db & ((1u << NB) - 1u) & ~play) | (db & (play | LED_PLAY_GREEN) ? play : 0u), dk);
    }
}
void plat_lights_mid(uint32_t buttons, uint32_t keys)   /* felucca-bsp's steady mid level (fm1_led_mid) */
{
    uint8_t ml[FM1_NCOL] = {0};
    uint32_t i, c;
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        uint8_t q = led_pos[id];
        if (q != 0xFF && (i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u))
            ml[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led_mid[c] = (uint8_t)(ml[c] & ~fm1_led[c]);
    mirror_mid(buttons & ((1u << NB) - 1u), keys);
}
static uint8_t glow_on = 1, play_red;
void plat_glow(int on) { glow_on = (uint8_t)(on != 0); }
void plat_play_red(int red) { play_red = (uint8_t)(red != 0); }
void plat_leds(uint32_t buttons, uint32_t keys)    /* PLAY green, unless red (recording); the unlit buttons glow */
{
    uint32_t play = 1u << B_PLAY, b = buttons, dim = glow_on ? ((1u << NB) - 1u) & ~buttons : 0u;
    if (!play_red) {
        b = (b & ~play) | (buttons & play ? LED_PLAY_GREEN : 0u);
        dim = (dim & ~play) | (dim & play ? LED_PLAY_GREEN : 0u);
    }
    plat_lights(b, keys, dim, 0, glow_on ? 2 : 0);
}

/* the 1 kHz tick (app_tick) runs in the timer interrupt: the main loop locks it out around what they share. The lock
 * nests and restores the interrupts as it found them (plat_lock.c); MIDI sent from the tick itself needs none (nothing
 * that sends interrupts it) */
static volatile uint8_t in_tick;
#include "plat_lock.c"
#ifdef APP_SYSEX
/* (built for an app that talks SysEx, APP_SYSEX: another's image stays as it was, byte for byte) */
int plat_sysex_send(const uint8_t *frame, uint32_t n) { return usb.config ? ota_wire_send(frame, n) : -1; }
uint32_t plat_sysex_room(void)
{
    return !usb.config || usb.suspended || sx_busy ? 0u : (SXQ - (so_w - so_r)) * 3u;   /* (3 bytes a USB-MIDI packet) */
}
uint32_t plat_sysex_queued(void) { return (so_w - so_r) * 3u; }
uint32_t plat_usb_session(void) { return usb.config ? usb.resets + 1u : 0u; }   /* (usb.c counts the bus resets) */
/* a stream: usb.c's SysEx ring held (sx_busy: ep1_tx sends no note, ann_wire_send waits) from begin to end, a USB-MIDI
 * packet into it each 3 bytes (CIN 4: more follow; 5..7: the last, with F7) */
static struct {
    uint8_t on, cut, k, acc[3];
} sxs;
int plat_sysex_begin(void)
{
    if (!usb.config || sx_busy)
        return -1;
    sx_busy = 1;
    sxs.on = 1, sxs.cut = 0, sxs.k = 0;
    return 0;
}
static int sxs_pkt(uint32_t cin)               /* the packet in sxs.acc into the ring, waiting while the computer reads */
{
    uint32_t t0 = fm1_ms, r = so_r;
    while (so_w - so_r >= SXQ) {
        if (so_r != r)
            t0 = fm1_ms, r = so_r;
        if (!usb.config || fm1_ms - t0 > PLAT_SYSEX_STALL_MS)
            return -1;
        ota_idle();
    }
    sx_out_q[so_w % SXQ] = cin | (uint32_t)sxs.acc[0] << 8 | (uint32_t)sxs.acc[1] << 16 | (uint32_t)sxs.acc[2] << 24;
    RING_PUBLISH();
    so_w++;
    sxs.k = 0;
    return 0;
}
int plat_sysex_put(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (!sxs.on || sxs.cut)
        return -1;
    for (i = 0; i < n; i++) {
        if (sxs.k == 3u && sxs_pkt(4u)) {       /* (a full packet goes when the next byte comes: the last carries F7) */
            sxs.cut = 1;
            return -1;
        }
        sxs.acc[sxs.k++] = p[i];
    }
    return 0;
}
int plat_sysex_end(void)
{
    int rc = -1;
    if (sxs.on && !sxs.cut && sxs.k) {
        if (sxs.k < 3u)
            sxs.acc[2] = 0;
        if (sxs.k < 2u)
            sxs.acc[1] = 0;
        rc = sxs_pkt(4u + sxs.k);
    }
    if (sxs.on)
        sx_busy = 0;
    sxs.on = 0, sxs.k = 0;
    return rc;
}
#else
int plat_sysex_send(const uint8_t *frame, uint32_t n) { return ota_wire_send(frame, n); }
#endif

/* the data area and its two more windows (plat.h): read through the unencrypted XIP window (main_fm1.c
 * fl_plain_window_init, from 0x93000 up); erased and written by felucca-bsp's RAM driver (FL_RANGE_OK: the store, its
 * FL_XDATA windows) */
_Static_assert(FL_XDATA0_LO == PLAT_XDATA0_LO && FL_XDATA0_HI == PLAT_XDATA0_HI && FL_XDATA1_HI == PLAT_XDATA1_HI &&
               FL_AUTO_LO == PLAT_XDATA1_LO && FL_AUTO_HI == FL_XDATA1_LO, "plat.h's windows are felucca-bsp's");
#if FM1B_XDATA2                                   /* the fourth window: felucca-bsp's FL_GLOB (in its FL_STORE_OK), the
                                                   * store's objects there refused (plat_xdata2.h) */
#include "plat_xdata2.h"
_Static_assert(FL_GLOB_LO == PLAT_XDATA2_LO && FL_GLOB_HI == PLAT_XDATA2_HI, "PLAT_XDATA2 is felucca-bsp's FL_GLOB");
#define DATA_STORE_HIT(off, n) plat_xdata2_store_hit((off), (n))
#else
#define DATA_STORE_HIT(off, n) 0
#endif
const uint8_t *plat_data(uint32_t off)
{
    return flash_ok && PLAT_DATA_OK(off, 1u) ? (const uint8_t *)FL_XIP(off) : 0;
}
int plat_data_erase(uint32_t off)
{
    uint32_t took;
    if (!flash_ok || (off & 0xFFFu) || !PLAT_DATA_OK(off, 0x1000u) || DATA_STORE_HIT(off, 0x1000u))
        return -1;
    fm1_wdt_feed();                               /* (a run of erases: tens of ms each) */
    audio_silence();
    return fl_erase4k(off, &took);
}
int plat_data_write(uint32_t off, const void *src, uint32_t n)   /* page programs only (fl_prog_ram, 0x02): bits
                                                                 * clear in place, nothing erased, nothing read back
                                                                 * (plat.h's promise: keep it so) */
{
    return flash_ok && PLAT_DATA_OK(off, n) && !DATA_STORE_HIT(off, n) ? fl_write(off, src, n) : -1;
}
/* a pull from the host in the update's framing (ota.c ota_read), and how long it may take */
int plat_host_read(uint32_t addr, uint8_t *dst, uint32_t n) { return n > 512u ? -1 : ota_read(addr, dst, n); }
void plat_host_patience(uint32_t try_ms, uint32_t total_ms)
{
    ota_try_ms = try_ms ? try_ms : 5000u;
    ota_deadline = total_ms ? ota_now_ms() + total_ms : 0u;
}
/* USB audio full: the render at full level for the USB audio input, MASTER after it (main_fm1.c render_block) */
static volatile uint8_t uac_full;
void plat_usb_audio_full(int on) { uac_full = (uint8_t)(on != 0); }
/* the computer's USB audio's level in the mix, 0..4096 (uac_mix.h; read by the render: one word, no lock) */
static volatile uint32_t usb_play_q12 = 4096u;
void plat_usb_play_gain(uint32_t q12) { usb_play_q12 = q12 > 4096u ? 4096u : q12; }
#if FM1B_UAC && FM1B_UAC_CH > 2
int16_t *plat_usb_ch(uint32_t off, uint32_t *run) { return uac_ch_frame(off, run); }
#else
int16_t *plat_usb_ch(uint32_t off, uint32_t *run) { (void)off; *run = 0; return 0; }
#endif
void plat_yield(void)
{
    ble_poll();                                   /* Bluetooth: its notifications go out while the screen draws */
#if FM1B_WIFI
    wifi_poll();                                  /* WiFi's tasks, once on */
#endif
#ifdef APP_YIELD
    app_yield();
#endif
}

/* MIDI: the rings carry 4-byte USB-MIDI event packets, byte 0 = cable / CIN. USB and the TRS jack share midi_in_q
 * (both from TIMER5); the jack's packets carry cable 1 (midi_uart.c), which the single-jack USB descriptor never
 * does. Bluetooth has its own queue (radio.c, main loop), and its out rings, one a link. */
/* each port's messages, in and out (plat_midi_count): for the app's MIDI lights and counters */
static uint32_t midi_n[NPORTS][2], midi_t[NPORTS][2];
static void midi_counted(int port, int out, uint32_t pkt)
{
    if ((pkt >> 8 & 0xFFu) >= 0xF8u)             /* clock, start, stop, active sensing: not counted, no light */
        return;
    midi_n[port][out]++;
    midi_t[port][out] = plat_ms() | 1u;          /* (0: never) */
}
uint32_t plat_midi_count(int port, int out) { return port >= 0 && port < NPORTS ? midi_n[port][!!out] : 0; }
uint32_t plat_midi_ago(int port, int out)
{
    return port < 0 || port >= NPORTS || !midi_t[port][!!out] ? 0xFFFFFFFFu : plat_ms() - midi_t[port][!!out];
}
uint32_t plat_midi_out_room(uint32_t to)
{
    uint32_t room = 0xFFFFFFFFu, r;
    if ((to & TO_USB) && (r = midi_out_room()) && r < room)
        room = r;
    if ((to & TO_BLE) && (r = ble_midi_out_room(to)) && r < room)
        room = r;
    return room == 0xFFFFFFFFu ? 0u : room;
}
uint32_t plat_midi_out_link(void) { return midi_out_link(); }
static uint32_t midi_in_from;                     /* plat_midi_in_from: the last one's link, as an out */
uint32_t plat_midi_in_from(void) { return midi_in_from; }
#if FM1B_MIDI_STAMP || FM1B_BLE_MIDI_TIME
static uint32_t midi_in_t_last;                   /* plat_midi_in_t24: the last one's landing time (FM1B_MIDI_STAMP) */
uint32_t plat_midi_in_t24(void) { return midi_in_t_last; }
#define MIDI_IN_T(t) (midi_in_t_last = (t))
#else
uint32_t plat_midi_in_t24(void) { return fm1_ticks(); }   /* (no stamps: now, called where plat_midi_in is) */
#define MIDI_IN_T(t) ((void)0)
#endif
int plat_midi_in(uint32_t *pkt, int *port)
{
    int from;
    if (mi_r != mi_w) {
        uint32_t p = midi_in_q[mi_r % MQ];
#if FM1B_MIDI_STAMP
        MIDI_IN_T(midi_in_t24[mi_r % MQ]);
#else
        MIDI_IN_T(fm1_ticks());
#endif
        RING_PUBLISH();
        mi_r++;
        from = (p >> 4 & 0xFu) == 1u ? PORT_TRS : PORT_USB;
        midi_in_from = from == PORT_USB ? TO_USB : 0u;
        *pkt = p & ~0xF0u;
    }
    else if (ble_midi_in(pkt, &midi_in_from)) {
        from = PORT_BLE;
#if FM1B_BLE_MIDI_TIME
        MIDI_IN_T(ble_in_t_last);                 /* the sender's own time (fm1.toml ble_midi_time) */
#else
        MIDI_IN_T(fm1_ticks());
#endif
    } else {
        return 0;
    }
    if (port)
        *port = from;
    midi_counted(from, 0, *pkt);
    return 1;
}
void plat_midi_send(uint32_t pkt, uint32_t to)    /* the main loop's and the tick's rings are shared */
{
    uint32_t f = 0;
    if (!in_tick)
        f = plat_lock();
    if (to & TO_USB) {
        midi_out_event(pkt);
        midi_counted(PORT_USB, 1, pkt);
    }
    if (to & TO_BLE) {
        ble_midi_tap(pkt, to);
        midi_counted(PORT_BLE, 1, pkt);
    }
    if (!in_tick)
        plat_unlock(f);
}
void plat_midi_out(uint32_t pkt) { plat_midi_send(pkt, TO_USB); }

#if FELUCCA_USB_HOST                              /* (felucca-bsp's UH_MSG_* are USBH_MSG_*, in order) */
void plat_usb_host(int on) { usb_host_switch((uint32_t)(on != 0)); }
int plat_usb_host_state(void) { return !usb_host_active() ? USBH_OFF : usb_host_connected() ? USBH_CONNECTED : USBH_LOOKING; }
int plat_usb_host_msg(void) { return (int)usb_host_message(); }
#else
void plat_usb_host(int on) { (void)on; }
int plat_usb_host_state(void) { return USBH_OFF; }
int plat_usb_host_msg(void) { return USBH_MSG_NONE; }
#endif
int plat_usb_lost(void)
{
    if (!midi_usb_lost)
        return 0;
    midi_usb_lost = 0;
    return 1;
}
#ifdef APP_USB_HOST_NAME                        /* (built for an app that shows it: another's image stays as it was) */
#if FELUCCA_USB_HOST
const char *plat_usb_host_name(void) { return usb_host_name(); }
#else
const char *plat_usb_host_name(void) { return ""; }
#endif
#endif

/* the keys' velocity and pressure (APP_KEY_SENSE): felucca-bsp's analog rows, as fm1-ctl and fm1-monk read them (the
 * converter's clock, a held key driven, the top row probed). Off, the converter is the main loop's (MASTER). */
void plat_sense(int on)
{
    if (!on) {
        fm1_an.con = 0;
        return;
    }
    if (!fm1_an.con) {
        int32_t a = fm1_adc_read(FM1_ADC_MASTER);  /* the pot's slot comes round in a frame: no dip meanwhile */
        if (a >= 0)
            fm1_an.master = (uint16_t)a;
    }
    fm1_an.drive = 1;
    fm1_an.probe = 1;
    fm1_an.con = (uint16_t)FM1_ADC_CON(7, 2);
}
uint32_t plat_analog_mask(void)
{
    uint32_t m = 0, c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 3; r <= 4u; r++)
            if (FM1_KEYMAP[r][c] >= 14)
                m |= 1u << (FM1_KEYMAP[r][c] - 14);
    return m;
}

#if FM1B_WIFI || FM1B_BLE
const uint8_t *plat_chip_uid(void) { return flash_ok ? flash_uid : 0; }
#else
const uint8_t *plat_chip_uid(void) { return 0; }
#endif
int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
int plat_store_head(uint32_t obj, void *dst, uint32_t n) { return flash_ok ? st_load_head(obj, dst, n) : -1; }
int plat_store_size(uint32_t obj) { return flash_ok ? st_size(obj) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? st_save(obj, src, len) : -9; }
#ifdef APP_STORE_POOL
int plat_store_room(void) { return flash_ok ? st_pool_room() : 0; }
#else
int plat_store_room(void) { return -1; }
#endif

static volatile uint32_t audio_cpu_pct, audio_xruns, audio_peak_us;
uint32_t plat_cpu_pct(void) { return audio_cpu_pct; }
uint32_t plat_audio_peak_us(void) { return audio_peak_us; }
void plat_audio_peak_reset(void) { audio_peak_us = 0; }
#ifdef APP_CPU_KHZ
uint32_t plat_cpu_khz(void) { return fm1_clock_is_stock() ? 360000u : 0u; }   /* (PLL1 24 / 12 * 270 MHz, x 2/3) */
#endif
uint32_t plat_xruns(void) { return audio_xruns; }
_Static_assert(PLAT_TICKS_PER_US == FM1_TICKS_PER_US, "plat_ticks24 is TIMER4's count");
uint32_t plat_ticks24(void) { return fm1_ticks(); }
void plat_code_window(const void *end) { fm1_guard_pc_extend((uint32_t)(uintptr_t)end); }
uint32_t plat_core(void) { return fm1_cnum(); }
/* the cache RAM (plat.h; felucca-bsp's fm1_cache_ram.h, set and tested in fm1_cstart): the ways less what .cache_ram
 * holds (device/app.ld), 0 without the test passed */
const struct plat_cache_ram *plat_cache_ram(void)
{
    /* (its own section: dropped with the function when no one calls it, as -ffunction-sections does for code) */
    static struct plat_cache_ram r __attribute__((section(".bss.plat_cache_ram")));
#if FM1B_CACHE_RAM
    extern uint8_t _cr_start[], _cr_end[];
    uint32_t all = fm1_cache_ram_bytes(), used = (uint32_t)(_cr_end - _cr_start);
    r.ways = fm1_cr.ways;
    r.status = fm1_cr.status;
    r.dway = fm1_cr.dway;
    r.iway = fm1_cr.iway;
    r.bad_at = fm1_cr.bad_at;
    r.bytes = all > used ? all - used : 0u;
    r.base = r.bytes ? _cr_end : 0;
#endif
    return &r;
}
