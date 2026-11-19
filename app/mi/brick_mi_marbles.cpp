// SPDX-License-Identifier: GPL-3.0-only
// Mutable Dreams for Trimui Brick, by Kid Bunka: the bridge to original Mutable Instruments DSP code by
// Emilie Gillet (MIT, third_party/mi, eurorack commit f4cfa76, stmlib commit d18def8):
//   PARTICLES  the granular processor of Clouds: granular, stretch (WSOLA), looping delay, spectral
//   CROSSMOD   the modulator of Warps: one track (or the mix) cross-modulates another
//   RANDOM     the random sequence and beta distributions of Marbles: deja vu melodies
// Compiled with -Dstmlib=mi_stmlib so this newer stmlib never meets the 2.1 engine's copy at link time.
#include "brick_mi.h"
#include <string.h>
#include <math.h>
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/random_sequence.h"
#include "marbles/random/distributions.h"

/* ----------------------------------------------------------- Marbles --- */
static marbles::RandomGenerator marbles_gen;
static marbles::RandomStream marbles_stream;
static marbles::RandomSequence marbles_seq, marbles_tseq;

extern "C" void mi_random_init(uint32_t seed)
{
    marbles_gen.Init(seed);
    marbles_stream.Init(&marbles_gen);
    marbles_seq.Init(&marbles_stream);
    marbles_tseq.Init(&marbles_stream);
}

/* the next step: whether it plays (the t side: probability through its own deja vu loop) and its value 0..1
 * (the x side: the deja vu loop shaped by Marbles' beta distribution: spread and bias) */
extern "C" int mi_random_step(const mi_random_params *p, float *value)
{
    float u, t;
    marbles_seq.set_deja_vu(p->deja_vu);
    marbles_seq.set_length(p->length);
    marbles_tseq.set_deja_vu(p->deja_vu);
    marbles_tseq.set_length(p->length);
    t = marbles_tseq.NextValue(false, 0.0f);
    u = marbles_seq.NextValue(false, 0.0f);
    *value = marbles::BetaDistributionSample(u, p->spread, p->bias);
    return t < p->probability;
}

/* a second Marbles of its own for VOICES (TRIGGER MARBLES): its t side decides whether a burst of grains comes, its
   x side where in the voice; deja vu brings back the same places, in loops of 8 */
static marbles::RandomGenerator marbles_gen2;
static marbles::RandomStream marbles_stream2;
static marbles::RandomSequence marbles_seq2, marbles_tseq2;
static int marbles2_ready;
extern "C" int mi_random2_step(const mi_random_params *p, float *value)
{
    float u, t;
    if (!marbles2_ready) {
        marbles_gen2.Init(0x5EEDF00Du);
        marbles_stream2.Init(&marbles_gen2);
        marbles_seq2.Init(&marbles_stream2);
        marbles_tseq2.Init(&marbles_stream2);
        marbles2_ready = 1;
    }
    marbles_seq2.set_deja_vu(p->deja_vu);
    marbles_seq2.set_length(p->length);
    marbles_tseq2.set_deja_vu(p->deja_vu);
    marbles_tseq2.set_length(p->length);
    t = marbles_tseq2.NextValue(false, 0.0f);
    u = marbles_seq2.NextValue(false, 0.0f);
    *value = marbles::BetaDistributionSample(u, p->spread, p->bias);
    return t < p->probability;
}
