/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Henri David */
/* plat_lock / plat_unlock (plat.h): interrupts off around what the main loop shares with the 1 kHz tick (app_tick, in
 * the timer interrupt) or the audio render (app_render, in the ALNK0 interrupt). Part of plat_fm1.c; on its own for
 * tests/lock_test.c, which gives it fm1_icfg, fm1_irq_off, fm1_irq_on and fm1_ticks over a simulated CPU.
 *
 * The lock nests. Each plat_lock returns whether interrupts were on when it was called (icfg bit 9, the global
 * enable cli and sti clear and set), and the plat_unlock given that value switches them back on only if they were
 * and only at the outermost unlock. So a lock taken where interrupts are already off (inside an interrupt, or inside
 * the app's own section with them off) leaves them off: before, the outermost unlock always switched them on, inside
 * an interrupt too. */
#define LOCK_IE 0x200u                            /* icfg bit 9: the global interrupt enable */
static uint32_t lock_depth;
uint32_t lock_max, lock_from;                     /* the longest the app kept interrupts off (us), and from where */
static uint32_t lock_t0, lock_ra;
uint32_t plat_lock(void)
{
    uint32_t was_on = (fm1_icfg() & LOCK_IE) != 0u;
    fm1_irq_off();
    if (lock_depth++ == 0) {
        lock_t0 = fm1_ticks();
        lock_ra = (uint32_t)(uintptr_t)__builtin_return_address(0);
    }
    return was_on;
}
void plat_unlock(uint32_t f)                      /* f: what the matching plat_lock returned */
{
    if (lock_depth && --lock_depth == 0) {
        uint32_t us = (fm1_ticks() - lock_t0) / FM1_TICKS_PER_US;
        if (us > lock_max) {
            lock_max = us;
            lock_from = lock_ra;
        }
        if (f)
            fm1_irq_on();
    }
}
