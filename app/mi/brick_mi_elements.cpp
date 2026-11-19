// SPDX-License-Identifier: GPL-3.0-only
// Mutable Dreams for Trimui Brick, by Kid Bunka: Elements (Mutable Instruments, Emilie Gillet, MIT;
// third_party/mi/elements), the modal voice: bow, blow and strike exciters into a resonator, with its space. Elements
// computes at 32 kHz (its envelopes, filters and modes are made for it), so here it runs at its own rate and a linear
// resampler brings it to 44.1 kHz. Compiled with -Delements=mi_elements: never meets the 2.1 engine's own parts.
#include "brick_mi.h"
#include <string.h>
#include "elements/dsp/part.h"

static elements::Part el_part;
static uint16_t el_reverb[32768];
static int el_ready;
static float el_l[3], el_r[3];                    /* the last two 32 kHz frames, and the position between them */
static double el_phase = 1.0;
static float el_ql[elements::kMaxBlockSize], el_qr[elements::kMaxBlockSize];
static unsigned el_qn, el_qpos;

static void el_block(const mi_elements_params *q)
{
    elements::PerformanceState ps;
    elements::Patch *p = el_part.mutable_patch();
    float blow[elements::kMaxBlockSize], strike[elements::kMaxBlockSize];
    memset(blow, 0, sizeof blow);
    memset(strike, 0, sizeof strike);
    p->exciter_envelope_shape = q->contour;
    p->exciter_bow_level = q->bow, p->exciter_bow_timbre = q->bow_timbre;
    p->exciter_blow_level = q->blow, p->exciter_blow_meta = q->flow, p->exciter_blow_timbre = q->blow_timbre;
    p->exciter_strike_level = q->strike, p->exciter_strike_meta = q->mallet, p->exciter_strike_timbre = q->strike_timbre;
    p->exciter_signature = 0;
    p->resonator_geometry = q->geometry, p->resonator_brightness = q->brightness;
    p->resonator_damping = q->damping, p->resonator_position = q->position;
    p->resonator_modulation_frequency = 0, p->resonator_modulation_offset = 0;
    p->reverb_diffusion = 0.625f, p->reverb_lp = 0.7f;
    p->space = q->space;
    p->modulation_frequency = 0;
    ps.gate = q->gate != 0, ps.note = q->note, ps.modulation = 0, ps.strength = q->strength;
    el_part.Process(ps, blow, strike, el_ql, el_qr, elements::kMaxBlockSize);
    el_qn = elements::kMaxBlockSize;
    el_qpos = 0;
}

extern "C" void mi_elements_process(const mi_elements_params *q, float *out_l, float *out_r, unsigned n)
{
    if (!el_ready) {
        el_part.Init(el_reverb);
        el_ready = 1;
    }
    el_part.set_resonator_model((elements::ResonatorModel)(q->model % 3));
    for (unsigned k = 0; k < n; k++) {
        el_phase += 32000.0 / 44100.0;
        while (el_phase >= 1.0) {                 /* the next 32 kHz frame */
            if (el_qpos >= el_qn)
                el_block(q);
            el_l[0] = el_l[1], el_r[0] = el_r[1];
            el_l[1] = el_ql[el_qpos], el_r[1] = el_qr[el_qpos];
            el_qpos++;
            el_phase -= 1.0;
        }
        out_l[k] = el_l[0] + (el_l[1] - el_l[0]) * (float)el_phase;
        out_r[k] = el_r[0] + (el_r[1] - el_r[0]) * (float)el_phase;
    }
}
