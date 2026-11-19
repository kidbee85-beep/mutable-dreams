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
#include "warps/dsp/modulator.h"

/* ------------------------------------------------------------- Warps --- */
static warps::Modulator warps_mod;

extern "C" void mi_crossmod_init(void)
{
    warps_mod.Init(44100.0f);
}

extern "C" void mi_crossmod_process(const mi_crossmod_params *p, const float *carrier, const float *modulator,
                                    float *out, unsigned n)
{
    warps::Parameters *q = warps_mod.mutable_parameters();
    warps::ShortFrame fi[warps::kMaxBlockSize], fo[warps::kMaxBlockSize];
    q->channel_drive[0] = p->drive_carrier;
    q->channel_drive[1] = p->drive_modulator;
    q->modulation_algorithm = p->algorithm;
    q->modulation_parameter = p->timbre;
    q->carrier_shape = 0;                        // external carrier: a track
    q->frequency_shift_pot = 0.5f;
    q->frequency_shift_cv = 0.0f;
    q->phase_shift = 0.0f;
    q->note = 60.0f;
    while (n) {
        unsigned m = n > 64 ? 64 : n, k;           // a multiple of the oversampling's block (6)? 64 frames: as the module
        for (k = 0; k < m; k++) {
            float c = carrier[k] * 32767.0f, d = modulator[k] * 32767.0f;
            fi[k].l = (short)(c > 32767 ? 32767 : c < -32768 ? -32768 : c);
            fi[k].r = (short)(d > 32767 ? 32767 : d < -32768 ? -32768 : d);
        }
        warps_mod.Process(fi, fo, m);
        for (k = 0; k < m; k++)
            out[k] = fo[k].l * (1.0f / 32768.0f);
        carrier += m;
        modulator += m;
        out += m;
        n -= m;
    }
}

