/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-ble and fm1-ctl) */
/* The radio (FM1B_BLE, FM1B_WIFI): the platform JieLi's libraries run on (the shim in ble/shim asks for interrupts,
 * a clock, delays, a log, a store), a radio's start (its log, the render held while it calibrates), and Bluetooth
 * MIDI (FM1B_BLE): JieLi's BT stack (btctrler.a, btstack.a, wl_rf_common.a: Apache-2.0, binary only), started at
 * every boot, as the stock firmware does, unless the last radio run ended in a crash or a watchdog reset (then by
 * hand: plat_ble_start). WiFi's own part: wifi.c. Part of the unity build (unity.c). A short log and a crash note
 * survive a reset (plat_ble_log_line). */
#if !FM1B_BLE_OWN
#include "shim.h"
#else
void shim_log(const char *s);                     /* (the own stack has no shim: these are radio.c's, for both) */
uint32_t shim_ms(void);
#endif

/* ---- the platform the shim asks for */
/* milliseconds since boot, from TIMER4 (independent of the timer-5 tick). TIMER4 wraps every 2^32 / 24 MHz = 179 s,
 * and the stack's timers and our timeouts must not see time jump back: its deltas are summed (called far more often
 * than every 179 s; fm1-radio's) */
uint32_t shim_irq_save(void);
void shim_irq_restore(uint32_t f);
uint32_t shim_ms(void)
{
    static uint32_t last, acc, ms;
    uint32_t f = shim_irq_save(), now = fm1_ticks(), r;     /* nests: called inside the shim's critical sections */
    acc += now - last;
    last = now;
    if (acc >= 1000u * FM1_TICKS_PER_US) {
        ms += acc / (1000u * FM1_TICKS_PER_US);
        acc %= 1000u * FM1_TICKS_PER_US;
    }
    r = ms;
    shim_irq_restore(f);
    return r;
}

/* the shim's critical sections (shim_irq_save / shim_irq_restore: JieLi's local_irq_disable and the shim's own
 * locks): they nest and restore the interrupts as they found them (shim_lock.c), so the radio's own interrupts can
 * take them */
#include "shim_lock.c"
#if SHIM_WIFI
/* the nesting is each task's own, as FreeRTOS keeps it: a task that waits inside a critical section must not leave
 * the interrupts off for the others (os.c switch_to; fm1-radio's) */
uint32_t shim_irq_depth(void) { return shim_depth; }
uint32_t shim_irq_hold(void)
{
    fm1_irq_off();
    return shim_depth;
}
void shim_irq_resume(uint32_t depth)
{
    shim_depth = depth;
    if (!depth)
        fm1_irq_on();
}
int shim_in_irq(void) { return fm1_sp_in_isr(); }   /* a wait asked for there is refused (os.c) */
#else
int shim_in_irq(void) { return 0; }
#endif
volatile uint32_t shim_isr_blocks;               /* waits refused in an interrupt (os.c; with SHIM_WIFI only) */
/* every task waits: run what is due (a timer may be what they wait for) and keep the watchdog fed,
 * whichever context we are in (btstack_init waits in the main loop's) */
static void radio_stuck_check(void);
static void (*radio_idle_hook)(void);             /* while every task waits: what must still be answered (USB) */
#if !FM1B_BLE_OWN
void shim_idle(void)
{
    static uint8_t in;
    fm1_wdt_feed();
    if (!in) {
        in = 1;
#if SHIM_WIFI
        shim_timers_post();                       /* timers to their owners: never a callback inside a task's wait */
#else
        shim_timers();
#endif
        radio_stuck_check();
        if (radio_idle_hook)
            radio_idle_hook();
        in = 0;
    }
}
#endif
void shim_reboot(void) { fm1_reboot(); }
/* the shim's delays (JieLi's delay_us and friends): from RAM, timed by TIMER4, nothing called */
__attribute__((section(".ram_text"), noinline, used)) void shim_delay_us(uint32_t us) { fm1_busy_us_inl(us); }


/* the radio's interrupts sit above the input timer (4): JieLi asks for 0..2, mapped to 5..7 */
static void hex8(char *b, uint32_t v);
void shim_request_irq(int index, int prio, void (*handler)(void))
{
    uint32_t f;
    {   /* on the log: which source, at what priority, which handler (it goes in the vector table as it is) */
        char b[32] = "irq ";
        b[4] = (char)('0' + index / 100 % 10);
        b[5] = (char)('0' + index / 10 % 10);
        b[6] = (char)('0' + index % 10);
        b[7] = ' ';
        b[8] = (char)('0' + (prio & 7));
        b[9] = ' ';
        hex8(b + 10, (uint32_t)(uintptr_t)handler);
        b[18] = 0;
        shim_log(b);
    }
    f = irq_save();
    fm1_guard_unlock_top();
    fm1_irq_attach((uint32_t)index, handler, (uint32_t)(prio + 5 > 7 ? 7 : prio + 5));
    fm1_guard_lock_top();
    irq_restore(f);
}

