/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Boot, audio ISR and main loop adapted from Felucca's main.c / audio.c, Copyright (C) 2026
 * Leo Kuroshita (@kurogedelic), Hugelton Instruments. */
/* The platform's boot and main loop (from X0X's, by way of FoMni-1): boot (WDT first, boot-loop guard, guards), LCD, input (TIMER5
 * 10 kHz), audio (ALNK0), USB; then the main loop: UI at ~60 frames/s, input polled in between. */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];
extern uint8_t _app_xip_end[];
#include "boot_lzma.h"

/* --------------------------------------------------------------- audio --- */
/* HALF_FRAMES (unity.c, from APP_BLOCK): the I2S half buffer, 256 frames = 5.8 ms at 44.1 kHz by default */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define FS 44100u
#define REND_FRAMES HALF_FRAMES             /* app_render's frames per call */
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

#define DBG_MAGIC 0x44424731u                       /* "DBG1": read with `fm1t memr` */
struct fm1b_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q12, boots, stage;
    uint32_t crash_seen;                            /* the crash count already reported at a boot */
#if FM1B_CRASHLOG
    uint32_t crash_kept, crash_saving;              /* the crash count already in the flash history; a boot's save of it
                                                     * under way (crashlog.c cl_boot: one cut short is not tried again) */
#endif
} fm1b_dbg __attribute__((section(".noinit")));
static uint8_t safe_mode;                           /* two failed boots: no audio, USB on (safe_main) */
int8_t clock_fix;                                   /* fm1_clock_stock() at the start: 0 stock already, 1 set, -1 failed */
static uint8_t power_boot[4];                       /* ANA_CON0, CON5, CON7, WLDO12_AUTO as the start found them */
static int8_t power_fix;                            /* fm1_power_stock(): 1 the DC-DC turned on, 0 it was on */
static uint32_t clock_boot[6];                      /* the clock's registers as the boot loader left them (fm1_clock_read) */
static uint8_t rails_boot[2], rails_tries;          /* VDC14's and SYSVDD's registers as the boot loader left them, and
                                                     * the raises it took to read back M-VAVE's levels (the log) */
static uint32_t wdt_stage __attribute__((section(".noinit")));   /* the main loop's step when the watchdog fired:
                                                                  * 2 Bluetooth, 3 the app's loop, 4 USB, 5 a frame,
                                                                  * 6 input, 7 the debug stats (fm1b_dbg.stage) */

#if FM1B_BT_AUDIO
static int own_bta_feed(const int32_t *o, uint32_t n);   /* bt_own_audio.c */
static int bta_frame(const uint8_t *p, uint32_t n);
#endif
#if FM1B_BLE_OWN && FM1B_BLE_CENTRAL
static int dev_frame(const uint8_t *p, uint32_t n);   /* bt_own_central.c */
#endif
#include "uac_mix.h"                                /* the USB recording, the computer's audio, in that order */
#include "fm1_flash_ok.h"                          /* the flash parts the storage may use (FM1B_FLASH_JEDEC_OK) */
#if FM1B_BT_AUDIO
static volatile uint8_t bta_streamed;               /* the last render went to Bluetooth (plat_bta_streams) */
int plat_bta_streams(void) { return bta_streamed; }
#else
int plat_bta_streams(void) { return 0; }
#endif
static void render_block(int32_t *o)                /* ALNK0: one half, measured */
{
#ifdef APP_LOCAL_MONITOR
    static unsigned monitor_level;
    const int monitor_muted = app_local_monitor_muted();
#endif
    uint32_t t0 = fm1_ticks(), us, budget = HALF_FRAMES * 1000000u / FS;
    fm1b_dbg.in_audio = 1;
#if FELUCCA_UAC
    if (!radio_quiet)                                 /* (USB audio's code is in the flash too: none while the radio
                                                     * calibrates, when a fetch can come back corrupted; a CTL-1 crashed
                                                     * so, returning to a garbled address, 0.6 s into each start) */
        uac_render_start();
#endif
    {
        uint32_t k, gain = APP_GAIN(master_pos);
#if FELUCCA_UAC
        const int full = uac_full != 0;             /* USB audio full: the render at full level for the USB audio
                                                     * input, MASTER for the speaker after it (plat_usb_audio_full) */
#else
        const int full = 0;
#endif
        for (k = 0; k < HALF_FRAMES; k += REND_FRAMES) {
            if (radio_quiet) {                        /* the radio calibrating: no flash reads (radio.c) */
#ifdef APP_QUIET
                app_quiet(o + 2u * k, REND_FRAMES);
#else
                uint32_t i;
                for (i = 0; i < 2u * REND_FRAMES; i++)
                    o[2u * k + i] = 0;
#endif
            } else {
                app_render(o + 2u * k, REND_FRAMES, full ? 4096u : gain);
            }
            if (radio_quiet)
                continue;
            uac_mix(o + 2u * k, REND_FRAMES, gain, master_pos, full, usb_play_q12);   /* the USB
                                                     * recording, then the computer's audio, then MASTER if full */
#if FM1B_BT_AUDIO
#ifdef APP_BT_FRAMES
            bta_streamed = (uint8_t)own_bta_feed(app_bt_frames(o + 2u * k, REND_FRAMES), REND_FRAMES);   /* (the app's
                                                     * frames for Bluetooth: 0 silence) */
#else
            bta_streamed = (uint8_t)own_bta_feed(o + 2u * k, REND_FRAMES);
#endif
            if (bta_streamed) {                     /* Bluetooth audio streams: the FM-1's own output muted */
                uint32_t i;
                for (i = 0; i < 2u * REND_FRAMES; i++)
                    o[2u * k + i] = 0;
            }
#endif
#ifdef APP_LOCAL_MONITOR
            local_monitor(o + 2u * k, REND_FRAMES, &monitor_level, monitor_muted);
#endif
        }
    }
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    if (us > audio_peak_us)
        audio_peak_us = us;
#ifdef APP_OVERLOAD
    if (us > budget * 9u / 10u)
        app_overload();                             /* over 90 % of its time: the app sheds what it can */
#endif
    /* cpu_q12 is the load smoothed over about 16 blocks, in 1/4096 of the budget. The +8 rounds the division, so
     * a steady load settles within half a percent of its true value from either side. us * 4096 wraps above about
     * 1 s in one block, which only an interrupt stuck that long reaches: the reading is then wrong until it decays. */
    fm1b_dbg.cpu_q12 = (fm1b_dbg.cpu_q12 * 15u + us * 4096u / budget + 8u) / 16u;
    audio_cpu_pct = (fm1b_dbg.cpu_q12 * 100u + 2048u) >> 12;   /* whole percent, rounded to nearest */
    fm1b_dbg.halves++;
    fm1b_dbg.last_us = us;
    if (us > fm1b_dbg.max_us)
        fm1b_dbg.max_us = us;
    fm1b_dbg.in_audio = 0;
}

#ifndef FM1B_AUDIO_ASIDE
#define FM1B_AUDIO_ASIDE 0                          /* fm1.toml audio_aside = true: 1 */
#endif
#if FM1B_AUDIO_ASIDE
#include "audio_commit.c"                           /* the half rendered aside, copied in while its half is free;
                                                     * FM1B_AUDIO_ASIDE 0 (the default): straight into the DMA's half */
#endif

void fm1_alnk0_irq(void)                            /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half();
#if FM1B_AUDIO_ASIDE
        uint32_t ok;
        fm1_audio_ack_half();                       /* (now: a new pending after the render = the DMA moved on) */
        render_block(aside);
        ok = audio_commit(half);
        if (fm1_audio_pending() & FM1_AUDIO_HALF)
            fm1_audio_ack_half();                   /* (the half missed: not rendered again) */
        if (!ok) {
#else
        render_block(&abuf[half * HALF_WORDS]);
        fm1_audio_ack_half();
        if (fm1_audio_free_half() != half) {
#endif
            fm1b_dbg.late++;                         /* the DMA moved on while we rendered */
            audio_xruns++;
        }
    }
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

static void audio_silence(void)                     /* IRQs off (flash erase): the DMA would loop stale audio */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}

