/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Henri David */
/* shim_irq_save / shim_irq_restore (ble/shim/shim.h): the shim's critical sections, which JieLi's libraries take
 * through local_irq_disable / local_irq_enable (misc.c) and the shim's own heap, queues, semaphores and timers. Part
 * of radio.c; on its own for tests/shim_lock_test.c, which gives it fm1_icfg, fm1_irq_off, fm1_irq_on and fm1_ticks
 * over a simulated CPU.
 *
 * They nest (the shim calls itself). Each shim_irq_save returns whether interrupts were on when it was called (icfg
 * bit 9, the global enable cli and sti clear and set), and the shim_irq_restore given that value switches them back
 * on only if they were and only at the outermost restore. The radio's own interrupts take these sections (btctrler.a
 * RF_ble.c: ble_event_irq_handler and ble_rx_irq_handler call local_irq_disable / enable): there interrupts are
 * off, and they stay off. Before, the outermost restore always switched them on, inside those interrupts too. */
#define SHIM_LOCK_IE 0x200u                       /* icfg bit 9: the global interrupt enable */
static volatile uint32_t shim_depth;
uint32_t shim_off_max, shim_off_from;            /* the longest the stack kept interrupts off (us), and from where
                                                  * (main_fm1.c's log reads and clears them) */
static uint32_t shim_off_t0, shim_off_ra;
uint32_t shim_irq_save(void)                     /* nests, unlike irq_save(): the shim calls itself */
{
    uint32_t was_on = (fm1_icfg() & SHIM_LOCK_IE) != 0u;
    fm1_irq_off();
    if (shim_depth++ == 0) {
        shim_off_t0 = fm1_ticks();
        shim_off_ra = (uint32_t)(uintptr_t)__builtin_return_address(0);
    }
    return was_on;
}
void shim_irq_restore(uint32_t f)                 /* f: what the matching shim_irq_save returned */
{
    if (shim_depth && --shim_depth == 0) {
        uint32_t us = (fm1_ticks() - shim_off_t0) / FM1_TICKS_PER_US;
        if (us > shim_off_max) {
            shim_off_max = us;
            shim_off_from = shim_off_ra;
        }
        if (f)
            fm1_irq_on();
    }
}
