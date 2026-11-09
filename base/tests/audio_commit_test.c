/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* audio_commit (device/audio_commit.c, FM1B_AUDIO_ASIDE) over a simulated audio DMA and interrupt enable: the half
 * rendered aside is copied into the DMA's free half only while that half is still free and no new half is pending,
 * whole, and the other half is never touched; late (the DMA moved on before the copy, or a new half pending), nothing
 * is written and it says so; the DMA moving on during the copy is reported late; the interrupts are off for the copy
 * and put back as they were (on in the main loop, off inside the audio interrupt). Not the chip: on the FM-1 the
 * DMA's side is felucca-bsp's fm1_audio.h. */
#include <stdio.h>
#include <stdint.h>

#define HALF_WORDS 256u
#define FM1_AUDIO_HALF 1u
static int32_t abuf[2u * HALF_WORDS];
static uint32_t sim_icfg, sim_free, sim_pending, sim_reads, sim_reads_irq_on, sim_move_at;
static uint32_t fm1_icfg(void) { return sim_icfg; }
static void fm1_irq_off(void) { sim_icfg &= ~0x200u; }
static void fm1_irq_on(void) { sim_icfg |= 0x200u; }
static uint32_t fm1_audio_free_half(void)          /* the DMA: plays the other half; sim_move_at: moves on at that read */
{
    sim_reads++;
    sim_reads_irq_on += (sim_icfg & 0x200u) != 0u;
    if (sim_move_at && sim_reads == sim_move_at) {
        sim_free ^= 1u;
        sim_pending |= FM1_AUDIO_HALF;
    }
    return sim_free;
}
static uint8_t fm1_audio_pending(void) { return (uint8_t)sim_pending; }
#include "audio_commit.c"

static int fails;
static void want(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static void setup(uint32_t free, uint32_t pending, uint32_t move_at, uint32_t icfg)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = -1;
    for (i = 0; i < HALF_WORDS; i++)
        aside[i] = (int32_t)(i * 3u + 1u);
    sim_free = free, sim_pending = pending, sim_move_at = move_at, sim_icfg = icfg;
    sim_reads = sim_reads_irq_on = 0;
}
static int half_is(uint32_t half, int aside_in)    /* 1: the half holds aside (aside_in) or is untouched (-1s) */
{
    uint32_t i;
    for (i = 0; i < HALF_WORDS; i++)
        if (abuf[half * HALF_WORDS + i] != (aside_in ? aside[i] : -1))
            return 0;
    return 1;
}

int main(void)
{
    uint32_t r;
    setup(1u, 0u, 0u, 0x300u);
    r = audio_commit(1u);
    want("in time: copied, whole, into the free half", r == 1u && half_is(1u, 1));
    want("the half the DMA plays untouched", half_is(0u, 0));
    want("interrupts off while the DMA's state was read", sim_reads > 0u && sim_reads_irq_on == 0u);
    want("the main loop's interrupts back on after", (sim_icfg & 0x200u) != 0u && (sim_icfg & 0x100u) != 0u);

    setup(0u, 0u, 0u, 0x300u);
    r = audio_commit(1u);
    want("the DMA moved on (its free half is the other): late, nothing written", r == 0u && half_is(0u, 0) &&
                                                                                  half_is(1u, 0));
    setup(1u, FM1_AUDIO_HALF, 0u, 0x300u);
    r = audio_commit(1u);
    want("a new half pending: late, nothing written", r == 0u && half_is(0u, 0) && half_is(1u, 0));

    setup(1u, 0u, 2u, 0x300u);                     /* the DMA moves on during the copy */
    r = audio_commit(1u);
    want("the DMA moving on during the copy: reported late", r == 0u);
    want("and the other half (now the free one) not written", half_is(0u, 0));

    setup(0u, 0u, 0u, 0x100u);                     /* inside the audio interrupt: the global enable off */
    r = audio_commit(0u);
    want("inside the interrupt: copied", r == 1u && half_is(0u, 1) && half_is(1u, 0));
    want("inside the interrupt: interrupts left off", (sim_icfg & 0x200u) == 0u);

    printf(fails ? "audio-commit: %d FAILED\n" : "audio-commit: all passed\n", fails);
    return fails != 0;
}