/* ------------------------------------------------------------ timer --- */
static uint8_t usb_due;
static uint32_t t5_gap_max, t5_late, t5_run_max;                /* the longest gap between ticks (us), and gaps over 300 us:
                                                     * the key and light scan held up (the log reads and clears) */
void fm1_timer5_irq(void)
{
    static uint32_t sub, t5_prev;
    fm1_timer5_ack();
    {
        uint32_t now = fm1_ticks(), gap = (now - t5_prev) / FM1_TICKS_PER_US;
        if (t5_prev) {
            if (gap > t5_gap_max)
                t5_gap_max = gap;
            if (gap > 300u)
                t5_late++;
        }
#if FM1B_MIDI_STAMP
        /* what came in since the last tick came in, on the average, half way since: within 0.05 ms at 10 kHz (a tick
         * held up by a lock: half the gap). Every tick, the render's too: the poll below waits for the render, the
         * stamps do not. One SIE read and a look at the jack's ring */
        {
            uint32_t t = t5_prev ? now - (now - t5_prev) / 2u : now;
            usb_midi_peek(t);
#if FM1B_TRS
            uart_midi_peek(t);
#endif
        }
#endif
        t5_prev = now;
    }
    fm1b_dbg.timer_irqs++;
    if (fm1b_dbg.in_audio)
        fm1b_dbg.nested++;
    fm1_input_tick();
    /* 2 kHz: all USB SIE traffic lives here. This ISR now nests into the audio render (so the key
     * matrix keeps its rhythm, below); USB shares the MIDI queues with the audio ISR, so a poll that
     * falls inside the render waits for the first tick outside it, as it did before the nesting. */
    if (sub % 5u == 0u) {
        usb_due = 1;
#if FELUCCA_UAC && FELUCCA_UAC_CH == 2
        /* the USB audio stream cannot wait for the render (one packet per 1 ms frame, a render takes
         * up to ~11.6 ms): it runs nested too. It touches only EP4 (INDEX is set on every access) and
         * the consumer side of its ring, and usb_poll never runs nested, so the two never interleave */
        if (fm1b_dbg.in_audio)
            uac_service();
#endif
    }
#if FELUCCA_UAC && FELUCCA_UAC_CH > 2
    /* usb_audio_channels: every tick (0.1 ms), nested in the render or not (outside it, but for the ticks usb_poll
     * runs: it services the stream itself). A packet of 10 channels takes ~0.6 ms on the bus: polled at 2 kHz, the next
     * was often queued after the next frame's IN, which then found none (on an FM-1: 38,400 frames/s instead of
     * 44,100, the ring overflowing, halves dropped: crackles and a lower pitch). The stall count: FELUCCA_UAC_STALL_POLLS */
    if (fm1b_dbg.in_audio || !usb_due)
        uac_service();
#endif
    if (usb_due && !fm1b_dbg.in_audio) {
        usb_due = 0;
        usb_poll();
#if FM1B_TRS
        uart_midi_poll();                           /* the TRS jack: same context, same queue */
#endif
    }
#ifdef APP_TICK
    if (sub == 0u) {                                /* 1 kHz: the app's tick (a clock, a sequencer) */
        in_tick = 1;
        app_tick();
        in_tick = 0;
    }
#endif
    if (++sub == 10u)
        sub = 0;
    {
        uint32_t run = (fm1_ticks() - t5_prev) / FM1_TICKS_PER_US;
        if (run > t5_run_max)
            t5_run_max = run;
    }
    {
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}
extern void isr_timer5(void);

/* ------------------------------------------------------------- boot --- */
#define BOOTGUARD_MAGIC 0x42475244u
struct bootguard_s bootguard __attribute__((section(".noinit")));   /* declared in main.c (ota_commit) */

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

#if FM1B_DBGSTATS && !FM1B_CRASHLOG
#error "debug_stats: with the crash history (tools/build.py turns both on)"
#endif
#if FM1B_CRASHLOG
#include "stacks.c"                                 /* the stacks' room: painted at the start, read at a crash */

/* a sector of the crash history or the debug stats, main loop only: one window, the sound silenced, as plat_data_erase
 * (Henri's crashlog.c / dbgstats.c call his data_erase, felucca-bsp's sliced erase from w0ts/fm1-base
 * feat/sliced-erase, which is not taken here) */
static int data_erase(uint32_t off)
{
    uint32_t took;
    audio_silence();
    return fl_erase4k(off, &took);
}

/* the crash history in flash (crashlog.c: the staging area's 0xE2000..0xE3FFF), the boot's save and the readouts */
#ifndef FM1B_BUILD
#define FM1B_BUILD 0u                               /* (tools/build.py's build_id) */
#endif
#include "crashlog.c"
_Static_assert(CL_LO >= OTA_AREA + 0x2000u && CL_LO + CL_SECTORS * 0x1000u <= (OTA_RES & ~0xFFFu),
               "the crash history: in ota.c's staging, past the loader's head and backup.c's journal, before its record");
#if FM1B_BACKUP
_Static_assert(RS_REC + 0x1000u <= CL_LO, "the crash history: off backup.c's journal record");
#endif
static int cl_in(uint32_t off, uint32_t n) { return flash_ok && off >= CL_LO && n <= CL_SECTORS * 0x1000u - (off - CL_LO); }
static int cl_read(uint32_t off, void *dst, uint32_t n) { return cl_in(off, n) ? st_read(off, dst, n) : -1; }
static int cl_erase(uint32_t off)                   /* (the main loop's too: the clear, sliced as plat_data_erase's) */
{
    int rc;
    if (!cl_in(off, 0x1000u) || (off & 0xFFFu))
        return -1;
    fm1_wdt_feed();
    rc = data_erase(off);
    fm1_wdt_feed();
    return rc;
}
static int cl_prog(uint32_t off, const void *src, uint32_t n)
{
    return cl_in(off, n) ? fl_write(off, (const uint8_t *)src, n) : -1;   /* (src: a record on the stack, RAM) */
}
static int cl_send(const uint8_t *f, uint32_t n) { return ota_wire_send(f, n); }
static void crash_keep(void)                        /* the boot: a crash since the last one into the flash history */
{
    cl_rec_t r;
    const fm1_crash_ctx_t *x = &fm1_crash_ctx[fm1_crash.count % FM1_CRASH_CTX_N];
    uint32_t i;
    if (fm1_crash.magic != FM1_CRASH_MAGIC || !flash_ok)
        return;
    memset(&r, 0, sizeof r);
    r.count = fm1_crash.count, r.vec = fm1_crash.vec, r.pc = fm1_crash.pc, r.rets = fm1_crash.rets;
    r.sp = fm1_crash.sp, r.uptime_ms = fm1_crash.uptime_ms, r.emu = fm1_crash.emu, r.dbg = fm1_crash.dbg;
    r.psr = fm1_crash.psr, r.build = FM1B_BUILD;
    if (x->count == fm1_crash.count) {              /* (its registers, when the ring still holds them) */
        r.flags = CL_CTX;
        for (i = 0; i < 16u; i++)
            r.r[i] = x->r[i];
        r.usp = x->usp, r.ssp = x->ssp;
    }
    if (pst_crash.count == fm1_crash.count)         /* (the stacks' room fm1_fault read at this crash: stacks.c) */
        r.flags |= cl_stk_pack(pst_crash.left);
    (void)cl_boot(&r, &fm1b_dbg.crash_kept, &fm1b_dbg.crash_saving);
}
#else
int32_t plat_stack_left(int k) { (void)k; return -1; }   /* (fm1.toml crash_history = true measures them) */
#endif
#if FM1B_DBGSTATS

/* the debug stats in flash (dbgstats.c: the staging area's last sector, 0xE4000..0xE4EFF), the snapshots and the
 * readouts. Read through the plain window (plat_data's: no window with the interrupts off, the second core's flash
 * left alone), written by fl_write's page programs, each one's window measured (prog_us: the cost while playing) */
#define DS_NOINIT __attribute__((section(".noinit")))
#ifdef APP_STATS_LAYOUT
#define DS_LAYOUT APP_STATS_LAYOUT
#else
#define DS_LAYOUT 0
#endif
#include "dbgstats.c"
_Static_assert(DS_LO == (OTA_RES & ~0xFFFu) && DS_LO + DS_SLOTS * DS_REC <= OTA_RES && DS_LO >= CL_LO + CL_SECTORS * 0x1000u,
               "the debug stats: ota.c's staging's last sector, below its record, past the crash history");
static int ds_in(uint32_t off, uint32_t n) { return flash_ok && off >= DS_LO && n <= DS_SLOTS * DS_REC - (off - DS_LO); }
static int ds_read(uint32_t off, void *dst, uint32_t n)
{
    if (!ds_in(off, n))
        return -1;
    memcpy(dst, (const void *)FL_XIP(off), n);   /* (the plain window: fl_plain_window_init) */
    return 0;
}
static int ds_erase(uint32_t off)                   /* (sliced, as plat_data_erase's: the sound plays on) */
{
    int rc;
    if (!flash_ok || off != DS_LO)
        return -1;
    fm1_wdt_feed();
    rc = data_erase(off);
    fm1_wdt_feed();
    return rc;
}
static int ds_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t t0, us;
    int rc;
    if (!ds_in(off, n))
        return -1;
    t0 = fm1_ticks();
    rc = fl_write(off, (const uint8_t *)src, n);   /* (src: the .noinit queue, RAM; one page, one window) */
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    if (us > ds_prog_us)
        ds_prog_us = (uint16_t)(us < 0xFFFFu ? us : 0xFFFFu);
    return rc;
}
static int ds_send(const uint8_t *f, uint32_t n)   /* (120 frames, ~20 KB: half a second of the main loop) */
{
    fm1_wdt_feed();
    return ota_wire_send(f, n);
}
static int ds_staged(void)                          /* an update staged (or no flash): the area is not the stats' */
{
    uint32_t w[2];
    if (!flash_ok)
        return 1;
    memcpy(&w[0], (const void *)FL_XIP(OTA_AREA), 4u);   /* (the plain window, as ds_read) */
    memcpy(&w[1], (const void *)FL_XIP(OTA_RES), 4u);
    return (w[0] & w[1]) != 0xFFFFFFFFu;
}
static uint8_t ds_u8(uint32_t v) { return (uint8_t)(v < 255u ? v : 255u); }
static uint16_t ds_u16(uint32_t v) { return (uint16_t)(v < 65535u ? v : 65535u); }
static void ds_fill(ds_rec_t *r)
{
    extern uint32_t lock_max;
    uint32_t c2 = (bootguard.failed < 7u ? bootguard.failed : 7u) << 5;
#if FM1B_CPU2
    c2 |= (uint32_t)(plat_cpu2_on() != 0) | (uint32_t)(plat_cpu2_barred() != 0) << 1 |
          ((uint32_t)plat_cpu2_gave_up() & 7u) << 2;
#endif
    r->up_s = fm1_ms / 1000u, r->halves = fm1b_dbg.halves, r->nested = fm1b_dbg.nested;
    r->late = ds_u16(fm1b_dbg.late), r->max_us = ds_u16(fm1b_dbg.max_us);
    r->gap_us = ds_u16(t5_gap_max), r->t5_late = ds_u8(t5_late), r->lock_us = ds_u16(lock_max);
    r->boots = ds_u8(fm1b_dbg.boots);
    r->crashes = fm1_crash.magic == FM1_CRASH_MAGIC ? ds_u8(fm1_crash.count) : 0u;
    r->stage = (uint8_t)((fm1b_dbg.stage & 15u) | (wdt_stage & 15u) << 4);
    r->cpu2 = (uint8_t)c2;
#if FM1B_BLE
    r->radio = (uint8_t)(((uint32_t)plat_ble_state() & 3u) | (uint32_t)(plat_ble_connected() != 0) << 2 |
                         (uint32_t)(plat_ble_linked() != 0) << 3);
#endif
#if FM1B_WIFI
    r->radio |= (uint8_t)(((uint32_t)plat_wifi_state() & 15u) << 4);
#endif
#ifdef APP_STATS
    app_stats(r->app);
#endif
}
static int ds_quiet(void)
{
#ifdef APP_STATS_QUIET
    return app_stats_quiet();
#else
    return 1;
#endif
}
static int32_t ds_stack(int k) { return plat_stack_left(k); }
void plat_stats_enable(int on) { ds_enable(on); }
int plat_stats_on(void) { return ds_on; }
int32_t plat_stack_least_ever(int k) { return ds_least_ever(k); }
static void stats_keep(void)                        /* the boot: the queue's left, a crash's snapshot (dbgstats.c), the
                                                     * stacks' least ever (the flash's, the crash's room) */
{
    uint32_t count = fm1_crash.magic == FM1_CRASH_MAGIC ? fm1_crash.count : 0u;
    ds_init();
    if (flash_ok)
        (void)ds_boot(count, count && pst_crash.count == count ? pst_crash.left : 0);
}
#else
void plat_stats_enable(int on) { (void)on; }     /* (fm1.toml debug_stats = true keeps them) */
int plat_stats_on(void) { return 0; }
int32_t plat_stack_least_ever(int k) { (void)k; return -1; }
#endif
#if FM1B_CRASHLOG
#define CL_REQUEST(p, n) cl_request((p), (n))     /* F0 7D 4C 48 ..: the crash history's readout and clear */
#else
#define CL_REQUEST(p, n) 0
#endif
#if FM1B_DBGSTATS
#define DS_REQUEST(p, n) ds_request((p), (n))     /* F0 7D 4C 53 ..: the debug stats' */
#else
#define DS_REQUEST(p, n) 0
#endif
#if FM1B_BLE
static char *log_dec(char *b, uint32_t v)            /* v in decimal at b; the end */
{
    char t[10];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    return b;
}
#endif

