/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* plat_lock / plat_unlock (device/plat_lock.c) over a simulated CPU: icfg's global enable (bit 9), which cli clears
 * and sti sets. The outermost unlock switches interrupts on only if they were on at its lock: in the main loop, back
 * on; inside an interrupt (where they are off), they stay off, nested or not. Not the hardware: on the FM-1 it is
 * the same instructions' effect on the real icfg. */
#include <stdio.h>
#include <stdint.h>

static uint32_t sim_icfg, sim_ticks, sim_sti;     /* the register; the time; how many times sti ran */
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_icfg(void) { return sim_icfg; }
static void fm1_irq_off(void) { sim_icfg &= ~0x200u; }
static void fm1_irq_on(void)
{
    sim_icfg |= 0x200u;
    sim_sti++;
}
static uint32_t fm1_ticks(void) { return sim_ticks; }
#include "plat_lock.c"

static int fails;
static void want(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static int ie(void) { return (sim_icfg & 0x200u) != 0u; }

/* plat_midi_send's shape: lock, work, unlock */
static void send(void)
{
    uint32_t f = plat_lock();
    sim_ticks += 24u * 5u;                        /* 5 us with interrupts off */
    plat_unlock(f);
}
static void nested(void)
{
    uint32_t f = plat_lock();
    send();
    plat_unlock(f);
}

int main(void)
{
    sim_icfg = 0x300u;                            /* bit 8 (the sources' enable) and 9 on: the main loop */
    {
        uint32_t f = plat_lock();
        want("a lock switches interrupts off", !ie());
        plat_unlock(f);
        want("the main loop's unlock switches them back on", ie());
    }
    {
        uint32_t f = plat_lock(), g;
        g = plat_lock();
        plat_unlock(g);
        want("an inner unlock leaves them off", !ie());
        plat_unlock(f);
        want("the outermost unlock switches them on", ie());
    }
    want("bit 8 untouched", (sim_icfg & 0x100u) != 0u);

    /* inside an interrupt (app_render in ALNK0): the CPU has the global enable off */
    fm1_irq_off();
    sim_sti = 0;
    send();
    want("a lock inside an interrupt leaves them off", !ie());
    nested();
    want("a nested lock inside an interrupt leaves them off", !ie());
    want("no sti inside the interrupt", sim_sti == 0u);
    want("the depth back at 0", lock_depth == 0u);
    fm1_irq_on();                                 /* (rti) */

    /* the main loop again after the interrupt: the lock still restores them */
    send();
    want("the main loop's lock after an interrupt switches them back on", ie());

    /* the measurement: the longest, from the outermost lock */
    lock_max = 0;
    nested();
    want("lock_max: the outermost section's time (5 us)", lock_max == 5u);

    /* an unbalanced unlock changes nothing */
    fm1_irq_off();
    plat_unlock(1u);
    want("an unlock with no lock leaves them as they were", !ie());

    printf(fails ? "lock: %d FAILED\n" : "lock: all passed\n", fails);
    return fails != 0;
}
