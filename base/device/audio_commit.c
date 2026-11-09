/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The half rendered aside (FM1B_AUDIO_ASIDE 1, main_fm1.c fm1_alnk0_irq): the render goes into a buffer of its own
 * and is copied into the DMA's free half only while that half is still free (no new half pending, the DMA on the same
 * side), with the interrupts off for the copy: a render that runs late leaves the half the DMA now plays as it was
 * (the audio of two halves before, whole) instead of writing into it while it plays, which tore the output mid-half (a
 * click at every late half). 2 KiB of RAM. Part of main_fm1.c (after abuf and HALF_WORDS); on its own for
 * tests/audio_commit_test.c, which gives it the DMA's state and the interrupt enable over a simulated chip. */
static int32_t aside[HALF_WORDS] __attribute__((aligned(4)));
static uint32_t audio_commit(uint32_t half)        /* 1 copied, 0 late (nothing written) */
{
    volatile int32_t *dst = &abuf[half * HALF_WORDS];
    uint32_t ie = fm1_icfg() & 0x200u, ok = 0, i;   /* (icfg bit 9: the global enable) */
    fm1_irq_off();
    if (fm1_audio_free_half() == half && !(fm1_audio_pending() & FM1_AUDIO_HALF)) {
        for (i = 0; i < HALF_WORDS; i++)
            dst[i] = aside[i];
        ok = fm1_audio_free_half() == half && !(fm1_audio_pending() & FM1_AUDIO_HALF);
    }
    if (ie)
        fm1_irq_on();
    return ok;
}