/* a readout for development over USB-MIDI: F0 7D 4C 47 F7 ("LG") asks; the answer is one frame per line,
 * F0 7D 4C 47 <ASCII> F7: the last crash (count, vector, pc, rets, sp, uptime), the crash history kept in flash (its
 * records, the oldest first, in crashlog.c's form: it survives a power-off), the supplies' registers (rails), the
 * power setup's and the clock's as the boot loader left them, then
 * Bluetooth's log, the oldest first (it survives a reset), the cache RAM's test (FM1B_CACHE_RAM) and the app's own
 * lines (APP_LOG); "end" last.
 * The crash history alone, for a program: F0 7D 4C 48 F7 ("LH") asks; one frame a record, F0 7D 4C 48 <ASCII> F7,
 *   crash I/N n COUNT vec HEX pc HEX rets HEX sp HEX ms DEC emu HEX dbg HEX psr HEX build HEX seq DEC
 *   regs I/N r0 HEX .. r15 HEX usp HEX ssp HEX (when the record has the registers)
 *   stk I/N main DEC irq DEC c1 DEC guard 0|1 (each stack's bytes left at the crash, -1 not known; guard: EMU_MSG bit 3
 *     or DBG_MSG bit 13, a stack into its guard; crashlog.c)
 * the oldest first (I of N, at most 8), then "end N". F0 7D 4C 48 43 4C 52 F7 ("LHCLR") erases it and answers
 * "cleared" (or "clear failed"); a boot never clears it (an install does: the update's staging, crashlog.c).
 * The debug stats (dbgstats.c: every 5 minutes while the app has them on, and one with every crash; the record's
 * layout there): F0 7D 4C 53 F7 ("LS") asks; three frames a record, F0 7D 4C 53 <ASCII> F7, the oldest first (at most 60),
 *   stat I/N seq DEC up DEC build HEX flags HEX boots DEC crashes DEC halves DEC nested DEC late DEC maxus DEC
 *     gap DEC t5late DEC lock DEC pass DEC pstage DEC prog DEC stage DEC wdt DEC cpu2 HEX bt HEX wifi DEC
 *   stk I/N main DEC irq DEC ever main DEC irq DEC c1 DEC (bytes left since the start, then the least ever)
 *   app I/N NAME DEC ... (the app's fields, APP_STATS_LAYOUT) or app I/N raw HEX (14 digits)
 * then "end N". F0 7D 4C 53 43 4C 52 F7 ("LSCLR") erases them: "cleared", or "clear failed" (an update staged). Up and
 * the times in seconds and microseconds; flags 1 periodic, 2 a crash's, 4 written while playing, 8 the switch on.
 * Both answer in safe mode too */
