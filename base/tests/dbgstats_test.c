/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* Host test of device/dbgstats.c (the debug stats in flash) against a simulated NOR: erase -> 0xFF, a program only
 * clears bits and stays within its page, and nothing at all outside 0xE4000..0xE4EFF (the crash history, the update's
 * record). The snapshots every 5 minutes, the switch, the ring's wrap (erase, the newest written back, the order kept),
 * a power cut at every program and inside the erase and the write-back (a record cut short reads as absent, nothing
 * made up; a restart brings the queue's back), a staged update refused (no write, no clear), the quiet moment and the
 * forced write while playing, the crash's snapshot at the boot (once, a save cut short not tried again), the readout's
 * frames and the clear. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/plat.h"

static uint32_t test_build = 0xABCD1234u;      /* (a variable: a test installs another build) */
#define FM1B_BUILD test_build
#define DS_NOINIT
#define DS_LAYOUT ds_layout
static const char *ds_layout = "c10 1 c60 1 drops 2 sram 4";
static uint8_t nor[0x100000];
static int prog_left = -1;                     /* power cut: programs still allowed (-1: no limit) */
static int erase_cut;                          /* 1: the next erase stops half way (half the sector erased) */
static uint32_t erases, progs;
static int staged, quiet = 1;
static uint32_t fill_n;                        /* ds_fill's calls: each snapshot's up_s and halves */
static char sent[200][400];                    /* the frames sent, as text (between F0 7D 4C 53 and F7) */
static int nsent;

static int ds_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int ds_erase(uint32_t off)
{
    if (off != 0xE4000u) {
        printf("an erase off the stats' sector: %#x\n", off);
        exit(1);
    }
    erases++;
    memset(nor + off, 0xFF, erase_cut ? 2048u : 4096u);
    if (erase_cut) {
        erase_cut = 0;
        return -5;
    }
    return 0;
}
static int ds_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    if (off < 0xE4000u || off + n > 0xE4F00u) {
        printf("a program outside 0xE4000..0xE4EFF: %#x + %u\n", off, n);
        exit(1);
    }
    if (staged) {
        printf("a program while an update is staged: %#x\n", off);
        exit(1);
    }
    if (prog_left == 0)
        return -9;
    if (prog_left > 0)
        prog_left--;
    progs++;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
static int ds_send(const uint8_t *f, uint32_t n)
{
    if (n < 5u || f[0] != 0xF0 || f[1] != 0x7D || f[2] != 0x4C || f[3] != 0x53 || f[n - 1u] != 0xF7 || nsent == 200 ||
        n - 5u >= 400u) {
        printf("a bad frame (%u bytes)\n", n);
        exit(1);
    }
    for (uint32_t i = 4; i + 1u < n; i++)
        if (f[i] & 0x80u) {
            printf("a frame byte over 7 bits\n");
            exit(1);
        }
    memcpy(sent[nsent], f + 4, n - 5u);
    sent[nsent][n - 5u] = 0;
    nsent++;
    return 0;
}
static int ds_staged(void) { return staged; }
static int ds_quiet(void) { return quiet; }
static uint32_t now_ms;
#include "../device/dbgstats.c"
static void ds_fill(ds_rec_t *r)               /* the platform's counters and the app's block, as main_fm1.c fills them */
{
    fill_n++;
    r->up_s = now_ms / 1000u, r->halves = now_ms / 6u, r->nested = 7, r->late = 3, r->max_us = 4321;
    r->gap_us = 120, r->t5_late = 1, r->lock_us = 55, r->boots = 2, r->crashes = 0;
    r->stage = 5 | 3 << 4, r->cpu2 = 1, r->radio = 2 | 4 << 4;
    r->app[0] = 41, r->app[1] = 77, r->app[2] = 0x34, r->app[3] = 0x12;
    r->app[4] = 0x78, r->app[5] = 0x56, r->app[6] = 0x34;
}
static int32_t tstk[3] = {20000, 7000, -1};    /* the stacks' room now (stacks.c plat_stack_left), as a run uses them */
static int32_t ds_stack(int k) { return tstk[k]; }

static int bad;
static void check(const char *what, int ok)
{
    printf("%-66s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t listed(uint32_t *first_seq, uint32_t *last_seq)   /* the records in the flash, their seqs' ends */
{
    uint8_t at[DS_SLOTS];
    uint32_t n = ds_list(at);
    ds_rec_t r;
    *first_seq = *last_seq = 0;
    if (n && ds_slot(at[0], &r) == 1)
        *first_seq = r.seq;
    if (n && ds_slot(at[n - 1u], &r) == 1)
        *last_seq = r.seq;
    return n;
}
static int ordered(void)                       /* the listed seqs go up by one each */
{
    uint8_t at[DS_SLOTS];
    uint32_t n = ds_list(at), k;
    ds_rec_t a, b;
    for (k = 1; k < n; k++) {
        if (ds_slot(at[k - 1u], &a) != 1 || ds_slot(at[k], &b) != 1)
            return 0;
        if (b.seq != a.seq + 1u)
            return 0;                          /* (up_s not: a restart starts the uptime again) */
    }
    return 1;
}
static void run_to(uint32_t ms)                /* the main loop's passes, one a millisecond, to ms */
{
    while (now_ms < ms) {
        now_ms += 1;
        ds_poll(now_ms, 900u, 5u);
    }
}
static void run_to_due(void)                  /* to the next 5th minute: one snapshot, one try at its write */
{
    run_to((now_ms / DS_PERIOD_MS + 1u) * DS_PERIOD_MS);
}
static void request(const char *frame_tail)    /* a request from the computer: F0 7D 4C 53 <tail> F7 */
{
    uint8_t p[16] = {0x7D, 0x4C, 0x53};
    uint32_t n = 3;
    while (*frame_tail)
        p[n++] = (uint8_t)*frame_tail++;
    nsent = 0;
    check("... the request is the stats'", ds_request(p, n) == 1);
}
static void restart(void)                      /* a crash or a reset: .noinit kept, the uptime from 0 */
{
    now_ms = 0;
    ds_init();
}
static void power(void)                        /* a power-off and on: .noinit garbage */
{
    memset(&ds_ni, 0xA5, sizeof ds_ni);
    restart();
}

int main(void)
{
    uint32_t n, a, b, e0, p0, i;
    memset(nor, 0xFF, sizeof nor);
    power();

    request("");
    check("empty flash: no records, \"end 0\"", nsent == 1 && !strcmp(sent[0], "end 0"));
    {
        const uint8_t lh[] = {0x7D, 0x4C, 0x48}, lg[] = {0x7D, 0x4C, 0x47};
        check("the crash history's request (LH) and the log's (LG) are not the stats'",
              ds_request(lh, 3) == 0 && ds_request(lg, 3) == 0);
    }

    /* off (the default): no snapshot, nothing written */
    run_to(20u * 60000u);
    check("switched off: 20 minutes, nothing written", listed(&a, &b) == 0 && progs == 0 && ds_ni.nq == 0);

    /* on at 20:00: the first at 25:00, then every 5 minutes */
    ds_enable(1);
    run_to(24u * 60000u + 59999u);
    check("on at 20:00: none before 25:00", listed(&a, &b) == 0);
    run_to(25u * 60000u);
    check("... one at 25:00 (quiet: written at once)", listed(&a, &b) == 1 && a == 1);
    run_to(46u * 60000u);
    n = listed(&a, &b);
    check("... 25, 30, 35, 40, 45: five, in order", n == 5 && a == 1 && b == 5 && ordered());
    request("");
    check("... the readout: 5 records x 3 frames, then \"end 5\"", nsent == 16 && !strcmp(sent[15], "end 5"));
    check("... the stat line's fixed form",
          !strcmp(sent[0], "stat 1/5 seq 1 up 1500 build ABCD1234 flags 09 boots 2 crashes 0 halves 250000 nested 7 "
                           "late 3 maxus 4321 gap 120 t5late 1 lock 55 pass 900 pstage 5 prog 0 stage 5 "
                           "wdt 3 cpu2 01 bt 02 wifi 4"));
    check("... the stk line: since the start, the least ever (-1: not known)",
          !strcmp(sent[1], "stk 1/5 main 20000 irq 7000 ever main 20000 irq 7000 c1 -1"));
    check("... the app line by the layout (u8 u8 u16 u32: the 4-byte one cut at the block's end)",
          !strcmp(sent[2], "app 1/5 c10 41 c60 77 drops 4660 sram 3430008"));
    ds_enable(0);
    run_to(70u * 60000u);
    check("switched off again: no more", listed(&a, &b) == 5);
    ds_enable(1);

    /* the wrap: the sector full (60), the 61st erases it, the newest 3 back first, then it */
    e0 = erases;
    while (listed(&a, &b) < DS_SLOTS)
        run_to(now_ms + DS_PERIOD_MS);
    check("60 records: the sector full, no erase yet", erases == e0 && a == 1 && b == 60 && ordered());
    check("... nothing at 0xE4F00..0xE4FFF, nothing off the sector",
          nor[0xE4F00] == 0xFF && nor[0xE4FFF] == 0xFF && nor[0xE3FFF] == 0xFF && nor[0xE5000] == 0xFF);
    run_to(now_ms + DS_PERIOD_MS);
    n = listed(&a, &b);
    check("the 61st: erased once, 58 59 60 back, then 61", erases == e0 + 1u && n == 4 && a == 58 && b == 61 &&
          ordered());
    run_to(now_ms + 3u * DS_PERIOD_MS);
    check("... and on: 64", listed(&a, &b) == 7 && b == 64 && ordered());

    /* a power cut at each program of an append: absent, the rest kept; the next one (restart) kept */
    for (i = 0; i < 2u; i++) {
        uint32_t na = listed(&a, &b);
        prog_left = (int)i;
        run_to_due();
        prog_left = -1;
        check(i ? "power cut before the magic: absent, the rest kept" : "power cut before the body: absent, the rest kept",
              listed(&a, &b) == na && ordered() && ds_pending() >= 0);
        if (i) {
            power();
            ds_enable(1);
            check("... a power-off: the queue lost, nothing made up", ds_ni.nq == 0 && listed(&a, &b) == na && ordered());
        } else {
            restart();
            check("... a restart: the boot writes the queued one", ds_boot(0, 0) == 0 && listed(&a, &b) == na + 1u &&
                  ordered());
            ds_enable(1);
        }
    }
    run_to(now_ms + DS_PERIOD_MS);
    n = listed(&a, &b);
    check("the next after the cuts: kept after the slots cut short", ordered() && ds_pending() < 0);

    /* the wrap cut: the erase cut short, then a restart */
    while (listed(&a, &b), ds_scan(&a, &n), n < DS_SLOTS)
        run_to(now_ms + DS_PERIOD_MS);
    listed(&a, &b);
    erase_cut = 1;
    run_to_due();
    check("the wrap's erase cut short: the record still queued", ds_pending() >= 0 && ds_last_rc == -1);
    restart();
    check("... a restart: the boot erases again, writes back, writes it", ds_boot(0, 0) == 0 && ds_pending() < 0);
    n = listed(&a, &b);
    check("... 3 back + it, in order, after the newest before", n == 4 && ordered() && b == a + 3u);
    ds_enable(1);

    /* the wrap cut in the write-back: a restart writes the record; a power cut loses the rest, makes up nothing */
    while (ds_scan(&a, &n), n < DS_SLOTS)
        run_to(now_ms + DS_PERIOD_MS);
    prog_left = 3;                             /* (the first written back, the second's body: then the cut) */
    run_to_due();
    prog_left = -1;
    n = listed(&a, &b);
    check("the write-back cut: one back, the record queued", n == 1 && ds_pending() >= 0 && ds_last_rc == -5);
    restart();
    check("... a restart: the record written after it (past the cut slot, its seq after the queue's)",
          ds_boot(0, 0) == 0 && listed(&a, &b) == 2 && b == a + 3u);
    ds_enable(1);

    /* a staged update: no write, no clear; the record waits and goes once the area is the stats' again */
    p0 = progs;
    staged = 1;
    run_to_due();
    check("an update staged: nothing written, the record queued", progs == p0 && ds_pending() >= 0 &&
          ds_last_rc == -7);
    request("CLR");
    check("... the clear refused: \"clear failed\"", nsent == 1 && !strcmp(sent[0], "clear failed") && listed(&a, &b) == 2);
    staged = 0;
    run_to(now_ms + DS_RETRY_MS);
    check("... unstaged (an update that failed: the area erased): written", ds_pending() < 0 && listed(&a, &b) == 3);
    memset(nor + 0xE4000u, 0xFF, 0x1000u);     /* (ota_unstage: the sector erased under the stats) */
    run_to(now_ms + DS_PERIOD_MS);
    {
        ds_rec_t r;
        check("... after an unstage's erase: the next goes on with the queue's order",
              listed(&a, &b) == 1 && ds_slot(0, &r) == 1 && r.seq > 3u);
    }

    /* playing (not quiet): the snapshot waits; still playing DS_FORCE_MS later, it goes, flagged DS_LOUD */
    quiet = 0;
    n = listed(&a, &b);
    run_to((now_ms / DS_PERIOD_MS + 1u) * DS_PERIOD_MS);
    check("playing: the snapshot queued, not written", listed(&a, &b) == n && ds_pending() >= 0);
    run_to(now_ms + DS_FORCE_MS - 1u);
    check("... not yet at 3:59", listed(&a, &b) == n);
    run_to(now_ms + 1u);
    {
        uint8_t at[DS_SLOTS];
        ds_rec_t r;
        uint32_t m = ds_list(at);
        check("... at 4:00 written anyway, flagged loud", m == n + 1u && ds_slot(at[m - 1u], &r) == 1 &&
              (r.flags & (DS_LOUD | DS_PERIODIC)) == (DS_LOUD | DS_PERIODIC));
    }
    run_to(now_ms + 30000u);
    quiet = 1;
    run_to(now_ms + DS_PERIOD_MS);
    {
        uint8_t at[DS_SLOTS];
        ds_rec_t r;
        uint32_t m = ds_list(at);
        check("... stopped: the next at once, not loud", ds_slot(at[m - 1u], &r) == 1 && !(r.flags & DS_LOUD));
    }

    /* the crash's snapshot: the boot writes the second before it, once */
    run_to(now_ms + 4321u);
    restart();
    n = listed(&a, &b);
    check("a crash (count 1): its snapshot written at the boot", ds_boot(1, 0) == 1 && listed(&a, &b) == n + 1u);
    {
        uint8_t at[DS_SLOTS];
        ds_rec_t r;
        uint32_t m = ds_list(at);
        check("... flagged crash, the crash count, not periodic", ds_slot(at[m - 1u], &r) == 1 && r.flags & DS_CRASH &&
              !(r.flags & DS_PERIODIC) && r.crashes == 1);
    }
    restart();
    check("... a restart, the same crash: not again", ds_boot(1, 0) == 0 && listed(&a, &b) == n + 1u);
    ds_enable(0);
    run_to(3000u);
    restart();
    check("crash 2, the switch off: its snapshot all the same", ds_boot(2, 0) == 1 && listed(&a, &b) == n + 2u);
    ds_ni.busy = 1;                            /* the boot's save of crash 3 struck by a crash */
    check("a save cut short: the next boot keeps nothing", ds_boot(3, 0) < 0 && !ds_ni.busy && listed(&a, &b) == n + 2u);
    check("... nor the one after", ds_boot(3, 0) == 0 && listed(&a, &b) == n + 2u);
    power();
    check("a power-on after a crash: no snapshot taken, none written", ds_boot(4, 0) == 0 && listed(&a, &b) == n + 2u);

    /* the stacks' least room ever: once a second, kept by a restart, from the flash after a power-on, the crash's */
    {
        ds_rec_t r;
        uint8_t at[DS_SLOTS];
        uint32_t m;
        ds_enable(1);
        tstk[0] = 3000, tstk[2] = 900;         /* (a deep pass: then back up) */
        run_to(now_ms + 1500u);
        tstk[0] = 20000, tstk[2] = 1800;
        run_to(now_ms + 1500u);
        check("the least ever: the deep second kept, the room now higher", ds_least_ever(PLAT_STK_MAIN) == 3000 &&
              ds_least_ever(PLAT_STK_C1) == 900 && ds_least_ever(PLAT_STK_IRQ) == 7000);
        run_to_due();
        m = ds_list(at);
        check("... in the next record (since the start: the room now)", ds_slot(at[m - 1u], &r) == 1 &&
              r.ever[0] == 3000 && r.ever[2] == 900 && r.stk_main == 20000);
        restart();
        check("... a restart keeps it", ds_boot(0, 0) == 0 && ds_least_ever(PLAT_STK_MAIN) == 3000);
        power();
        check("... a power-off loses the queue's", ds_ni.ever[0] == -1);
        check("... the boot takes the newest record's", ds_boot(0, 0) == 0 && ds_least_ever(PLAT_STK_MAIN) == 3000 &&
              ds_least_ever(PLAT_STK_C1) == 900);
        {
            const int32_t left[3] = {1500, 6000, -1};   /* (a crash with the main stack this deep: stacks.c) */
            run_to(now_ms + 1500u);
            restart();
            check("a crash 1500 bytes from the main stack's bottom: the least ever 1500", ds_boot(9, left) == 1 &&
                  ds_least_ever(PLAT_STK_MAIN) == 1500 && ds_least_ever(PLAT_STK_IRQ) == 6000 &&
                  ds_least_ever(PLAT_STK_C1) == 900);
            m = ds_list(at);
            check("... its snapshot carries it", ds_slot(at[m - 1u], &r) == 1 && r.flags & DS_CRASH && r.ever[0] == 1500);
            power();
            check("... and after a power-off, the flash's", ds_boot(0, 0) == 0 && ds_least_ever(PLAT_STK_MAIN) == 1500);
        }
    }

    /* the readout's frames: 3 a record, the end; the clear */
    request("");
    n = listed(&a, &b);
    check("the readout: all, three frames each, oldest first, \"end N\"", nsent == (int)(3u * n + 1u) &&
          !strncmp(sent[0], "stat 1/", 7) && !strncmp(sent[3u * n - 3u], "stat ", 5) && atoi(sent[3u * n] + 4) == (int)n);
    request("X");
    check("LS with other bytes: nothing sent, nothing erased", nsent == 0 && listed(&a, &b) == n);
    request("CLR");
    check("the clear answers \"cleared\"", nsent == 1 && !strcmp(sent[0], "cleared") && ds_ni.nq == 0);
    request("");
    check("... then \"end 0\"", nsent == 1 && !strcmp(sent[0], "end 0"));
    check("the clear empties the least ever too (.noinit: a restart would keep the erased records')",
          ds_ni.ever[0] == -1 && ds_ni.ever[1] == -1 && ds_ni.ever[2] == -1);
    restart();
    check("... and a restart after it knows no least ever", ds_boot(0, 0) == 0 && ds_ni.ever[0] == -1 &&
          ds_ni.ever[1] == -1 && ds_ni.ever[2] == -1);

    /* the least ever of a record is this build's alone: an install of another build starts it again */
    {
        ds_rec_t r;
        uint8_t at[DS_SLOTS];
        uint32_t m;
        ds_enable(1);
        tstk[0] = 4000, tstk[2] = 1100;
        run_to(now_ms + 1500u);
        tstk[0] = 20000, tstk[2] = 1800;
        run_to_due();
        m = ds_list(at);
        check("a record of this build carries its least ever", m && ds_slot(at[m - 1u], &r) == 1 && r.build == test_build &&
              r.ever[0] == 4000 && r.ever[2] == 1100);
        power();
        check("... a power-on of the same build takes it", ds_boot(0, 0) == 0 && ds_least_ever(PLAT_STK_MAIN) == 4000 &&
              ds_least_ever(PLAT_STK_C1) == 1100);
        test_build = 0x5A5A0001u;              /* (another build's install: the stacks may be sized otherwise) */
        power();
        check("... a power-on of another build does not: the room now", ds_boot(0, 0) == 0 && ds_ni.ever[0] == -1 &&
              ds_ni.ever[2] == -1 && ds_least_ever(PLAT_STK_MAIN) == 20000);
        test_build = 0xABCD1234u;
    }
    check("the update's record and the crash history never touched", nor[0xE4F00] == 0xFF && nor[0xE2000] == 0xFF);

    printf(bad ? "FAIL: %d\n" : "all ok\n", bad);
    return bad != 0;
}
