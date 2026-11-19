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
#include "clouds/dsp/granular_processor.h"

/* ------------------------------------------------------------ Clouds --- */
static clouds::GranularProcessor clouds_proc;
static uint8_t clouds_large[118784];
static uint8_t clouds_small[65536 - 128];

extern "C" void mi_particles_init(void)
{
    // (No memset: the buffers start zeroed as statics, and Init / Prepare reset what Clouds reads.)
    clouds_proc.Init(clouds_large, sizeof clouds_large, clouds_small, sizeof clouds_small);
    clouds_proc.set_num_channels(2);
    clouds_proc.set_low_fidelity(false);
    clouds_proc.set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);
    clouds_proc.Prepare();
}

static inline float mi_unit(float v, float top) { return v < 0.0f ? 0.0f : v > top ? top : v; }

extern "C" void mi_particles_process(const mi_particles_params *p, const float *in, float *out, unsigned n)
{
    clouds::Parameters *q = clouds_proc.mutable_parameters();
    clouds::ShortFrame fi[32], fo[32];
    clouds_proc.set_playback_mode(static_cast<clouds::PlaybackMode>(p->mode & 3));
    clouds_proc.set_quality(p->lofi ? 2 : 0);
    // Clouds' tables are read at index * size and index * size + 1: a value of exactly 1.0 (a knob the module never
    // quite reaches, our Lab does) reads one entry past the end (AddressSanitizer: lut_xfade_in at dry_wet 1.0).
    // So every 0..1 value stays just below 1.
    const float top = 0.99999f;
    q->position = mi_unit(p->position, top);
    q->size = mi_unit(p->size, top);
    q->pitch = p->pitch;
    q->density = mi_unit(p->density, top);
    q->texture = mi_unit(p->texture, top);
    q->dry_wet = top;                            // the Lab mixes (fully wet)
    q->stereo_spread = mi_unit(p->spread, top);
    q->feedback = mi_unit(p->feedback, top);
    q->reverb = mi_unit(p->reverb, top);
    q->freeze = p->freeze != 0;
    q->trigger = false;
    q->gate = false;
    while (n) {
        unsigned m = n > 32 ? 32 : n, k, r;
        // The module calls Prepare from its idle main loop, many times between two blocks; in STRETCH each call lets
        // the WSOLA correlator weigh more candidate windows, and without enough of them no window is ever scheduled.
        for (r = 0; r < (p->mode == 1 ? 8u : 1u); r++)
            clouds_proc.Prepare();
        for (k = 0; k < m; k++) {
            float l = in[2 * k] * 32767.0f, r = in[2 * k + 1] * 32767.0f;
            fi[k].l = (short)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            fi[k].r = (short)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
        clouds_proc.Process(fi, fo, m);
        for (k = 0; k < m; k++) {
            out[2 * k] = fo[k].l * (1.0f / 32768.0f);
            out[2 * k + 1] = fo[k].r * (1.0f / 32768.0f);
        }
        in += 2 * m;
        out += 2 * m;
        n -= m;
    }
}