static void log_line_send(const char *t)
{
    uint8_t f[48];
    uint32_t n = 0;
    f[n++] = 0xF0; f[n++] = 0x7D; f[n++] = 0x4C; f[n++] = 0x47;
    while (*t && n < sizeof f - 1u)
        f[n++] = (uint8_t)(*t++ & 0x7Fu);
    f[n++] = 0xF7;
    ota_wire_send(f, n);
}
static int log_frame(const uint8_t *p, uint32_t n)
{
    char b[48], *q;
    int k;
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x47u)
        return 0;
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        const uint32_t v[6] = {fm1_crash.count, fm1_crash.vec, fm1_crash.pc, fm1_crash.rets, fm1_crash.sp,
                               fm1_crash.uptime_ms};
        static const char *const L[6] = {"crash n ", " vec ", " pc ", " rets ", " sp ", " ms "};
        uint32_t i;
        q = b;
        for (i = 0; i < 3u; i++) {
            const char *l = L[i];
            while (*l)
                *q++ = *l++;
            hexs(q, v[i]);
            q += 8;
        }
        *q = 0;
        log_line_send(b);
        q = b;
        for (i = 3; i < 6u; i++) {
            const char *l = L[i] + 1;
            while (*l)
                *q++ = *l++;
            hexs(q, v[i]);
            q += 8;
            *q++ = ' ';
        }
        *q = 0;
        log_line_send(b);
        for (k = (int)FM1_CRASH_CTX_N - 1; k >= 0; k--) {   /* the registers at the last crashes, the oldest first */
            const fm1_crash_ctx_t *x = &fm1_crash_ctx[(fm1_crash.count - (uint32_t)k) % FM1_CRASH_CTX_N];
            uint32_t r, j;
            if (x->count != fm1_crash.count - (uint32_t)k || x->count == 0u)
                continue;                            /* (not one of them: an older run's, or never written) */
            q = b;
            for (j = 0; j < 8u; j++)
                *q++ = "regs n  "[j];
            hexs(q, x->count);
            q += 8;
            for (j = 0; j < 4u; j++)
                *q++ = " pc "[j];
            hexs(q, x->pc);
            q += 8;
            for (j = 0; j < 5u; j++)
                *q++ = " usp "[j];
            hexs(q, x->usp);
            log_line_send(b);
            for (r = 0; r < 16u; r += 4u) {
                q = b;
                *q++ = 'r';
                *q++ = (char)('0' + r / 10u);
                *q++ = (char)('0' + r % 10u);
                for (j = 0; j < 4u; j++) {
                    *q++ = ' ';
                    hexs(q, x->r[r + j]);
                    q += 8;
                }
                log_line_send(b);
            }
        }
    } else {
        log_line_send("no crash recorded");
    }
#if FM1B_CRASHLOG
    {   /* the crash history in flash (crashlog.c: it survives a power-off): its records, the oldest first, as the
         * history's own request (F0 7D 4C 48 F7) gives them */
        log_line_send("saved in flash, the oldest first:");
        if (!cl_print(0x47))
            log_line_send("saved none");
    }
#endif
    {   /* the supplies: as the boot loader left them, the raises, and now (raw registers: a broken read shows) */
        static const char H[] = "0123456789ABCDEF";
        uint8_t v[4] = {rails_boot[0], rails_boot[1], fm1_p33_read(FM1_P3_ANA_CON6), fm1_p33_read(FM1_P3_ANA_CON9)};
        char r[40], *q = r;
        const char *t = "rails boot ";
        uint32_t j;
        while (*t)
            *q++ = *t++;
        for (j = 0; j < 4u; j++) {
            if (j == 2u) {
                for (t = "now "; *t; )
                    *q++ = *t++;
            }
            *q++ = H[v[j] >> 4];
            *q++ = H[v[j] & 15u];
            *q++ = ' ';
        }
        *q++ = 'x';
        *q++ = (char)('0' + rails_tries);
        *q = 0;
        log_line_send(r);
    }
    {   /* the power setup as the start found it (fm1_power_stock): "power boot FB 44 A5 C0 fix 0" is M-VAVE's */
        static const char H[] = "0123456789ABCDEF";
        char r[40], *q = r;
        const char *t = "power boot ";
        uint32_t j;
        while (*t)
            *q++ = *t++;
        for (j = 0; j < 4u; j++) {
            *q++ = H[power_boot[j] >> 4];
            *q++ = H[power_boot[j] & 15u];
            *q++ = ' ';
        }
        for (t = "fix "; *t; )
            *q++ = *t++;
        *q++ = (char)('0' + power_fix);
        *q = 0;
        log_line_send(r);
    }
    {   /* the clock as the boot loader left it, and what the start did (fm1_clock.h): "sys 06" is M-VAVE's 360 MHz */
        char r[44];
        uint32_t j;
        const char *t;
        for (j = 0; j < 3u; j++) {
            char *q = r;
            t = j == 0u ? "clock boot sys " : j == 1u ? "clock pll1 " : "clock pll0 ";
            while (*t)
                *q++ = *t++;
            if (j == 0u) {
                hexs(q, clock_boot[0] << 24);
                q += 2;
                for (t = " mux "; *t; )
                    *q++ = *t++;
                hexs(q, clock_boot[1]);
                q += 8;
                for (t = " fix "; *t; )
                    *q++ = *t++;
                *q++ = clock_fix < 0 ? '-' : ' ';
                *q++ = (char)('0' + (clock_fix < 0 ? -clock_fix : clock_fix));
            } else {
                hexs(q, clock_boot[2u * j]);
                q += 8;
                *q++ = ' ';
                hexs(q, clock_boot[2u * j + 1u]);
                q += 8;
            }
            *q = 0;
            log_line_send(r);
        }
    }
    if (wdt_stage) {
#if FM1B_DBGSTATS
        static const char *const ST[8] = {"?", "?", "Bluetooth", "the app's loop", "USB", "a frame", "input",
                                          "the debug stats"};
#else
        static const char *const ST[7] = {"?", "?", "Bluetooth", "the app's loop", "USB", "a frame", "input"};
#endif
        char *q = b;
        const char *t = "last watchdog reset in ";
        while (*t)
            *q++ = *t++;
        t = ST[wdt_stage < sizeof ST / sizeof ST[0] ? wdt_stage : 0];
        while (*t)
            *q++ = *t++;
        *q = 0;
        log_line_send(b);
    }
    for (k = (int)plat_ble_log_total() - 1; k >= 0; k--) {
        if (!plat_ble_log_line(k)[0])
            continue;                                /* (beyond what the log keeps) */
        log_line_send(plat_ble_log_line(k));
    }
    {
        extern uint32_t lock_max, lock_from;
        static const char *const L[5] = {"gap ", " late ", " tick ", "lock ", " from "};
        const uint32_t v[5] = {t5_gap_max, t5_late, t5_run_max, lock_max, lock_from};
        uint32_t i;
        q = b;
        for (i = 0; i < 5u; i++) {
            const char *l = L[i];
            if (i == 3u) {                           /* two lines: a frame holds ~40 characters */
                *q = 0;
                log_line_send(b);
                q = b;
            }
            while (*l)
                *q++ = *l++;
            hexs(q, v[i]);
            q += 8;
        }
        *q = 0;
        log_line_send(b);
        t5_gap_max = t5_late = t5_run_max = lock_max = 0;
    }
#if FM1B_BLE
    {
        extern uint32_t shim_off_max, shim_off_from;
        const char *l = "BT irqs off max us ";
        q = b;
        while (*l)
            *q++ = *l++;
        hexs(q, shim_off_max);
        q += 8;
        for (l = " from "; *l;)
            *q++ = *l++;
        hexs(q, shim_off_from);
        log_line_send(b);
        shim_off_max = 0;
    }
