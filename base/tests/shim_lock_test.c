/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* shim_irq_save / shim_irq_restore (device/shim_lock.c) over a simulated CPU: icfg's global enable (bit 9), which cli
 * clears and sti sets. The outermost restore switches interrupts on only if they were on at its save: in the main
 * loop, back on; inside an interrupt (where they are off), they stay off, nested or not. The interrupt here has the
 * shape of btctrler.a's ble_rx_irq_handler: local_irq_disable, again inside, both enabled again, then the return.
 * Not the hardware: on the FM-1 it is the same instructions' effect on the real icfg. */
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
#include "shim_lock.c"

static int fails;
static void want(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static int ie(void) { return (sim_icfg & 0x200u) != 0u; }

/* misc.c's local_irq_disable / local_irq_enable: each save's value kept for its restore */
static uint32_t saved[8];
static int depth;
static void local_irq_disable(void) { saved[depth++] = shim_irq_save(); }
static void local_irq_enable(void) { shim_irq_restore(saved[--depth]); }

/* shim_ms's (or malloc's) shape: save, work, restore */
static void section(void)
{
    uint32_t f = shim_irq_save();
    sim_ticks += 24u * 5u;                        /* 5 us with interrupts off */
    shim_irq_restore(f);
}
static void radio_isr(void)                       /* ble_rx_irq_handler's shape */
{
    local_irq_disable();
    local_irq_disable();
    section();
    local_irq_enable();
    local_irq_enable();
}

int main(void)
{
    sim_icfg = 0x300u;                            /* bit 8 (the sources' enable) and 9 on: the main loop */
    {
        uint32_t f = shim_irq_save();
        want("a save switches interrupts off", !ie());
        shim_irq_restore(f);
        want("the main loop's restore switches them back on", ie());
    }
    {
        uint32_t f = shim_irq_save(), g;
        g = shim_irq_save();
        shim_irq_restore(g);
        want("an inner restore leaves them off", !ie());
        shim_irq_restore(f);
        want("the outermost restore switches them on", ie());
    }
    want("bit 8 untouched", (sim_icfg & 0x100u) != 0u);

    /* inside the radio's interrupt: the CPU has the global enable off */
    fm1_irq_off();
    sim_sti = 0;
    section();
    want("a section inside an interrupt leaves them off", !ie());
    radio_isr();
    want("nested sections inside an interrupt leave them off", !ie());
    want("no sti inside the interrupt", sim_sti == 0u);
    want("the depth back at 0", shim_depth == 0u);
    fm1_irq_on();                                 /* (rti) */

    /* the main loop again after the interrupt: the sections still restore them */
    local_irq_disable();
    section();
    local_irq_enable();
    want("the main loop's sections after an interrupt switch them back on", ie());

    /* the measurement: the longest, from the outermost save */
    shim_off_max = 0;
    local_irq_disable();
    section();
    local_irq_enable();
    want("shim_off_max: the outermost section's time (5 us)", shim_off_max == 5u);

    /* an unbalanced restore changes nothing */
    fm1_irq_off();
    shim_irq_restore(1u);
    want("a restore with no save leaves them as they were", !ie());

    printf(fails ? "shim_lock: %d FAILED\n" : "shim_lock: all passed\n", fails);
    return fails != 0;
}