/* a short log for the screen, kept through a reset (.noinit): after a crash the app can show
 * what the last run said, and a crash note */
#define BLE_LOG_N 32
#define BLE_LOG_MAGIC 0x424C4532u                 /* "BLE2" */
static struct {
    uint32_t magic, w, crash_seen, running, repeat, t0_up;   /* running: a BT run was on (cleared at boot) */
    char line[BLE_LOG_N][40];
} blog __attribute__((section(".noinit")));
void shim_wdt(void) { fm1_wdt_feed(); }
#if FM1B_WIFI
static void wifi_log_watch(const char *s);
#endif
void shim_log(const char *s)
{
#if FM1B_WIFI
    wifi_log_watch(s);
#endif
    char *d = blog.line[blog.w % BLE_LOG_N];
    uint32_t i;
    if (blog.magic != BLE_LOG_MAGIC)
        return;
    if (blog.w) {                                 /* the same start as the last line: fold it, "xN" */
        char *p = blog.line[(blog.w - 1u) % BLE_LOG_N];
        for (i = 0; i < 5u && s[i] && s[i] == p[i]; i++)
            ;
        if (i == 5u || (!s[i] && !p[i])) {
            uint32_t n = ++blog.repeat + 1u, k = 0;
            char num[12];
            for (i = 0; i < 26u && s[i]; i++)
                p[i] = s[i] >= 32 && s[i] < 127 ? s[i] : ' ';
            p[i++] = ' ';
            p[i++] = 'x';
            do {
                num[k++] = (char)('0' + n % 10u);
                n /= 10u;
            } while (n && k < 10u);
            while (k)
                p[i++] = num[--k];
            p[i] = 0;
            return;
        }
    }
    blog.repeat = 0;
    for (i = 0; i < 39u && s[i]; i++)
        d[i] = s[i] >= 32 && s[i] < 127 ? s[i] : ' ';
    d[i] = 0;
    blog.w++;
}
static void hex8(char *b, uint32_t v)
{
    int i;
    for (i = 0; i < 8; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15u];
}
static char *put_dec(char *b, uint32_t v)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}

#include "fm1_rf_regs.h"
/* the radio's calibration as it stands (both stacks): the TX LO trims, RX DC, the RX filter codes and the VCO
 * bank, one log line each pair (docs/bt/RF-CALIBRATION.md §4.1: what a measurement of ours must match) */
static void radio_cal_log(void)
{
    uint32_t v[FM1_RF_CAL_NREGS], i;
    char b[40];
    fm1_rf_cal_regs(v);
    for (i = 0; i < FM1_RF_CAL_NREGS; i += 2u) {
        b[0] = 'r'; b[1] = 'f'; b[2] = (char)('1' + i / 2u);   /* (rf1..rf7: the log folds lines that start alike) */
        hex8(b + 3, FM1_RF_CAL_REGS[i] & 0xFFFFFu);
        b[3] = ' ';
        b[11] = ' ';
        hex8(b + 12, v[i]);
        b[20] = ' ';
        hex8(b + 21, v[i + 1]);
        b[29] = 0;
        shim_log(b);
    }
}

/* at boot: what ended the last run. The crash note: the fault's pc, vector and debug register, its
 * return address and how long Bluetooth had run, and the core's last four branches (ETM). */