#if FM1B_BLE_OWN
#if BLE_DIAG                                         /* (fm1.toml ble_diag = false: the counters not kept, no line) */
    {   /* the own stack's link layer: advertising events, connections taken / refused, packets good / CRC bad,
         * links closed : the last reason, the longest BLE interrupt (us) */
        q = b;
        for (const char *l = "BT ll "; *l;)
            *q++ = *l++;
        q = log_dec(q, ble_dg.adv_events);
        *q++ = ' ';
        q = log_dec(q, ble_dg.cind_ok);
        *q++ = '/';
        q = log_dec(q, ble_dg.cind_rej);
        *q++ = ' ';
        q = log_dec(q, ble_dg.rx_good);
        *q++ = '/';
        q = log_dec(q, ble_dg.rx_crc_bad);
        *q++ = ' ';
        q = log_dec(q, ble_dg.closes);
        *q++ = ':';
        q = log_dec(q, ble_dg.close_reason);
        *q++ = ' ';
        q = log_dec(q, ble_dg.isr_max_us);
        *q = 0;
        log_line_send(b);
    }
#endif
#else
    {   /* the radio's tasks' deepest stacks and its heap's most, now (the "stacks" lines may have scrolled away) */
        extern int shim_stack_depth(int task);
        extern uint32_t shim_heap_peak;
        extern const uint32_t shim_heap_size;
        int k;
        const char *l = "BT stacks ";
        q = b;
        while (*l)
            *q++ = *l++;
        for (k = 1; k <= 3; k++) {
            q = log_dec(q, (uint32_t)shim_stack_depth(k) * 256u);
            *q++ = ' ';
        }
        for (l = "heap "; *l;)
            *q++ = *l++;
        q = log_dec(q, shim_heap_peak);
        *q++ = '/';
        q = log_dec(q, shim_heap_size);
        *q = 0;
        log_line_send(b);
    }
#endif
    {   /* the clock by Bluetooth since the last read: ticks, the sender's gaps between them (ms), how many of
         * those 0 (a packet's one time for all), the most late one, and the lateness allowed now */
        extern uint32_t bj_gap_min, bj_gap_max, bj_gap_zero, bj_ticks, bj_rel_max;
        extern uint32_t ble_jitter_late(void);
        const char *l;
        q = b;
        for (l = "BT clk "; *l;)
            *q++ = *l++;
        q = log_dec(q, bj_ticks);
        for (l = " gap "; *l;)
            *q++ = *l++;
        q = log_dec(q, bj_ticks > 1u ? bj_gap_min : 0u);
        *q++ = '.';
        *q++ = '.';
        q = log_dec(q, bj_gap_max);
        for (l = " z "; *l;)
            *q++ = *l++;
        q = log_dec(q, bj_gap_zero);
        for (l = " late "; *l;)
            *q++ = *l++;
        q = log_dec(q, bj_rel_max);
        *q++ = '/';
        q = log_dec(q, ble_jitter_late());
        *q = 0;
        log_line_send(b);
        bj_gap_min = 0xFFFFu;
        bj_gap_max = bj_gap_zero = bj_ticks = bj_rel_max = 0;
    }
#endif
#if FM1B_CACHE_RAM
    {   /* the cache RAM (plat_cache_ram): "cache ram ways 2 st 1 free 00002000 at 01F28000" (st 1 ready, 2 the cache
         * never idled, 3 the masks not taken, 4 the test failed: put back, free 0), then the masks after (a set bit: a
         * way kept as a cache) and the first failing address */
        const struct plat_cache_ram *c = plat_cache_ram();
        const char *l;
        q = b;
        for (l = "cache ram ways "; *l;)
            *q++ = *l++;
        *q++ = (char)('0' + c->ways % 10u);
        for (l = " st "; *l;)
            *q++ = *l++;
        *q++ = (char)('0' + c->status % 10u);
        for (l = " free "; *l;)
            *q++ = *l++;
        hexs(q, c->bytes);
        q += 8;
        *q = 0;
        log_line_send(b);
        q = b;
        for (l = "cache at "; *l;)
            *q++ = *l++;
        hexs(q, (uint32_t)(uintptr_t)c->base);
        q += 8;
        for (l = " bad "; *l;)
            *q++ = *l++;
        hexs(q, c->bad_at);
        q += 8;
        *q = 0;
        log_line_send(b);
        q = b;
        for (l = "cache dway "; *l;)
            *q++ = *l++;
        hexs(q, c->dway);
        q += 8;
        for (l = " iway "; *l;)
            *q++ = *l++;
        hexs(q, c->iway);
        q += 8;
        *q = 0;
        log_line_send(b);
    }
#endif
#ifdef APP_LOG
    app_log(log_line_send);                          /* (the app's own lines) */
#endif
    log_line_send("end");
    return 1;
}

static uint32_t remote_lease_at;
static uint8_t remote_leased;

static void remote_release(void)
{
    remote_leased = 0;
    remote.btn = remote.keys = 0;
    remote.master = -1;
    for (unsigned i = 0; i < NE; i++)
        remote.enc[i] = 0;
}

/* remote control: F0 7D 4C 43 ("LC") op ... F7, 7-bit bytes:
 *   1 b0 b1          the buttons held (B_* bits: b0 the low seven, b1 the next seven)
 *   2 k0 k1 k2 k3    the keys held (bit k = key k, seven bits a byte, low first)
 *   3 role delta     an encoder turned (EN_*; delta - 64 detents)
 *   4 lo hi          MASTER's position (0..4096, seven bits a byte; 4097 or more: the pot's own again)
 *   5 on             browser session (1 starts/renews, 0 releases controls)
 * Each is added to the panel's own input (plat_fm1.c remote); the answer is F0 7D 4C 43 op F7. */
static int remote_frame(const uint8_t *p, uint32_t n)
{
    static uint8_t ack[6] = {0xF0, 0x7D, 0x4C, 0x43, 0, 0xF7};
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x43u)
        return 0;
    switch (p[3]) {
    case 5:
        if (n != 5u || p[4] > 1u)
            return 1;
        if (p[4])
            remote_leased = 1;
        else
            remote_release();
        break;
    case 1:
        if (n >= 6u)
            remote.btn = ((uint32_t)p[4] | (uint32_t)p[5] << 7) & ((1u << NB) - 1u);
        break;
    case 2:
        if (n >= 8u)
            remote.keys = ((uint32_t)p[4] | (uint32_t)p[5] << 7 | (uint32_t)p[6] << 14 | (uint32_t)p[7] << 21) &
                          ((1u << NKEYS) - 1u);
        break;
    case 3:
        if (n >= 6u && p[4] < NE)
            remote.enc[p[4]] += (int32_t)p[5] - 64;
        break;
    case 4:
        if (n >= 6u) {
            uint32_t v = (uint32_t)p[4] | (uint32_t)p[5] << 7;
            remote.master = v > 4096u ? -1 : (int32_t)v;
        }
        break;
    default:
        return 1;
    }
    if (remote_leased)
        remote_lease_at = fm1_ms;
    ack[4] = p[3];
    ota_wire_send(ack, sizeof ack);
    return 1;
}

#if FM1B_WIFI
/* WiFi over USB (passwords cannot be typed on the FM-1): F0 7D 4C 57 ("LW") op ... F7. Text goes as nibbles (each
 * byte two: high, low), the answers as plain 7-bit text, a line a frame (F0 7D 4C 57 10 <text> F7), then
 * F0 7D 4C 57 11 F7:
 *   1 on   2 off   3 scan
 *   4 status        "<state> <ssid> <ip> <rssi> <why>"
 *   5 the networks kept, one a line, the most recent first
 *   6 i             forget network i
 *   7 ssid 0 pass   join it with that password (nibbles; kept once joined); no password: a kept one's, or none
 *   8 the scan's networks, "<rssi> <secured> <ssid>" a line, the strongest first */
