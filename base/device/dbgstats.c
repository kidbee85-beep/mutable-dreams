/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The debug stats: the platform's counters and the app's, a snapshot every 5 minutes of uptime while the app has them
 * on (plat_stats_enable; off at every start), and one with every crash, kept in flash beside the crash history and
 * read over USB-MIDI (F0 7D 4C 53 F7, below). For a unit that misbehaves only after hours: what its loads, its late
 * halves and its stacks did before it went wrong, across restarts and power-offs.
 *
 * Where: DS_LO, the update's staging area's last sector (0xE4000: ota.c stages a loader in 0xE0000..0xE4FFF, its
 * record at 0xE4F00, OTA_RES). Records in 0xE4000..0xE4EFF only: 0xE4F00..0xE4FFF stays erased (the record's place,
 * and the update loader's record scan, ldr_core.c: "AT" at 0xF06 of a 4 KiB boundary - 256). The crash history
 * (crashlog.c, 0xE2000..0xE3FFF) and backup.c's journal (0xE1000) are not touched. The staging area is the update's
 * first: no record is written while an update is staged (the area's first word, 0xE0000, or the loader's record,
 * 0xE4F00, not erased: ds_staged), and the update erases the area whole (ota_stage, ota_unstage, the boot after an
 * install: ota_boot_cleanup), so every install starts the stats again, as it does the crash history.
 *
 * A record is 64 bytes, 60 to the sector (5 hours at one per 5 minutes). It goes in at the first erased slot after the
 * newest; its magic last (the commit): a record cut short, or a slot an erase cut short left, reads as absent (magic,
 * then a CRC-32 over the rest). When the sector is full it is erased (sliced: the sound plays on) and the newest
 * DS_KEEP records go back first, from a queue in .noinit (ds_ni): a restart keeps the queue, so a cut in the erase or
 * in the write-back loses nothing a restart can bring back; a power cut there loses what the sector held (the queue
 * too: the rest of the history is lost, never a record made up). The queue holds the records not yet in the flash too.
 *
 * When: every DS_PERIOD_MS of uptime (the 5th minute, the 10th..., whatever the moment the switch went on) a snapshot
 * is queued; the main loop writes it at a quiet moment (the app's app_stats_quiet: transport stopped), or, still not
 * quiet DS_FORCE_MS later, as it is (flag DS_LOUD: one 64-byte page, two programs; a wrap's erase sliced as
 * plat_data_erase's): a snapshot is never lost to a long play. With every crash: the boot writes the snapshot of the
 * second before it (ds_ni.now, refreshed once a second, flag DS_CRASH), switch on or off, as the crash history keeps
 * every crash; a boot's save that a crash cut short is not tried again (busy), so a fault in this path cannot loop.
 *
 * Wear: an erase every DS_SLOTS - DS_KEEP = 57 records, 4.75 hours of the switch on (and one per install, the
 * update's own): the P25Q80H's 100,000 cycles last 54 years switched on day and night.
 *
 * The record (ds_rec_t, little-endian, 64 bytes; tools/fm1_remote.py parse_stats_frames reads its readout):
 *   0  magic  u32  DS_MAGIC, programmed last         4  seq     u32  1 up, the newest the highest
 *   8  build  u32  FM1B_BUILD                        12 up_s    u32  uptime, seconds
 *   16 halves u32  audio halves rendered (fm1b_dbg)  20 nested  u32  TIMER5 ticks inside a render
 *   24 late   u16  halves the DMA overran            26 max_us  u16  the longest render, us
 *   28 gap_us u16  the longest TIMER5 gap, us        30 lock_us u16  the longest the app held the interrupts, us
 *   32 pass_us u16 the longest main-loop pass, us    34 prog_us u16  the longest page program of these records, us
 *   36 stk_main u16, 38 stk_irq u16                  the main and interrupt stacks' least room since the start, bytes
 *   40 ever_main u16, 42 ever_irq u16, 44 ever_c1 u16  each stack's least room ever (plat_stack_least_ever: across
 *                  restarts and power-offs, the crashes' too; the second core's jobs' stack too), bytes
 *   46 t5_late u8  TIMER5 gaps over 300 us           47 boots   u8   starts since power-on
 *   48 crashes u8  fm1_crash.count
 *   49 flags  u8   DS_PERIODIC 1, DS_CRASH 2, DS_LOUD 4 (written while the app was not quiet), DS_ON 8 (the switch);
 *                  bits 4..7: the stage the longest pass ended in
 *   50 stage  u8   the main loop's step now (fm1b_dbg.stage), bits 4..7 the last watchdog reset's (wdt_stage)
 *   51 cpu2   u8   bit 0 it takes jobs, 1 barred, 2..4 why it gave up (plat_cpu2_gave_up), 5..7 failed boots
 *   52 radio  u8   bits 0..3 Bluetooth (plat_ble_state, bit 2 a central listening, bit 3 linked), 4..7 plat_wifi_state
 *   53 app    7 bytes, the app's (APP_STATS: app_stats; APP_STATS_LAYOUT names them)
 *   60 crc    u32  CRC-32 (zlib's) of bytes 4..59
 * Counters saturate (u8 255, u16 65535); a stack's 65535: not known (not measured, the second core off). The maxima
 * (max_us, gap_us, lock_us) are since the start or the log readout's clear (F0 7D 4C 47 F7 reads and clears them);
 * pass_us and prog_us are since the record before.
 *
 * The least room ever (ds_ni.ever): each stack's room read once a second (ds_stack: plat_stack_left), the least kept in
 * the queue's .noinit (a restart keeps it, a clear of the records empties it); at a power-on taken from the newest
 * record in the flash if this build wrote it (another build's stacks are not these), then lowered by
 * the crash's room (crash_keep's, the crash record's CL_STK); written into every record as it goes to the flash.
 *
 * Hooks from the platform (device/main_fm1.c, host/plat_host.c, tests/dbgstats_test.c):
 *   ds_read(off, dst, n)   ds_erase(off) (4 KiB)   ds_prog(off, src, n) (bits 1 to 0, within a 256-byte page)
 *   ds_send(frame, n)      one F0 .. F7 frame to the computer
 *   ds_staged()            1: an update is staged (no write, no clear)
 *   ds_fill(r)             r's counters and app block (up_s .. app; the rest is set here)
 *   ds_quiet()             1: a write now disturbs nothing (the app's transport stopped)
 *   ds_stack(k)            stack k's room now (plat_stack_left: PLAT_STK_MAIN, _IRQ, _C1), bytes, -1 not known
 *   DS_LAYOUT              the app block's fields, "name width ..." (APP_STATS_LAYOUT), or 0: printed raw
 *   DS_NOINIT              the queue's section attribute (.noinit on the device) */
#define DS_MAGIC 0x32535344u                        /* "DSS2" (DSS1's layout, without the stacks, reads as absent) */
#define DS_LO 0x000E4000u                           /* the staging area's 5th and last sector */
#define DS_REC 64u                                  /* bytes a record: four to a 256-byte page, never across one */
#define DS_SLOTS (0xF00u / DS_REC)                  /* 60: 0xF00..0xFFF erased (the update's record, its scan) */
#define DS_Q 4u                                     /* the .noinit queue: the newest records, in the flash or not yet */
#define DS_KEEP 3u                                  /* written back after a wrap's erase */
#define DS_APP 7u                                   /* the app's bytes in a record (plat.h PLAT_STATS_APP) */
#define DS_STK_NONE 0xFFFFu                         /* a stack's room not known */
#define DS_PERIOD_MS 300000u                        /* a snapshot every 5 minutes of uptime */
#define DS_FORCE_MS 240000u                         /* not quiet this long after: written anyway (before the next) */
#define DS_RETRY_MS 1000u                           /* a write that failed (staged, the flash): tried again this much later */
#define DS_PERIODIC 1u
#define DS_CRASH 2u
#define DS_LOUD 4u
#define DS_ON 8u
#define DS_NI_MAGIC 0x51535344u                     /* "DSSQ": the queue is this power-on's */
#if !defined PLAT_DATA_HI || !defined PLAT_XDATA1_LO
#error "dbgstats.c: plat.h first"
#endif
_Static_assert(DS_LO >= PLAT_DATA_HI && DS_LO + 0x1000u == PLAT_XDATA1_LO,
               "the debug stats: the update's staging's last sector (0xE4000), below the third data window");
_Static_assert(DS_LO + DS_SLOTS * DS_REC <= 0xE4F00u, "the debug stats: below the update's record (0xE4F00)");
_Static_assert(DS_LO >= 0xE4000u, "the debug stats: off the crash history (0xE2000..0xE3FFF) and backup.c's journal");
_Static_assert(DS_KEEP < DS_Q && DS_KEEP < DS_SLOTS, "the debug stats: the write-back fits the queue and the sector");

typedef struct {
    uint32_t magic, seq, build, up_s, halves, nested;
    uint16_t late, max_us, gap_us, lock_us, pass_us, prog_us;
    uint16_t stk_main, stk_irq, ever[3];            /* bytes; DS_STK_NONE not known (ever: main, irq, c1) */
    uint8_t t5_late, boots, crashes, flags, stage, cpu2, radio;
    uint8_t app[DS_APP];
    uint32_t crc;                                   /* CRC-32 of seq .. app */
} ds_rec_t;
_Static_assert(sizeof(ds_rec_t) == DS_REC && __builtin_offsetof(ds_rec_t, stk_main) == 36u &&
               __builtin_offsetof(ds_rec_t, t5_late) == 46u && __builtin_offsetof(ds_rec_t, app) == 53u &&
               __builtin_offsetof(ds_rec_t, crc) == 60u, "a debug stats record: 64 bytes, as documented above");

typedef struct {                                    /* the queue, kept across a restart (lost at a power-off) */
    uint32_t magic;                                 /* DS_NI_MAGIC */
    uint32_t crash_seen, busy;                      /* the crash count whose snapshot was saved; a boot's save under way */
    uint32_t nq;                                    /* q[0 .. nq - 1], the oldest first */
    ds_rec_t now;                                   /* the snapshot of the last second (a crash's: ds_boot) */
    ds_rec_t q[DS_Q];                               /* seq 0: not in the flash yet (its crc then over seq 0) */
    uint32_t qms[DS_Q];                             /* when each was queued (the uptime's ms) */
    int32_t ever[3];                                /* each stack's least room ever, bytes; -1 not known */
} ds_ni_t;
static ds_ni_t ds_ni DS_NOINIT;
static uint8_t ds_on;                               /* the switch (plat_stats_enable), off at every start */
static uint32_t ds_due, ds_now_ms;                  /* the next snapshot's uptime (ms, 0: none due); the last refresh */
static uint16_t ds_prog_us, ds_pass_us;             /* since the record before: the longest program, pass (us) */
static uint8_t ds_pass_stage;
static int ds_last_rc;                              /* the last write's: 0 kept, < 0 not (host's expect, the tests) */
static uint32_t ds_fail_ms;                         /* when it failed (ds_last_rc < 0) */

static int ds_read(uint32_t off, void *dst, uint32_t n);
static int ds_erase(uint32_t off);
static int ds_prog(uint32_t off, const void *src, uint32_t n);
static int ds_send(const uint8_t *f, uint32_t n);
static int ds_staged(void);
static void ds_fill(ds_rec_t *r);
static int ds_quiet(void);
static int32_t ds_stack(int k);
static void ds_ever_put(ds_rec_t *r);

static uint32_t ds_crc(const void *p, uint32_t n)  /* zlib's CRC-32, a bit at a time (no table: 56 bytes a second) */
{
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--)
        for (c ^= *b++, k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    return ~c;
}
static uint32_t ds_rcrc(const ds_rec_t *r) { return ds_crc(&r->seq, DS_REC - 8u); }
static uint32_t ds_at(uint32_t i) { return DS_LO + i * DS_REC; }
/* slot i: 1 a whole record (into *r), 0 erased, -1 neither (cut short, or garbage) */
static int ds_slot(uint32_t i, ds_rec_t *r)
{
    const uint32_t *w = (const uint32_t *)r;
    uint32_t k, all = 0xFFFFFFFFu;
    if (ds_read(ds_at(i), r, DS_REC))
        return -1;
    for (k = 0; k < DS_REC / 4u; k++)
        all &= w[k];
    if (all == 0xFFFFFFFFu)
        return 0;
    return r->magic == DS_MAGIC && r->crc == ds_rcrc(r) ? 1 : -1;
}
/* the sector as it is: the newest's seq (0 none) and the next slot to write (DS_SLOTS: full) */
static void ds_scan(uint32_t *seq, uint32_t *next)
{
    ds_rec_t r;
    uint32_t i;
    *seq = 0, *next = 0;
    for (i = 0; i < DS_SLOTS; i++)
        if (ds_slot(i, &r) == 1 && r.seq > *seq)
            *seq = r.seq, *next = i + 1u;
    while (*next < DS_SLOTS && ds_slot(*next, &r) != 0)
        (*next)++;                                  /* (a slot cut short after the newest, or garbage: past it) */
}
static int ds_put(uint32_t i, ds_rec_t *r)          /* r (seq and crc set) into erased slot i, magic last: 0 = kept */
{
    ds_rec_t chk;
    uint32_t at = ds_at(i);
    r->magic = 0xFFFFFFFFu;                         /* (left erased: programmed last) */
    if (ds_prog(at + 4u, &r->seq, DS_REC - 4u))
        return -2;
    r->magic = DS_MAGIC;
    if (ds_prog(at, &r->magic, 4u))
        return -3;
    return ds_slot(i, &chk) == 1 && !memcmp(&chk, r, DS_REC) ? 0 : -4;   /* (read back) */
}
/* queue entry k into the flash: its seq the next (past the flash's and the queue's), a full sector erased and the
 * queue's newest DS_KEEP in the flash written back first. 0 = kept */
static int ds_write(uint32_t k)
{
    ds_rec_t *r = &ds_ni.q[k];
    uint32_t seq, next, j, back[DS_Q], nb = 0;
    if (ds_staged())
        return -7;                                  /* (an update staged: the area is its; the record waits) */
    ds_scan(&seq, &next);
    for (j = 0; j < ds_ni.nq; j++)
        if (ds_ni.q[j].seq > seq)
            seq = ds_ni.q[j].seq;                   /* (the sector erased under us, an update's: the order goes on) */
    if (next >= DS_SLOTS) {                         /* full: erased, the newest kept ones back first */
        for (j = 0; j < ds_ni.nq; j++)
            if (j != k && ds_ni.q[j].seq)
                back[nb++] = j;                     /* (the queue's order is the seq's: oldest first) */
        if (ds_erase(DS_LO))
            return -1;
        for (j = nb > DS_KEEP ? nb - DS_KEEP : 0u, next = 0; j < nb; j++, next++)
            if (ds_put(next, &ds_ni.q[back[j]]))
                return -5;
    }
    r->seq = seq + 1u;
    ds_ever_put(r);                                 /* (the least ever as it is now: with every record) */
    r->crc = ds_rcrc(r);
    if (ds_put(next, r)) {
        r->seq = 0, r->crc = ds_rcrc(r);            /* (not kept: still to write) */
        return -6;
    }
    ds_prog_us = 0;                                 /* (the next record's maxima start again) */
    return 0;
}
static void ds_push(const ds_rec_t *r, uint32_t ms) /* a snapshot queued (seq 0: to write); the oldest goes when full */
{
    uint32_t j, drop = 0;
    if (ds_ni.nq == DS_Q) {
        for (j = 0; j < DS_Q && !ds_ni.q[j].seq; j++)
            ;
        drop = j < DS_Q ? j : 0u;                   /* (the oldest in the flash; none: the oldest of all, lost) */
        for (j = drop; j + 1u < DS_Q; j++)
            ds_ni.q[j] = ds_ni.q[j + 1u], ds_ni.qms[j] = ds_ni.qms[j + 1u];
        ds_ni.nq--;
    }
    ds_ni.q[ds_ni.nq] = *r;
    ds_ni.q[ds_ni.nq].seq = 0;
    ds_ni.q[ds_ni.nq].crc = ds_rcrc(&ds_ni.q[ds_ni.nq]);
    ds_ni.qms[ds_ni.nq++] = ms;
}
static int ds_pending(void)                         /* the oldest queued not in the flash yet: its index, -1 none */
{
    uint32_t j;
    for (j = 0; j < ds_ni.nq; j++)
        if (!ds_ni.q[j].seq)
            return (int)j;
    return -1;
}
static void ds_ever_note(uint32_t k, int32_t v)     /* stack k had v bytes left: the least ever lowered */
{
    if (v >= 0 && (ds_ni.ever[k] < 0 || v < ds_ni.ever[k]))
        ds_ni.ever[k] = v;
}
static uint16_t ds_stk16(int32_t v) { return v < 0 ? (uint16_t)DS_STK_NONE : (uint16_t)(v < 65534 ? v : 65534); }
static void ds_ever_put(ds_rec_t *r)                /* the least ever into r (as it goes to the flash) */
{
    uint32_t k;
    for (k = 0; k < 3u; k++)
        r->ever[k] = ds_stk16(ds_ni.ever[k]);
}
static void ds_snap(ds_rec_t *r, uint32_t flags)    /* a snapshot now (seq, magic, crc set when it is written) */
{
    memset(r, 0, sizeof *r);
    ds_fill(r);
    r->stk_main = ds_stk16(ds_stack(PLAT_STK_MAIN));
    r->stk_irq = ds_stk16(ds_stack(PLAT_STK_IRQ));
    ds_ever_put(r);
    r->build = FM1B_BUILD;
    r->pass_us = ds_pass_us, r->prog_us = ds_prog_us;
    r->flags = (uint8_t)(flags | (ds_on ? DS_ON : 0u) | (uint32_t)(ds_pass_stage & 15u) << 4);
}

/* ---------------------------------------------------------- the API --- */
/* the start: the queue as a restart left it, or empty after a power-on (each entry's CRC checked: none made up) */
static void ds_init(void)
{
    uint32_t j, n = 0;
    if (ds_ni.magic != DS_NI_MAGIC || ds_ni.nq > DS_Q) {
        memset(&ds_ni, 0, sizeof ds_ni);
        ds_ni.magic = DS_NI_MAGIC;
        ds_ni.ever[0] = ds_ni.ever[1] = ds_ni.ever[2] = -1;   /* (a power-on: ds_boot takes the flash's) */
    }
    for (j = 0; j < ds_ni.nq; j++)
        if (ds_ni.q[j].crc == ds_rcrc(&ds_ni.q[j])) {
            ds_ni.q[n] = ds_ni.q[j];
            ds_ni.qms[n++] = 0;                     /* (this start's uptime begins at 0: due at once if not quiet) */
        }
    ds_ni.nq = n;
    ds_on = 0, ds_due = 0, ds_now_ms = 0, ds_prog_us = ds_pass_us = 0, ds_pass_stage = 0, ds_last_rc = 0;
}
/* the boot, after ds_init (no sound yet: quiet): what the queue still holds into the flash, then, after a crash the
 * flash has no snapshot of (count: fm1_crash.count, 0 none), the second before it, flagged DS_CRASH. A save a crash
 * cut short is not tried again (busy), nor the crash it caused. 1 = a crash's snapshot kept, 0 = none new, < 0 not */
static void ds_ever_boot(const int32_t *crash_left)   /* the least ever: the newest record's, the crash's room */
{
    ds_rec_t r;
    uint32_t i, k, seq = 0;
    int32_t ev[3] = {-1, -1, -1};
    for (i = 0; i < DS_SLOTS; i++)
        if (ds_slot(i, &r) == 1 && r.seq > seq)
            for (seq = r.seq, k = 0; k < 3u; k++)       /* (the room of another build's stacks says nothing about this one's) */
                ev[k] = r.ever[k] == DS_STK_NONE || r.build != FM1B_BUILD ? -1 : (int32_t)r.ever[k];
    for (k = 0; k < 3u; k++) {
        ds_ever_note(k, ev[k]);                     /* (after a power-on the flash's; after a restart no higher) */
        if (crash_left)
            ds_ever_note(k, crash_left[k]);
    }
}
/* the least ever of stack k (plat_stack_least_ever): the kept one or the room now, the less; -1 not known */
static int32_t ds_least_ever(int k)
{
    int32_t now = ds_stack(k), e = k >= 0 && k < 3 ? ds_ni.ever[k] : -1;
    return now >= 0 && (e < 0 || now < e) ? now : e;
}
/* crash_left: the crash's stacks' room (stacks.c pst_crash, as the crash record keeps them), 0 none */
static int ds_boot(uint32_t count, const int32_t *crash_left)
{
    int k, rc = 0;
    ds_ever_boot(count && count != ds_ni.crash_seen ? crash_left : 0);
    if (ds_ni.busy) {
        ds_ni.busy = 0;
        ds_ni.crash_seen = count;
        return -9;
    }
    ds_ni.busy = 1;
    while ((k = ds_pending()) >= 0 && (rc = ds_write((uint32_t)k)) == 0)
        ;
    if (count && count != ds_ni.crash_seen) {
        ds_ni.crash_seen = count;
        if (ds_ni.now.magic == DS_MAGIC && ds_ni.now.crc == ds_rcrc(&ds_ni.now)) {   /* (a snapshot was taken) */
            ds_ni.now.flags = (uint8_t)((ds_ni.now.flags & ~(DS_PERIODIC | DS_LOUD)) | DS_CRASH);
            ds_ni.now.crashes = (uint8_t)(count < 255u ? count : 255u);
            ds_push(&ds_ni.now, 0);
            if (!rc)                                /* (the queue's went: the crash's is the only one left) */
                rc = ds_write((uint32_t)ds_pending());
            rc = rc ? rc : 1;
        }
    }
    ds_ni.busy = 0;
    ds_last_rc = rc;
    return rc;
}
static void ds_enable(int on)                       /* plat_stats_enable: the first snapshot at the next 5th minute */
{
    ds_on = (uint8_t)(on != 0);
    ds_due = 0;
}
/* a main-loop pass's end, ms the uptime, pass_us its length and stage where it ended: the second's snapshot refreshed,
 * a due one queued, and one queued written when quiet (or kept waiting too long) */
static void ds_poll(uint32_t ms, uint32_t pass_us, uint32_t stage)
{
    int k;
    if (pass_us > ds_pass_us)
        ds_pass_us = (uint16_t)(pass_us < 0xFFFFu ? pass_us : 0xFFFFu), ds_pass_stage = (uint8_t)stage;
    if (ms - ds_now_ms >= 1000u || !ds_now_ms) {
        uint32_t s;
        ds_now_ms = ms ? ms : 1u;
        for (s = 0; s < 3u; s++)
            ds_ever_note(s, ds_stack((int)s));      /* (the least ever, once a second) */
        ds_snap(&ds_ni.now, 0);
        ds_ni.now.magic = DS_MAGIC;                 /* (taken: the crash's snapshot, ds_boot) */
        ds_ni.now.crc = ds_rcrc(&ds_ni.now);
    }
    if (ds_on && !ds_due)
        ds_due = (ms / DS_PERIOD_MS + 1u) * DS_PERIOD_MS;
    if (ds_on && ms >= ds_due) {
        ds_rec_t r;
        ds_snap(&r, DS_PERIODIC);
        ds_push(&r, ms);
        ds_due += DS_PERIOD_MS;
        ds_pass_us = 0, ds_pass_stage = 0;
    }
    if ((k = ds_pending()) < 0 || (ds_last_rc < 0 && ms - ds_fail_ms < DS_RETRY_MS))
        return;                                     /* (nothing to write, or a failure a moment ago: no scan a pass) */
    if (!ds_quiet()) {
        if (ms - ds_ni.qms[k] < DS_FORCE_MS)
            return;                                 /* (a quiet moment may come yet) */
        ds_ni.q[k].flags |= DS_LOUD;
        ds_ni.q[k].crc = ds_rcrc(&ds_ni.q[k]);
    }
    ds_last_rc = ds_write((uint32_t)k);
    ds_fail_ms = ms;
}
/* the records' places, oldest first: their number (at[k]: the slot) */
static uint32_t ds_list(uint8_t at[DS_SLOTS])
{
    uint32_t seq[DS_SLOTS], n = 0, i, k;
    ds_rec_t r;
    for (i = 0; i < DS_SLOTS; i++) {
        if (ds_slot(i, &r) != 1)
            continue;
        for (k = n; k && seq[k - 1u] > r.seq; k--)  /* (in order of seq: insertion) */
            seq[k] = seq[k - 1u], at[k] = at[k - 1u];
        seq[k] = r.seq, at[k] = (uint8_t)i;
        n++;
    }
    return n;
}
static int ds_clear(void)                           /* the sector erased, the queue emptied: 0 = done */
{
    if (ds_staged() || ds_erase(DS_LO))
        return -1;
    ds_ni.nq = 0;
    ds_ni.ever[0] = ds_ni.ever[1] = ds_ni.ever[2] = -1;   /* (the least ever was the erased records': .noinit keeps it) */
    return 0;
}

/* ---------------------------------------------------------- readout --- */
static char *ds_hex(char *q, uint32_t v, uint32_t digits)
{
    while (digits--)
        *q++ = "0123456789ABCDEF"[(v >> (4u * digits)) & 15u];
    return q;
}
static char *ds_dec(char *q, uint32_t v)
{
    char t[10];
    int n = 0;
    do
        t[n++] = (char)('0' + v % 10u);
    while (v /= 10u);
    while (n)
        *q++ = t[--n];
    return q;
}
static char *ds_str(char *q, const char *s)
{
    while (*s)
        *q++ = *s++;
    return q;
}
static void ds_frame(uint8_t *f, char *end)        /* f: F0 7D 4C 53 then the text up to end; sent */
{
    f[0] = 0xF0, f[1] = 0x7D, f[2] = 0x4C, f[3] = 0x53;
    *end = (char)0xF7;
    ds_send(f, (uint32_t)((uint8_t *)end - f) + 1u);
}
/* a record's three lines, after "stat I/N", "stk I/N" and "app I/N":
 *   stat I/N seq DEC up DEC build HEX8 flags HEX2 boots DEC crashes DEC halves DEC nested DEC late DEC maxus DEC
 *        gap DEC t5late DEC lock DEC pass DEC pstage DEC prog DEC stage DEC wdt DEC cpu2 HEX2 bt HEX2 wifi DEC
 *   stk I/N main DEC irq DEC ever main DEC irq DEC c1 DEC   (bytes left: since the start, then the least ever; -1 not
 *        known)
 *   app I/N NAME DEC ... (DS_LAYOUT's fields, little-endian, in order) or app I/N raw HEX14 (no layout) */
static char *ds_stk(char *q, const char *lab, uint32_t v)   /* " lab N", N -1 when not known */
{
    q = ds_str(q, lab);
    return v == DS_STK_NONE ? ds_str(q, "-1") : ds_dec(q, v);
}
static char *ds_head(uint8_t *f, const char *h, uint32_t k, uint32_t n)   /* "h I/N" at f + 4: its end */
{
    char *q = ds_dec(ds_str((char *)f + 4, h), k + 1u);
    *q++ = '/';
    return ds_dec(q, n);
}
static __attribute__((noinline)) void ds_line(const ds_rec_t *r, uint32_t k, uint32_t n)
{
    static const char *const L[21] = {" seq ", " up ", " build ", " flags ", " boots ", " crashes ", " halves ",
                                      " nested ", " late ", " maxus ", " gap ", " t5late ", " lock ", " pass ",
                                      " pstage ", " prog ", " stage ", " wdt ", " cpu2 ", " bt ", " wifi "};
    const uint32_t v[21] = {r->seq, r->up_s, r->build, r->flags & 15u, r->boots, r->crashes, r->halves, r->nested,
                            r->late, r->max_us, r->gap_us, r->t5_late, r->lock_us, r->pass_us, (uint32_t)r->flags >> 4,
                            r->prog_us, r->stage & 15u, (uint32_t)r->stage >> 4, r->cpu2, r->radio & 15u,
                            (uint32_t)r->radio >> 4};
    uint8_t f[4u + 320u];                           /* (the stat line: 300 characters at most, then F7) */
    char *q = ds_head(f, "stat ", k, n);
    uint32_t j;
    for (j = 0; j < 21u; j++) {
        q = ds_str(q, L[j]);
        q = j == 2u ? ds_hex(q, v[j], 8u) : j == 3u || j == 18u || j == 19u ? ds_hex(q, v[j], 2u) : ds_dec(q, v[j]);
    }
    ds_frame(f, q);
    q = ds_stk(ds_stk(ds_head(f, "stk ", k, n), " main ", r->stk_main), " irq ", r->stk_irq);
    q = ds_stk(ds_stk(ds_stk(q, " ever main ", r->ever[0]), " irq ", r->ever[1]), " c1 ", r->ever[2]);
    ds_frame(f, q);
    q = ds_dec(ds_str((char *)f + 4, "app "), k + 1u);
    *q++ = '/';
    q = ds_dec(q, n);
    {
        const char *lay = DS_LAYOUT;
        uint32_t at = 0;
        if (!lay) {
            q = ds_str(q, " raw ");
            for (j = 0; j < DS_APP; j++)
                q = ds_hex(q, r->app[j], 2u);
        }
        while (lay && *lay) {                       /* "name width name width ...": width 1, 2 or 4 bytes */
            uint32_t w, x = 0;
            *q++ = ' ';
            while (*lay && *lay != ' ' && q < (char *)f + sizeof f - 16)
                *q++ = *lay++;
            while (*lay == ' ')
                lay++;
            w = (uint32_t)(*lay ? *lay++ - '0' : 1);
            while (*lay == ' ')
                lay++;
            if (w != 1u && w != 2u && w != 4u)
                w = 1u;
            for (j = 0; j < w && at + j < DS_APP; j++)
                x |= (uint32_t)r->app[at + j] << (8u * j);
            at += w;
            *q++ = ' ';
            q = ds_dec(q, x);
        }
    }
    ds_frame(f, q);
}
static uint32_t ds_print(void)                      /* every record, oldest first, three frames each; their number */
{
    uint8_t at[DS_SLOTS];
    uint32_t n = ds_list(at), k;
    ds_rec_t r;
    for (k = 0; k < n; k++)
        if (ds_slot(at[k], &r) == 1)
            ds_line(&r, k, n);
    return n;
}
/* the stats' own request (F0 7D 4C 53 F7, "LS"): the records, then "end N". F0 7D 4C 53 43 4C 52 F7 ("LSCLR"): the
 * sector erased and the queue emptied, then "cleared" (or "clear failed": an update staged, or the erase). 1 = the
 * frame was this */
static int ds_request(const uint8_t *p, uint32_t n)
{
    uint8_t f[4u + 24u];
    char *q;
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x53u)
        return 0;
    if (n == 6u && p[3] == 'C' && p[4] == 'L' && p[5] == 'R') {
        q = ds_str((char *)f + 4, ds_clear() ? "clear failed" : "cleared");
        ds_frame(f, q);
        return 1;
    }
    if (n != 3u)
        return 1;                                   /* (another of its form: not ours to guess at) */
    q = ds_dec(ds_str((char *)f + 4, "end "), ds_print());
    ds_frame(f, q);
    return 1;
}