static uint8_t radio_last_bad;                      /* the last BT run ended in a crash or a watchdog reset */
static void radio_boot_note(void)
{
    char b[40];
    int crashed = fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != blog.crash_seen;
    if (blog.magic != BLE_LOG_MAGIC || blog.w > 100000u) {
        if (!crashed)
            return;
        blog.magic = BLE_LOG_MAGIC;               /* a crash with Bluetooth never started: note it too */
        blog.w = blog.running = blog.t0_up = 0;
    }
    if (crashed) {
        char *q;
        blog.crash_seen = fm1_crash.count;
        q = put_dec(b + 6, fm1_crash.vec);
        b[0] = 'c'; b[1] = 'r'; b[2] = 'a'; b[3] = 's'; b[4] = 'h'; b[5] = ' ';
        q[0] = ' '; q[1] = 'p'; q[2] = 'c'; q[3] = ' ';
        hex8(q + 4, fm1_crash.pc);
        q[12] = ' '; q[13] = 'd'; q[14] = ' ';
        hex8(q + 15, fm1_crash.dbg);
        q[23] = 0;
        shim_log(b);
        b[0] = 'r'; b[1] = 'e'; b[2] = 't'; b[3] = ' ';
        hex8(b + 4, fm1_crash.rets);
        b[12] = ' '; b[13] = 'm'; b[14] = 's'; b[15] = ' ';
        put_dec(b + 16, blog.running ? fm1_crash.uptime_ms - blog.t0_up : 0u);
        shim_log(b);
        b[0] = 'e'; b[1] = ' ';
        hex8(b + 2, fm1_crash.etm[0]);
        b[10] = ' ';
        hex8(b + 11, fm1_crash.etm[1]);
        b[19] = ' ';
        hex8(b + 20, fm1_crash.etm[2]);
        b[28] = ' ';
        hex8(b + 29, fm1_crash.etm[3]);
        b[37] = 0;
        shim_log(b);
    } else if (blog.running && (fm1_boot.p3_rst & 4u)) {
#if FM1B_BLE_OWN
        const uint32_t shim_task_now = 0;         /* (no tasks: the main loop's) */
#else
        extern uint32_t shim_task_now;
#endif
        static const char *const T[4] = {"main", "1", "2", "3"};
        char *q = b;
        const char *t = "reset by the watchdog: task ";
        while (*t)
            *q++ = *t++;
        t = T[shim_task_now & 3u];
        while (*t)
            *q++ = *t++;
        *q = 0;
        shim_log(b);
    }
    radio_last_bad = (uint8_t)(blog.running && (crashed || (fm1_boot.p3_rst & 4u)));
    blog.running = 0;
}

/* an intentional restart (an update, the chip's update mode): the next boot does not take it for a crash */
static void radio_clean_exit(void) { blog.running = 0; }

/* the radio's address and pairing: the app's store object OBJ_BLE (app.h) */
#ifdef APP_BLE_STORE
#if !FM1B_BLE_OWN
#error "APP_BLE_STORE: with the own stack only (fm1.toml ble_stack = \"own\"): JieLi's keeps its records in OBJ_BLE"
#endif
#else
int shim_store_load(void *dst, uint32_t max) { return plat_store_load(OBJ_BLE, dst, max); }
int shim_store_save(const void *src, uint32_t len) { return plat_store_save(OBJ_BLE, src, len); }
#endif

#if !FM1B_BLE_OWN
extern int syscfg_read(uint16_t id, void *buf, uint16_t len);
extern int syscfg_write(uint16_t id, void *buf, uint16_t len);
/* the BT address: the one kept, or one made from the chip's unique data (the SDK's own fallback is
 * a random one, kept) */
#define CFG_BT_MAC_ADDR 1                         /* the shim's own syscfg record (ble/shim/misc.c; the SDK's id is 102,
                                                   * but only the shim reads it): kept 1, the address every FM-1 has */