static void wifi_line(const char *t)
{
    uint8_t f[96];
    uint32_t n = 0;
    f[n++] = 0xF0; f[n++] = 0x7D; f[n++] = 0x4C; f[n++] = 0x57; f[n++] = 0x10;
    while (*t && n < sizeof f - 1u)
        f[n++] = (uint8_t)(*t++ & 0x7Fu);
    f[n++] = 0xF7;
    ota_wire_send(f, n);
}
static char *wifi_cat(char *q, const char *t, char *end)
{
    while (*t && q < end)
        *q++ = *t++;
    *q = 0;
    return q;
}
static char *wifi_num(char *q, int32_t v, char *end)
{
    char t[12];
    int k = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0 && q < end)
        *q++ = '-';
    do {
        t[k++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u);
    while (k && q < end)
        *q++ = t[--k];
    *q = 0;
    return q;
}
static int wifi_frame(const uint8_t *p, uint32_t n)
{
    static const char *const ST[] = {"off", "starting", "on", "joining", "address", "connected", "failed"};
    static const uint8_t end[6] = {0xF0, 0x7D, 0x4C, 0x57, 0x11, 0xF7};
    char b[88], *e = b + sizeof b - 1, *q;            /* (a long name: "Charles Vestal's iPhone" ...) */
    int i;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x57u)
        return 0;
    switch (p[3]) {
    case 1: plat_wifi_on(1); break;
    case 2: plat_wifi_on(0); break;
    case 3: plat_wifi_scan(); break;
    case 4: {
        uint32_t ip = plat_wifi_ip();
        int st = plat_wifi_state();
        q = wifi_cat(b, st >= 0 && st <= WIFI_FAILED ? ST[st] : "?", e);
        q = wifi_cat(q, " ", e);
        q = wifi_cat(q, plat_wifi_ssid()[0] ? plat_wifi_ssid() : "-", e);
        for (i = 0; i < 4; i++) {
            q = wifi_cat(q, i ? "." : " ", e);
            q = wifi_num(q, (int32_t)(ip >> (8 * i) & 255u), e);
        }
        q = wifi_cat(q, " ", e);
        q = wifi_num(q, plat_wifi_rssi(), e);
        {
            uint32_t fps;
            int rr;
            plat_wifi_rx(&fps, &rr);
            q = wifi_cat(q, " rx ", e);
            q = wifi_num(q, (int32_t)fps, e);
            q = wifi_cat(q, "/s ", e);
            q = wifi_num(q, rr, e);
        }
        q = wifi_cat(q, " ", e);
        wifi_cat(q, plat_wifi_why(), e);
        wifi_line(b);
        break;
    }
    case 5:
        for (i = 0; plat_wifi_saved(i, b); i++)
            wifi_line(b);
        break;
    case 6:
        if (n >= 5u)
            plat_wifi_forget(p[4]);
        break;
    case 7: {                                    /* ssid 0 pass, as nibbles */
        static char ssid[33], pass[64];
        char *d = ssid;
        uint32_t k, len = 0, max = sizeof ssid - 1u, have_pass = 0;
        for (k = 4; k + 1u < n; k += 2) {
            char c = (char)((p[k] & 15u) << 4 | (p[k + 1] & 15u));
            if (!c && d == ssid) {
                ssid[len] = 0;
                d = pass;
                len = 0;
                max = sizeof pass - 1u;
                have_pass = 1;
                continue;
            }
            if (len < max)
                d[len++] = c;
        }
        d[len] = 0;
        plat_wifi_join(ssid, have_pass ? pass : 0);
        break;
    }
    case 8:
        for (i = 0; ; i++) {
            char ssid[33];
            int rssi, sec;
            if (!plat_wifi_found(i, ssid, &rssi, &sec))
                break;
            q = wifi_num(b, rssi, e);
            q = wifi_cat(q, sec ? " 1 ch" : " 0 ch", e);   /* signal, secured, channel, name */
            q = wifi_num(q, plat_wifi_found_channel(i), e);
            q = wifi_cat(q, " ", e);
            wifi_cat(q, ssid, e);
            wifi_line(b);
        }
        break;
    default:
        break;
    }
    ota_wire_send(end, sizeof end);
    return 1;
}
#endif

/* a frame in the update's framing that ota_service did not take (d: its decoded length, the frame in ota_dec):
 * backup and restore (backup.c), the mirror (mirror.c), or the app's (APP_USB_CMD) */
static void usb_cmd(uint32_t d)
{
    if (d < 7u || ota_dec[2] == 0x11u)
        return;
#if FM1B_BACKUP
    if (d == 11u && ota_dec[2] == BU_REQ && bu_rd32(ota_dec + 6) == BU_MAGIC && flash_ok) {
        bu_start(app_backup_playing());             /* a backup to the host: streamed by bu_step, playing on */
        return;
    }
    if (d == 15u && ota_dec[2] == RS_REQ && flash_ok) {   /* a restore from the host */
        uint32_t len = ota_rd32(ota_dec + 6), crc = ota_rd32(ota_dec + 10);
        int begun = 0, rc;
        if (bu.on)
            bu_stop();                              /* (a backup streaming: given up) */
        ota_try_ms = 300u;                          /* its head first: a host that does not answer costs a second */
        ota_deadline = ota_now_ms() + 3000u;
        rc = restore_rx(len, crc, &begun);
        ota_try_ms = 5000u;
        ota_deadline = 0;
        if (begun)
            app_ui_init();                          /* the screen again (restored: everything reloaded) */
        app_say(rc == RS_E_PLAYING ? "STOP FIRST" : "RESTORE FROM USB",
                !rc ? "RESTORED" : rc == RS_E_PLAYING ? "then restore" : rc == RS_E_NO ? "NOTHING CHANGED" : "NOT RESTORED");
        return;
    }
#endif
#if FM1B_RES
    if (d == 15u && ota_dec[2] == RES_REQ && flash_ok) {   /* the resource region from the host (res.c) */
        uint32_t len = ota_rd32(ota_dec + 6), crc = ota_rd32(ota_dec + 10);
        int begun = 0, rc;
        if (bu.on)
            bu_stop();
        ota_try_ms = 2000u;
        ota_deadline = ota_now_ms() + 120000u;
        rc = res_rx(len, crc, &begun);
        ota_try_ms = 5000u;
        ota_deadline = 0;
        if (begun) {
            app_ui_init();
            app_say("RESOURCES", !rc ? "INSTALLED" : "NOT INSTALLED");
        }
        return;
    }
#endif
#if FM1B_MIRROR
    if (d == 8u && ota_dec[2] == MR_REQ) {
        mirror_cmd(ota_dec[6]);                     /* the screen mirrored: on, off, refresh */
        return;
    }
    if (d == 12u && ota_dec[2] == MR_REMOTE) {
        mirror_remote(ota_dec + 6);                 /* the host's hands while it mirrors */
        return;
    }
#endif
#ifdef APP_USB_CMD
    app_usb_cmd(ota_dec, d);
    ota_try_ms = 5000u;                             /* (the app's patience, plat_host_patience, back to the default) */
    ota_deadline = 0;
#endif
}

#include "sysex_dispatch.h"

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
#if FM1B_CRASHLOG
    pst_at_crash(c->count);                         /* the stacks' room before anything here draws on one (stacks.c) */
#endif
#ifdef APP_FAULT
    app_fault(c);                                   /* the app's first (its second core: given up, or this is it) */
