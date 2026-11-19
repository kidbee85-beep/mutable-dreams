// SPDX-License-Identifier: GPL-3.0-only
// Mutable Dreams for Trimui Brick, by Kid Bunka: Rings (Mutable Instruments, Emilie Gillet, MIT; third_party/mi/rings)
// as four resonators: two for the FX RACKs (a signal sets them ringing) and one for SYNTH's RINGS (strummed by
// notes), one spare. Rings computes at 48 kHz; here it runs at 44.1 kHz with its pitch raised by 1.47 semitones so
// notes stay in tune. Compiled with -Drings=mi_rings: never meets the 2.1 engine's own Rings parts.
#include "brick_mi.h"
#include <string.h>
#include <math.h>
#include "rings/dsp/part.h"

enum { RINGS_MAX = 4 };
static rings::Part ri_part[RINGS_MAX];
static uint16_t ri_reverb[RINGS_MAX][32768];
static int ri_ready[RINGS_MAX];
static const float RI_TUNE = 1.4671f;              /* 12 log2(48000 / 44100) */

extern "C" void mi_rings_process(int i, const mi_rings_params *q, const float *in, float *out_l, float *out_r, unsigned n)
{
    rings::PerformanceState ps;
    rings::Patch patch;
    float ib[rings::kMaxBlockSize], ob[rings::kMaxBlockSize], ab[rings::kMaxBlockSize];
    if (i < 0 || i >= RINGS_MAX)
        return;
    if (!ri_ready[i]) {
        ri_part[i].Init(ri_reverb[i]);
        ri_ready[i] = 1;
    }
    ri_part[i].set_polyphony(q->polyphony < 1 ? 1 : q->polyphony > 4 ? 4 : q->polyphony);
    ri_part[i].set_model((rings::ResonatorModel)(q->model % rings::RESONATOR_MODEL_LAST));
    patch.structure = q->structure, patch.brightness = q->brightness, patch.damping = q->damping, patch.position = q->position;
    memset(&ps, 0, sizeof ps);
    ps.internal_exciter = q->internal_exciter != 0;
    ps.internal_strum = !q->strum_by_note;
    ps.internal_note = false;
    ps.tonic = 0;
    ps.note = q->note + RI_TUNE;
    ps.fm = 0;
    ps.chord = q->chord;
    for (unsigned c = 0; c < n; c += rings::kMaxBlockSize) {
        unsigned m = n - c < rings::kMaxBlockSize ? n - c : rings::kMaxBlockSize;
        for (unsigned k = 0; k < m; k++)
            ib[k] = in ? in[c + k] : 0.0f;
        ps.strum = q->strike && c == 0;
        ri_part[i].Process(ps, patch, ib, ob, ab, m);
        for (unsigned k = 0; k < m; k++) {
            out_l[c + k] = ob[k];
            out_r[c + k] = ab[k];
        }
    }
}