const uint8_t *bt_get_mac_addr(void)
{
    static uint8_t mac[6];
    static const uint8_t none[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t i, h = 2166136261u;
    if (syscfg_read(CFG_BT_MAC_ADDR, mac, 6) == 6 && memcmp(mac, none, 6))
        return mac;
    h ^= fm1_ticks();
    for (i = 0; i < 6u; i++) {
        h = (h ^ (h >> 13)) * 16777619u + fm1_ticks();
        mac[i] = (uint8_t)(h >> 8);
    }
    mac[0] = (uint8_t)((mac[0] & 0xFC) | 0xC0);   /* a static random address */
    syscfg_write(CFG_BT_MAC_ADDR, mac, 6);
    return mac;
}

/* the name it advertises (le_midi.c): "FM-1 " and four hex digits of this FM-1's own, Melodee's way, so two FM-1s on
 * one computer are told apart. From the flash's unique ID: the same every start, update and firmware, and a wiped
 * store; no ID (a part that does not answer), from the BT address */
void ble_midi_name(char *name)
{
    static const char hex[] = "0123456789ABCDEF";
    const uint8_t *u = plat_chip_uid();
    uint32_t h = 2166136261u, i = 1;
    while (u && i < 16u && u[i] == u[0])          /* all FF or all 0: no ID */
        i++;
    if (u && i < 16u) {
        for (i = 0; i < 16u; i++)
            h = (h ^ u[i]) * 16777619u;
        h ^= h >> 16;
    } else {
        u = bt_get_mac_addr();
        h = (uint32_t)u[4] << 8 | u[5];
    }
    name[0] = 'F', name[1] = 'M', name[2] = '-', name[3] = '1', name[4] = ' ';
    for (i = 0; i < 4u; i++)
        name[5 + i] = hex[h >> (12u - 4u * i) & 15u];
    name[9] = 0;
}
#endif


/* ---- a radio's start (Bluetooth's, WiFi's): its log, the render held while it calibrates */
static uint32_t radio_t0;
static uint8_t radio_starting;                    /* started, not ready yet (radio_settle) */
/* BLE_MASK_AT_START: mask the input timer while the radio calibrates. fm1-ble needed it while the RF
 * trim ran from the flash (the real cause of its crashes, notes/06); with the trim in RAM it should
 * not be, and without it keys, USB and the screen do not pause when Bluetooth starts. */
#if SHIM_WIFI                                     /* WiFi: its calibration undisturbed by the input timer and the
                                                  * audio render (fm1-radio: a tester's FM-1 crashed in its TX
                                                  * calibration with an interrupt taken; masked, the first join took
                                                  * 15 s, not minutes). Keys, USB and the sound pause meanwhile */
#define BLE_MASK_AT_START 1
#define MASK_ALNK0_AT_START 1
static void audio_silence(void);
#endif
#ifndef BLE_MASK_AT_START
#define BLE_MASK_AT_START 0
#endif
static uint8_t irqs_masked;
/* radio_quiet: the render holds (app_quiet, or silence) for the first second of the start. The radio's trim
 * draws ~500 ms of high current, and what is read from the flash meanwhile can come back corrupted (notes/06
 * of fm1-ble): the trim itself runs from RAM, but CTL-1's GM synth read its code and 200 KB of samples from
 * the flash in the audio interrupt, ~30% of the time, and crashed there 0.2 s into the start. */
static volatile uint8_t radio_quiet;
#if SHIM_WIFI
extern int os_taskq_pend(const char *fmt, int *argv, int argc);
/* the SDK's "sys_timer" task: timers added to it (and the WiFi library's posts) arrive as [Q_CALLBACK, fn, args],
 * which os_taskq_pend runs */
static void sys_timer_task(void *p)
{
    int m[8];
    (void)p;
    for (;;)
        os_taskq_pend("taskq", m, 8);
}
#endif
static void radio_begin(const char *what)
{
    static uint8_t os_up;
    if (fm1_crash.magic != FM1_CRASH_MAGIC)
        fm1_crash.count = 0;
    if (blog.magic != BLE_LOG_MAGIC || blog.w > 100000u)
        blog.w = 0;                               /* (else the log goes on: the last run's crash note stays readable) */
    blog.magic = BLE_LOG_MAGIC;
    blog.crash_seen = fm1_crash.count;
    blog.running = 1;
    blog.t0_up = fm1_uptime_ms();                 /* (the crash note's clock) */
    radio_t0 = shim_ms();
    radio_starting = 1;
    shim_log(what);
    radio_quiet = 1;
#if BLE_MASK_AT_START
    {
        uint32_t f = irq_save();
        fm1_irq_mask(FM1_IRQ_TIMER5);
#ifdef MASK_ALNK0_AT_START
        audio_silence();                          /* (the DMA loops the buffer meanwhile) */
        fm1_irq_mask(FM1_IRQ_ALNK0);
#endif
        irq_restore(f);
        irqs_masked = 1;
    }
#endif
#if FM1B_BLE_OWN
    (void)os_up;
#else
    if (!os_up) {                                 /* the shim, once (both radios share it) */
        os_up = 1;
        shim_os_init();
        fm1_guard_stack_floor(shim_stacks_lo());  /* the tasks' stacks are in the pool */
#if SHIM_WIFI
        task_create(sys_timer_task, 0, "sys_timer");
#endif
    }
#endif
}
/* the main loop: the render back a second into the start once the radio is ready (or after 15 s), and the input
 * timer if it was masked */
static void radio_settle(int ready)
{
    uint32_t f;
    if (ready)
        radio_starting = 0;
    else if (shim_ms() - radio_t0 <= 15000u)
        return;
    if (radio_quiet && shim_ms() - radio_t0 > 1000u) {
        radio_quiet = 0;
        shim_log(ready ? "sound back: ready" : "sound back: 15 s, not ready");
    }
    if (!irqs_masked)
        return;
    irqs_masked = 0;
    f = irq_save();
    fm1_irq_unmask(FM1_IRQ_TIMER5);
#ifdef MASK_ALNK0_AT_START
    fm1_irq_unmask(FM1_IRQ_ALNK0);
#endif
    irq_restore(f);
    shim_log(ready ? "irqs back: ready" : "irqs back: 15 s, not ready");
}
#if !FM1B_BLE_OWN
extern void shim_os_dump(void);
static void radio_stuck_check(void)               /* still starting after 3 s: say what each task waits for */
{
    static uint32_t said;
    if (radio_starting && shim_ms() - radio_t0 > 3000u && said != radio_t0) {
        said = radio_t0;
        shim_log("still starting after 3 s:");
        shim_os_dump();
    }
}
#endif

#if FM1B_BLE
/* ---- Bluetooth MIDI */
static volatile uint8_t ble_state;

/* ---- MIDI both ways (ble/midi/le_midi.c); everything here runs in the main loop */
extern int ble_midi_send_many(int link, const uint32_t *pkts, const uint16_t *ms, int n);
extern int ble_midi_connected(int link);
static uint32_t ble_rx_count, ble_tx_count;
/* the links: 0 the computer's, 1 the device's (FM1B_BLE_CENTRAL); each with its out (plat.h TO_BLE_*) */
#define BLINKS (1 + FM1B_BLE_CENTRAL)
static const uint32_t BLE_TO[2] = {TO_BLE_HOST, TO_BLE_DEV};
static uint8_t ble_conn;                          /* bit l: ble_midi_connected(l), as of the last poll */
#define BQ 64u
static uint32_t ble_in_q[BQ];
static uint32_t bi_w, bi_r;
#if FM1B_BLE_MIDI_TIME
/* fm1.toml ble_midi_time: every message goes in at once with the time it was sent, on plat_ticks24's clock (the own
 * stack's ble_in_time, bt/ble/ble_time.c: the sender's own timestamps, the connection's bunching taken out), for
 * plat_midi_in_t24; the transport is not held to the sender's time (ble_midi_receive, ble_transport: unused) */
static uint32_t ble_in_t[BQ], ble_in_t_last;
void ble_midi_receive_at(uint32_t pkt, uint32_t t24)   /* from the stack (in ble_poll): for plat_midi_in */
{
    if (bi_w - bi_r < BQ) {
        ble_in_q[bi_w % BQ] = pkt & ~0xE0u;
        ble_in_t[bi_w % BQ] = t24;
        bi_w++;
        ble_rx_count++;
    }
}
#endif
/* The transport (clock, start, continue, stop) by the sender's time: the radio bunches messages into its
 * connection events (7.5..30 ms apart), so ticks would come in clumps and a sequencer stepping on them would clump
 * too (LOOM-8 on a Mac's clock: its sixteenths 60..180 ms apart at 140 BPM, against 104..112 over USB). Each BLE
 * MIDI message carries the millisecond it was sent (13 bits); the quickest one seen gives the two clocks' offset
 * (a message can only be late), and each is let out at its sent time plus the lateness the link has lately had
 * (at most BJ_MAX): spaced as sent, a constant delay later. The rest (notes, CCs ...) comes at once */
#define BTQ 32u                                  /* (120 ms of ticks at 300 BPM: 15) */
#define BJ_MAX 120u                              /* a DAW schedules its clock ahead (Logic, Live: ~100 ms) and macOS
                                                  * sends each tick at once with its future time: they come up to
                                                  * that much early; held to their times, the steps are as even as
                                                  * over USB, a constant ~0.1 s later (a DAW's clock delay setting) */
static struct { uint32_t pkt, due; } ble_tq[BTQ];
static volatile uint32_t bt_w, bt_r;
static uint32_t bj_base, bj_late, bj_t, bj_creep, bj_decay;
static uint8_t bj_have, bj_ts_seen;
uint32_t bj_ts_prev, bj_gap_min = 0xFFFFu, bj_gap_max, bj_gap_zero, bj_ticks, bj_rel_max;   /* (the log readout) */
uint32_t ble_jitter_late(void) { return bj_late; }
static void ble_transport(uint32_t pkt, uint32_t ts)
{
    uint32_t now = shim_ms(), diff = (now - ts) & 8191u, rel;
    if (!bj_have || now - bj_t > 2000u) {         /* the first, or after a pause: the offset afresh */
        bj_base = diff;
        bj_late = 0;
        bj_creep = bj_decay = now;
        bj_have = 1;
    }
    if ((pkt >> 8 & 0xFFu) == 0xF8u) {           /* (the log: the clock's sender gaps and lateness, since read) */
        uint32_t g = (ts - bj_ts_prev) & 8191u;
        if (bj_ts_seen && g < 1000u) {
            if (g < bj_gap_min) bj_gap_min = g;
            if (g > bj_gap_max) bj_gap_max = g;
            if (g == 0u) bj_gap_zero++;
        }
        bj_ts_prev = ts;
        bj_ts_seen = 1;
        bj_ticks++;
    }
    bj_t = now;
    if (now - bj_creep >= 1000u) {                /* (the two crystals drift: the offset may grow; a quicker one
                                                   * sets it back at once) */
        bj_base = (bj_base + 1u) & 8191u;
        bj_creep = now;
    }
    rel = (diff - bj_base) & 8191u;
    if (rel >= 4096u) {                           /* quicker than any yet: the offset */
        bj_base = diff;
        rel = 0;
    }
    if (rel > bj_rel_max)
        bj_rel_max = rel;
    if (rel > BJ_MAX)
        rel = BJ_MAX;
    if (rel > bj_late)
        bj_late = rel;
    else if (now - bj_decay >= 1000u && bj_late) {   /* (the lateness allowed shrinks again, a ms a second) */
        bj_late--;
        bj_decay = now;
    }
    if (bt_w - bt_r < BTQ) {
        ble_tq[bt_w % BTQ].pkt = pkt & ~0xE0u;
        ble_tq[bt_w % BTQ].due = now + (bj_late - rel);
        __asm__ volatile("" ::: "memory");
        bt_w++;
        ble_rx_count++;
    }
}
void ble_midi_receive(uint32_t pkt, uint32_t ts)  /* from the stack (in ble_poll): for plat_midi_in. The device's
                                                   * packets carry cable 1, which tells them from the computer's */
{
    uint32_t st = pkt >> 8 & 0xFFu;
    if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {
        ble_transport(pkt, ts);
        return;
    }
    if (bi_w - bi_r < BQ) {
        ble_in_q[bi_w % BQ] = pkt & ~0xE0u;
        bi_w++;
        ble_rx_count++;
    }
}
static int ble_midi_in(uint32_t *pkt, uint32_t *from)   /* from: its link's out, TO_BLE_HOST or TO_BLE_DEV */
{
    uint32_t p;
    if (bt_r != bt_w && (int32_t)(shim_ms() - ble_tq[bt_r % BTQ].due) >= 0) {   /* the transport, when due */
        p = ble_tq[bt_r % BTQ].pkt;
        bt_r++;
    } else if (bi_r == bi_w)
        return 0;
    else {
        p = ble_in_q[bi_r % BQ];
#if FM1B_BLE_MIDI_TIME
        ble_in_t_last = ble_in_t[bi_r % BQ];
#endif
        bi_r++;
    }
    *from = (p >> 4 & 0xFu) == 1u ? TO_BLE_DEV : TO_BLE_HOST;
    *pkt = p & ~0xF0u;
    return 1;
}
/* out: every message with the millisecond it was queued (fm1_ms), which goes with it as its BLE MIDI
 * timestamp. MIDI clock waits in its own short ring (a few ticks at most: later than that they are no
 * use) so a stream of clock never holds up notes; the drain merges the two in time order. Each link has its
 * own two, so a slow one holds up only itself. */
#define BCQ 4u
static struct ble_out {
    uint32_t q[BQ];
    uint16_t ms[BQ];
    uint32_t w, r;
    uint16_t clk_ms[BCQ];
    volatile uint32_t cw, cr;
} ble_out[BLINKS];
static uint32_t ble_midi_out_room(uint32_t to)   /* free slots in the out rings of to's links: the fewest among
                                                   * those listening; 0: none is */
{
    uint32_t room = 0xFFFFFFFFu;
    int l;
    for (l = 0; l < BLINKS; l++)
        if ((to & BLE_TO[l]) && (ble_conn >> l & 1u) && BQ - (ble_out[l].w - ble_out[l].r) < room)
            room = BQ - (ble_out[l].w - ble_out[l].r);
    return ble_state == BLE_READY && room != 0xFFFFFFFFu ? room : 0u;
}
static void ble_midi_tap(uint32_t pkt, uint32_t to)   /* main loop (locked) or the tick; nothing queues for a link
                                                       * not there */
{
    uint16_t ms = (uint16_t)(fm1_ms & 0x1FFFu);
    int l;
    if (ble_state != BLE_READY)
        return;
    for (l = 0; l < BLINKS; l++) {
        struct ble_out *o = &ble_out[l];
        if (!(to & BLE_TO[l]) || !(ble_conn >> l & 1u))
            continue;
        if (((pkt >> 8) & 0xFFu) == 0xF8u) {
            if (o->cw - o->cr < BCQ) {
                o->clk_ms[o->cw % BCQ] = ms;
                RING_PUBLISH();
                o->cw++;
            }
        } else if (o->w - o->r < BQ) {
            o->q[o->w % BQ] = pkt;
            o->ms[o->w % BQ] = ms;
            RING_PUBLISH();
            o->w++;
        }
    }
}
static int ms_before(uint16_t a, uint16_t b) { return ((uint32_t)(b - a) & 0x1FFFu) < 4096u && a != b; }
static void ble_midi_drain(int l)                 /* main loop: what waits for link l, as many notifications as the
                                                   * stack takes */
{
    struct ble_out *o = &ble_out[l];
    int round;
    for (round = 0; round < 4; round++) {
        uint32_t pkts[16], n = 0, ni = 0, ci, nw, cw, sent, k;
        uint16_t ms[16];
        uint8_t is_clk[16];
        uint32_t f = plat_lock();                 /* a snapshot: the tick may add meanwhile */
        nw = o->w;
        cw = o->cw;
        plat_unlock(f);
        ci = o->cr;
        ni = o->r;
        while (n < 16u && (ni != nw || ci != cw)) {
            int take_clk = ci != cw && (ni == nw || ms_before(o->clk_ms[ci % BCQ], o->ms[ni % BQ]));
            if (take_clk) {
                pkts[n] = 0xFu | 0xF8u << 8;
                ms[n] = o->clk_ms[ci % BCQ];
                is_clk[n++] = 1;
                ci++;
            } else {
                pkts[n] = o->q[ni % BQ];
                ms[n] = o->ms[ni % BQ];
                is_clk[n++] = 0;
                ni++;
            }
        }
        if (!n)
            return;
        sent = (uint32_t)ble_midi_send_many(l, pkts, ms, (int)n);
        if (!sent)
            return;                               /* the stack is full: what did not go stays for the next poll */
        for (k = 0; k < sent; k++) {
            if (is_clk[k])
                o->cr++;
            else
                o->r++;
        }
        RING_PUBLISH();
        ble_tx_count += sent;
    }
}

/* ---- the stack */
#if FM1B_BLE_OWN
#include "bt_own.c"                               /* Optimist's own (bt/): no JieLi library, no shim */
#else
static volatile uint32_t ble_events;
extern void btstack_init(void);
extern void bt_ble_init(void);
extern void lmp_set_sniff_disable(void);
extern void bt_max_pwr_set(uint8_t pwr, uint8_t pg_pwr, uint8_t iq_pwr, uint8_t ble_pwr);
#define BT_TX_POWER 8                             /* the SDK's default (classic, unused) */
#define BLE_TX_POWER 11                           /* the top: -64 dBm at 6 where the stock firmware gives -39 */
extern void lib_make_ble_address(uint8_t *ble, uint8_t *edr);
extern int le_controller_set_mac(void *addr);

struct bt_event_s { uint8_t event; uint8_t args[7]; uint32_t value; };
void shim_bt_event(int from, const void *event, int len)
{
    const struct bt_event_s *e = event;
    (void)len;
    ble_events++;
    if (from == 3 && e && e->event == 3 && ble_state == BLE_STARTING) {   /* BT_EVENT_FROM_CON, BT_STATUS_INIT_OK */
        {
            char b[40] = "BT ready: advertising as ";
            ble_midi_name(b + 25);
            shim_log(b);
        }
        {
            extern uint32_t shim_heap_peak;
            extern const uint32_t shim_heap_size;
            char b[32] = "heap peak ", *q = put_dec(b + 10, shim_heap_peak);
            q[0] = ' '; q[1] = 'o'; q[2] = 'f'; q[3] = ' ';
            put_dec(q + 4, shim_heap_size);
            shim_log(b);
        }
        bt_ble_init();
        radio_cal_log();
        ble_state = BLE_READY;
    }
}

static void ble_start(void)
{
    uint8_t addr[6];
    if (ble_state != BLE_OFF)
        return;
    ble_state = BLE_STARTING;
    radio_begin("BT: starting");
    lmp_set_sniff_disable();
    /* the radio's transmit power, 0..11: the SDK sets it in its config start-up (apps/common/config/
     * user_cfg.c, cfg_file_parse: classic 8, BLE 6), which the shim does not run; left alone the
     * library transmits at its lowest (measured: -79 dBm where the stock firmware gives -39) */
    bt_max_pwr_set(BT_TX_POWER, 6, 6, BLE_TX_POWER);
    lib_make_ble_address(addr, (uint8_t *)bt_get_mac_addr());
    le_controller_set_mac(addr);
    btstack_init();
}

/* the main loop's turn: timers, then every BT task that can run */
static void ble_poll(void)
{
    uint8_t conn = 0;
    int l;
    if (ble_state == BLE_OFF)
        return;
    shim_timers();
    shim_run();
    for (l = 0; l < BLINKS; l++)
        if (ble_state == BLE_READY && ble_midi_connected(l))
            conn |= (uint8_t)(1u << l);
    ble_conn = conn;                              /* (once: the tick reads it) */
    for (l = 0; l < BLINKS; l++) {
        if (!(conn >> l & 1u)) {
            ble_out[l].r = ble_out[l].w;          /* a link gone: nothing stale for the next one */
            ble_out[l].cr = ble_out[l].cw;
        }
        ble_midi_drain(l);
    }
#if FM1B_BLE_CENTRAL
    {
        extern void ble_midi_poll(void);
        ble_midi_poll();                          /* a connection that never answers, given up */
    }
#endif
    radio_stuck_check();
    radio_settle(ble_state == BLE_READY);
    {   /* ten seconds after it is ready: the tasks' deepest stack so far (sizing; 8 KiB each), then again each
         * time one goes deeper (a second at most between looks) */
        static uint8_t said, deep[4];
        static uint32_t looked, heap_said;
        extern int shim_stack_depth(int task);
        extern uint32_t shim_heap_peak;
        int k, more = 0;
        if (ble_state == BLE_READY && said && shim_ms() - looked > 1000u) {
            looked = shim_ms();
            for (k = 1; k <= 3; k++)
                if (shim_stack_depth(k) > deep[k])
                    more = 1;
            if (shim_heap_peak > heap_said)
                more = 1;
        }
        if (ble_state == BLE_READY && (more || (!said && shim_ms() - radio_t0 > 10000u))) {
            char b[56] = "stacks ", *q = b + 7;
            said = 1;
            looked = shim_ms();
            for (k = 1; k <= 3; k++)
                deep[k] = (uint8_t)shim_stack_depth(k);
            for (k = 1; k <= 3; k++) {
                q = put_dec(q, (uint32_t)deep[k] * 256u);
                *q++ = ' ';
            }
            heap_said = shim_heap_peak;               /* (and the heap's most ever: its sizing) */
            q[0] = 'h'; q[1] = 'e'; q[2] = 'a'; q[3] = 'p'; q[4] = ' ';
            put_dec(q + 5, heap_said);
            shim_log(b);
        }
    }
}

/* ---- plat.h */
extern int ble_midi_peripheral_connected(void);
extern int ble_midi_peripheral_linked(void);
extern void ble_midi_enable(int on);
static uint8_t ble_off;                           /* switched off by the player (plat_ble_enable) */
int plat_ble_state(void) { return ble_off ? BLE_OFF : ble_state; }
int plat_ble_connected(void) { return !ble_off && ble_state == BLE_READY && ble_midi_peripheral_connected(); }
int plat_ble_linked(void) { return !ble_off && ble_state == BLE_READY && ble_midi_peripheral_linked(); }
int plat_ble_stream(const struct plat_ble_stream *s) { (void)s; return 0; }   /* (send by plat_midi_send) */
void plat_ble_link_info(uint32_t v[7])
{
    int i;
    for (i = 0; i < 7; i++)
        v[i] = 0;
}
void plat_ble_enable(int on)
{
    ble_off = (uint8_t)!on;
    if (on && ble_state == BLE_OFF)
        ble_start();                              /* (off at power-on, or after a crash) */
    else if (ble_state == BLE_READY)
        ble_midi_enable(on);
    if (!on)
        shim_log("switched off");
}
#if FM1B_BLE_CENTRAL
extern void ble_midi_scan(int on);
extern int ble_midi_found(int i, char *name, int *rssi);
extern int ble_midi_connect(int i);
extern void ble_midi_disconnect(void);
extern int ble_midi_central_state(void);
void plat_ble_scan(int on)
{
    if (ble_state == BLE_READY)
        ble_midi_scan(on);
}
int plat_ble_found(int i, char *name, int *rssi) { return ble_state == BLE_READY && ble_midi_found(i, name, rssi); }
int plat_ble_connect(int i) { return ble_state == BLE_READY ? ble_midi_connect(i) : -1; }
void plat_ble_disconnect(void)
{
    if (ble_state == BLE_READY)
        ble_midi_disconnect();
}
int plat_ble_device(void) { return ble_state == BLE_READY ? ble_midi_central_state() : BLE_DEV_OFF; }
int plat_ble_passkey(void) { return -1; }
#include "ble_peers_none.c"
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
#endif                                            /* (FM1B_BLE_OWN) */
#else
#include "ble_none.c"
#endif

/* ---- plat.h: the radio's log */
void plat_log(const char *line) { shim_log(line); }
const char *plat_ble_log_line(int back)
{
    if (blog.magic != BLE_LOG_MAGIC || back < 0 || (uint32_t)back >= blog.w || back >= BLE_LOG_N)
        return "";
    return blog.line[(blog.w - 1u - (uint32_t)back) % BLE_LOG_N];
}
uint32_t plat_ble_log_total(void) { return blog.magic == BLE_LOG_MAGIC && blog.w <= 100000u ? blog.w : 0u; }