#endif
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    sys_box(0, 8, 240, SYS_S, APP_NAME " CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    sys_box(10, 40, 220, SYS_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    sys_box(10, 60, 220, SYS_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    sys_box(10, 84, 220, SYS_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    sys_box(10, 102, 220, SYS_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    sys_box(10, 120, 220, SYS_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

#ifdef APP_RESTART                                  /* (built for an app that asks for it: another's image as it was) */
void plat_restart(void)
{
    app_panic();
    fm1_audio_stop();
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                          /* intentional reset: not a failed boot */
    radio_clean_exit();
    fm1_reboot();
}
#endif

static void enter_uboot(const char *why)
{
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sys_box(0, 110, 240, SYS_S, why, C_WHITE, 1);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                          /* intentional reset: not a failed boot */
    radio_clean_exit();
    fm1_enter_uboot();
}

/* SAFE MODE: the app crashed (or hung) twice while starting. Nothing that makes sound runs: no audio,
 * no engine, no UI; USB is on, so the web installer (this firmware's, or the stock one) and M-UPGRADE can
 * reach it. PLAY tries the app again, REC the chip's own update mode (as at power-on); it stays the last resort too
 * (fm1_cstart). */
static void safe_main(void)
{
    char b[24];
    uint32_t t0, play, rec;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sys_box(0, 18, 240, SYS_B, APP_NAME " SAFE MODE", C_HI, 1);
    sys_box(0, 52, 240, SYS_S, "IT CRASHED TWICE", C_WHITE, 1);
    sys_box(0, 72, 240, SYS_S, "WHILE STARTING.", C_WHITE, 1);
    sys_box(0, 102, 240, SYS_S, "NO SOUND. USB IS ON:", C_WHITE, 1);
    sys_box(0, 122, 240, SYS_S, "REINSTALL FROM THE", C_WHITE, 1);
    sys_box(0, 142, 240, SYS_S, "WEB INSTALLER.", C_WHITE, 1);
    sys_box(0, 172, 240, SYS_S, "PLAY: TRY AGAIN", C_HI, 1);
    sys_box(0, 190, 240, SYS_S, "REC: UPDATE MODE", C_HI, 1);
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        sys_box(0, 208, 240, SYS_S, b, C_GRAY, 1);
    }
    fm1_input_init();
    panel_init();
    usb_start();
    fm1_timer5_start(isr_timer5, 1);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    play = 1u << panel.btn[B_PLAY];
    rec = 1u << panel.btn[B_REC];
    t0 = fm1_ms;
    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (bootguard.pending && fm1_ms - t0 > 10000u)
            bootguard.pending = 0;                  /* safe mode itself is up: not another failed boot */
        if ((fm1_in.buttons & play) && fm1_ms - t0 > 500u) {
            bootguard.failed = 0;                   /* try the app again, from scratch */
            bootguard.pending = 0;
            sys_box(0, 172, 240, SYS_S, "STARTING " APP_NAME "...", C_HI, 1);
            usb_detach();
            fm1_delay_ms(30);
            fm1_reboot();
        }
        {   /* the log readout works here too: what crashed is the question safe mode raises */
            const uint8_t *p;
            uint32_t n;
            if (ota_frame_get(&p, &n)) {
                if (log_frame(p, n) || CL_REQUEST(p, n) || DS_REQUEST(p, n))
                    ota_frame_done();
                else
                    ota_service();                  /* the same frame the log didn't take */
            }
        }
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();                      /* an install commits and resets; else back here */
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
        if ((fm1_in.buttons & rec) && fm1_ms - t0 > 500u)
            enter_uboot("UPDATE MODE (REC)");
    }
}

static void splash(void)                           /* the app's (APP_SPLASH), or its name and tagline on paper */
{
#ifdef APP_SPLASH
    app_splash();
#else
    lcd_fill(0, 0, 240, 240, RGB(250, 244, 232));
    cv_begin(240, 64, RGB(250, 244, 232));
    {
        int32_t w = sys_w(SYS_BIG, APP_NAME);
        sys_text((240 - w) / 2, 0, SYS_BIG, APP_NAME, APP_SPLASH_RGB);
        w = sys_w(SYS_S, APP_TAGLINE);
        sys_text((240 - w) / 2, 42, SYS_S, APP_TAGLINE, RGB(150, 136, 118));
    }
    cv_blit(0, 92);
#endif
    lcd_sync();
}

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    uint32_t batt_t = 0;                            /* (the battery's last reading, fm1_ms) */
    uint32_t last_frame = 0;
    {
        uint32_t f = irq_save();
#if FM1B_CRASHLOG
        pst_boot(&knob);                            /* the stacks painted below what is live (stacks.c: the room a crash
                                                     * finds; the last run's marks were read at its fault, if any) */
#endif
        rails_boot[0] = fm1_p33_read(FM1_P3_ANA_CON6);
        rails_boot[1] = fm1_p33_read(FM1_P3_ANA_CON9);
        for (rails_tries = 1; ; rails_tries++) {
            fm1_vdc14_raise(7);                     /* M-VAVE's own supplies before anything loads them: 1.60 V, */
            fm1_sysvdd_raise(15);
            if ((fm1_vdc14() == 7u && fm1_sysvdd() == 15u) || rails_tries == 3u)
                break;                              /* (read back: a raise that did not take is tried again) */
        }
                                                    /* then 1.38 V (fm1_sys.h). At the boot loader's, flash fetches
                                                     * come back corrupted under load: the radio's trim crashed
                                                     * CTL-1 0.6 s into most Bluetooth starts */
        power_boot[0] = fm1_p33_read(FM1_P3_ANA_CON0);
        power_boot[1] = fm1_p33_read(FM1_P3_ANA_CON5);
        power_boot[2] = fm1_p33_read(FM1_P3_ANA_CON7);
        power_boot[3] = fm1_p33_read(FM1_P3_WLDO12_AUTO);
        power_fix = (int8_t)fm1_power_stock();      /* the rest of M-VAVE's power setup: the 1.3 V supply on its DC-DC,
                                                     * VDDIO 3.2 V, WiFi's regulator automatic (fm1_sys.h) */
        fm1_clock_read(clock_boot);
        clock_fix = (int8_t)fm1_clock_stock();      /* the stock 360 MHz, whichever bootloader started us (fm1_clock.h) */
        flash_ok = FM1B_FLASH_JEDEC_OK(FL_FAR(fl_jedec_ram)());   /* a known 1 MiB part, else RAM only */
#if FM1B_WIFI || FM1B_BLE
        if (flash_ok)
            FL_FAR(fl_uid_ram)(flash_uid);
#endif
        irq_restore(f);
        if (flash_ok) {
#if FM1B_BACKUP
            rs_boot();                              /* a restore's commit cut short by a power loss: finished */
#endif
#ifdef APP_STORE_POOL
            st_pool_boot();                         /* the A/B pairs from before the pool moved into it (storage.c) */
#endif
            ota_boot_cleanup();
            fl_plain_window_init();                 /* the data area read in place, unencrypted (plat_data) */
#if FM1B_RES
            res_check();                            /* the resource region: this build's? (res.c, plat_res) */
#endif
        }
    }
    lcd_init();
    palette_set(APP_PALETTE);
    splash();
    if (fm1b_dbg.magic != DBG_MAGIC) {
        memset(&fm1b_dbg, 0, sizeof fm1b_dbg);
        fm1b_dbg.magic = DBG_MAGIC;
    }
    fm1b_dbg.in_audio = 0;                          /* (.noinit: a crash inside the render left it set, and safe mode,
                                                     * with no render to clear it, then never polled USB) */
#if FM1B_CRASHLOG
    crash_keep();                                   /* a crash since the last boot into the flash history (crashlog.c):
                                                     * before safe mode, whose crashes at the start matter most; the
                                                     * staging cleaned up above first (ota_boot_cleanup erases it) */
#endif
#if FM1B_DBGSTATS
    stats_keep();                                   /* the debug stats' queue into the flash, a crash's snapshot
                                                     * (dbgstats.c): beside it, for the same reasons */
#endif
    if (safe_mode)
        safe_main();                                /* never returns */
    if (fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != fm1b_dbg.crash_seen) {
        char b[16];                                 /* a crash since the last boot: say so, a moment */
        fm1b_dbg.crash_seen = fm1_crash.count;
        b[0] = 'P';
        b[1] = 'C';
        b[2] = ' ';
        hexs(b + 3, fm1_crash.pc);
        sys_box(0, 176, 240, SYS_S, "RESTARTED AFTER A CRASH", RGB(255, 80, 60), 1);
        sys_box(0, 196, 240, SYS_S, b, C_GRAY, 1);
        fm1_wdt_feed();
        fm1_delay_ms(2500);
        fm1_wdt_feed();
    }
    fm1b_dbg.boots++;
    fm1b_dbg.max_us = 0;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    led_pos_init();
