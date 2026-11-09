/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* plat_headphones' debounce (device/plat_hp.c) over a simulated jack switch and clock, one main loop pass a
 * millisecond: -1 before the first reading, which counts at once; a plug's bounce (flips for ~150 ms) gives one change,
 * 100 ms after the pin settles, not before; a flip shorter than 100 ms is ignored; the clock's wrap changes nothing.
 * Not the pin: on an FM-1 fm1_hp_in is felucca-bsp's read of PB5. */
#include <stdio.h>
#include <stdint.h>

static uint32_t sim_raw, fm1_ms;
static uint32_t fm1_hp_in(void) { return sim_raw; }
#include "plat_hp.c"

static int fails;
static void want(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static void pass_ms(uint32_t ms)                    /* ms main loop passes, one a millisecond */
{
    while (ms--) {
        fm1_ms++;
        plat_hp_read();
    }
}
/* the first time (ms from now) plat_headphones reads `want_st`, within `limit` ms; -1 if never */
static int32_t until(int want_st, uint32_t limit)
{
    uint32_t t;
    for (t = 0; t <= limit; t++) {
        if (plat_headphones() == want_st)
            return (int32_t)t;
        pass_ms(1);
    }
    return -1;
}
static void run_from(uint32_t start)
{
    static const uint32_t bounce[] = {12, 30, 8, 25, 40, 35};   /* a plug's flips, ms apart (150 ms) */
    uint32_t i;
    int32_t t;
    hp_st = -1, hp_raw = 0, hp_t = 0;
    fm1_ms = start;
    sim_raw = 0;
    want("before the first reading: -1", plat_headphones() == -1);
    pass_ms(1);
    want("the first reading counts at once (nothing plugged)", plat_headphones() == 0);

    for (i = 0; i < sizeof bounce / sizeof bounce[0]; i++) {
        sim_raw ^= 1u;
        pass_ms(bounce[i]);
        if (plat_headphones() != 0)
            break;
    }
    want("a plug's bounce: no change while it bounces", i == sizeof bounce / sizeof bounce[0]);
    sim_raw = 1;                                    /* settled: plugged */
    t = until(1, 200);
    want("plugged: counted 100 ms after the pin settled (seen at the next pass)", t == 101);

    sim_raw = 0;                                    /* a short flip */
    pass_ms(60);
    sim_raw = 1;
    want("a 60 ms flip ignored", until(0, 300) == -1 && plat_headphones() == 1);

    sim_raw = 0;                                    /* unplugged, clean */
    t = until(0, 200);
    want("unplugged: counted after 100 ms held", t == 101);
}

int main(void)
{
    run_from(1000u);
    run_from(0xFFFFFF80u);                          /* the millisecond clock wraps during the plug */
    printf(fails ? "hp: %d FAILED\n" : "hp: all passed\n", fails);
    return fails != 0;
}