#if FM1B_CPU2
    cpu2_boot();
    cpu2_booting = 1;                               /* (plat_cpu2_set: the second core starts in app_init or never) */
#endif
    app_init();                                     /* the app's engine and its saved state (or defaults) */
#if FM1B_CPU2
    cpu2_booting = 0;
#endif
    audio_init();
    usb_start();
#if FM1B_TRS
    uart_midi_init();                               /* before TIMER5: it read-modify-writes port H too */
#endif
    /* above ALNK0 (3): the key / encoder / LED matrix is scanned one column per tick and must keep
     * its rhythm through a render (X0X's finding: below the audio, turns lost steps and LEDs flickered) */
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & 3u) == 3u)
        panel_setup();                              /* OCT- + OCT+ held at power-on */
    if (fm1_in.buttons & (1u << panel.btn[B_REC]))
        enter_uboot("UPDATE MODE (REC)");           /* REC held at power-on: the chip's update mode, the same key as
                                                     * m-bootloader's (which takes it first when it is installed): a way
                                                     * in when this firmware starts but its USB cannot be reached */
    fm1_delay_ms(400);
    radio_boot_note();                                /* what ended the last Bluetooth run, into its log */
    if (!radio_last_bad && APP_BLE_AT_BOOT)
        ble_start();                                /* Bluetooth is on, as on the stock firmware (unless the app's
                                                     * setting says off: APP_BLE_AT_BOOT) */
#ifdef APP_BOOT
    app_boot();                                     /* the app's own start (a radio), the platform up */
#endif
    app_ui_init();

    for (;;) {
#if FM1B_HEADPHONES
        plat_hp_read();                             /* the headphone jack, every pass (plat_headphones) */
#endif
#if FM1B_DBGSTATS
        {   /* the debug stats (dbgstats.c): the last pass's length and where it ended, the second's snapshot, a due
             * one queued, a queued one written when the app is quiet (or has kept it waiting 4 minutes) */
            static uint32_t t_ds;
            uint32_t now = fm1_ticks(), st = fm1b_dbg.stage;
            fm1b_dbg.stage = 7;
            ds_poll(fm1_ms, t_ds ? (now - t_ds) / FM1_TICKS_PER_US : 0u, st);
            t_ds = now;
        }
#endif
        fm1_wdt_feed();
#if FELUCCA_USB_HOST
        usb_host_task(fm1_ms);                      /* the host's mode change and connecting (plat_usb_host) */
#endif
        usb_retry(fm1_ms);
        if (remote_leased && (!usb.config || usb.suspended || fm1_ms - remote_lease_at >= 1800u))
            remote_release();
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {   /* MASTER: smoothed, 0..4092; with the keys' readings on, the scan owns the converter and reads it too */
            int32_t a = fm1_an.con || fm1_an.pend ? (int32_t)fm1_an.master : fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                knob += (a * 16 - knob) / 8;
                master_pos = mirror_in_master(remote.master >= 0 ? (uint32_t)remote.master : (uint32_t)(knob / 4));
            }
        }
#ifdef FM1_BATT_EVERY                               /* (felucca-bsp's scan reads the battery too) */
        if (fm1_ms - batt_t >= 100u) {              /* the battery, ten times a second (plat_battery); the scan's
                                                     * reading while it owns the converter (the keys' readings on) */
            int32_t b = fm1_an.con || fm1_an.pend ? (int32_t)fm1_an.batt : fm1_adc_read(FM1_ADC_BATT);
#else
        if (!fm1_an.con && !fm1_an.pend && fm1_ms - batt_t >= 100u) {   /* the battery, ten times a second (plat_battery) */
            int32_t b = fm1_adc_read(FM1_ADC_BATT);
#endif
            batt_t = fm1_ms;
            if (b > 0)
                batt_raw = batt_raw ? batt_raw + (b - batt_raw) / 16 : b;
        }
        fm1b_dbg.stage = 2;                         /* (breadcrumbs: where a watchdog reset found the loop) */
        ble_poll();                                 /* Bluetooth's tasks, once started */
#if FM1B_WIFI
        wifi_poll();                                /* WiFi's tasks, once on */
#endif
        fm1b_dbg.stage = 3;
#ifdef APP_LOOP
        app_loop();                                 /* the app's background (a radio's tasks) */
#endif
        fm1b_dbg.stage = 4;
        fm1_dispatch_sysex(fm1_ms);
#if FM1B_BACKUP
        bu_step();                                  /* a backup streaming to the host */
#endif
#if FM1B_MIRROR
        mirror_step();                              /* the screen mirrored: input reports, redraws, timeouts */
#endif
        if (usb.ota_req) {
            usb.ota_req = 0;
            app_panic();                            /* silence: the update rewrites the flash */
            if (flash_ok)
                ota_session();                      /* returns only if nothing was committed */
            app_ui_init();
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
        if (fm1_ms - last_frame >= 16u) {           /* ~60 frames/s at most; input in between */
            last_frame = fm1_ms;
            fm1b_dbg.ui_frames++;
            fm1b_dbg.stage = 5;
            app_frame();
        } else {
            fm1b_dbg.stage = 6;
            app_input_only();
        }
    }
}

/* one boot-packed load image at rec (tools/boot_pack.py: magic, packed bytes, raw bytes, 0, the stream) into start .. end;
 * the next record. The decoder's tables in the pool (cleared after, unused before). After Melodee's (main.c boot_image,
 * Kerem Kilic and Charles Vestal; with permission) */
static __attribute__((noinline)) const uint32_t *boot_image(const uint32_t *rec, uint32_t *start, uint32_t *end)
{
    uint32_t bytes = (uint32_t)((uintptr_t)end - (uintptr_t)start), packed;
    if ((uintptr_t)(rec + 4) > (uintptr_t)_app_xip_end || rec[0] != BOOT_LZMA_MAGIC || rec[2] != bytes || rec[3] != 0)
        fm1_enter_uboot();
    packed = rec[1];
    if (packed > (uintptr_t)_app_xip_end - (uintptr_t)(rec + 4) ||
        !boot_lzma((uint8_t *)start, bytes, (const uint8_t *)(rec + 4), packed, (uint16_t *)_pool_start))
        fm1_enter_uboot();
    return rec + 4 + (packed + 3u) / 4u;
}
_Static_assert(BOOT_LZMA_PROBS * 2u <= 65536u, "the boot decoder's tables fit the pool (its least, 64 KB)");

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
#if FM1B_CPU2
    fm1_cpu1_stop();                                /* a second core that a reset left running would read the RAM
                                                     * cleared below: held (registers only) */
#endif
#ifdef APP_CSTART
    app_cstart();                                   /* the app's, before the RAM is cleared (registers only: a second
                                                     * core a reset left running, held) */
#endif
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    if (p3 & 4u)
        wdt_stage = fm1b_dbg.stage;                 /* a watchdog reset: where the loop was (the log readout) */
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 4u) {                   /* safe mode failed too: the chip's own update mode */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    p3 |= (bootguard.failed >= 2u) << 8;            /* two failed boots: safe mode (kept past the .bss clear) */
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    if (_rt_load[0] == BOOT_LZMA_MAGIC) {
        /* the load images boot-packed (fm1.toml boot_pack, tools/boot_pack.py), .ram_text's then .data's, the decoder's
         * tables in the pool, cleared after; one that does not unpack exactly: update mode */
        const uint32_t *at = boot_image(_rt_load, _rt_start, _rt_end);
        (void)boot_image(at, _data_start, _data_end);
    } else {
        for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
            *d = *s;
        for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
            *d = *s;
    }
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
#if FM1B_CACHE_RAM
    fm1_cache_ram_init(FM1B_CACHE_RAM);             /* data-cache ways as RAM (felucca-bsp's fm1_cache_ram.h): its RAM
                                                     * code is in place now, the interrupts still off, the second core
                                                     * held; tested before anything uses it (plat_cache_ram) */
#endif
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    safe_mode = (uint8_t)(p3 >> 8);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
