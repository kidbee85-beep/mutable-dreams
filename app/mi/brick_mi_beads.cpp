// SPDX-License-Identifier: GPL-3.0-only
// Mutable Dreams for Trimui Brick, by Kid Bunka: the bridge to the granular processor of Mutable Instruments Beads
// by Emilie Gillet (MIT, third_party/mi, eurorack commit f4cfa76), called as beads/beads.cc calls it: blocks at
// about 4 kHz (12 frames: even, as the tape media halve the rate inside), the pots as base values, no CV, no seed
// input, and the SEED button latched: without it the trigger generator's gate stays low and no grain is ever born.
// Compiled with -Dstmlib=mi_stmlib (see brick_mi_clouds.cpp).
#include "brick_mi.h"
#include <string.h>
#include "beads/dsp/processor.h"

using namespace beads;

static Processor beads_proc;
static uint8_t beads_mem_a[512 * 1024];          // as beads.cc: 512 KiB (two 256 KiB halves) and 256 KiB
static uint8_t beads_mem_b[256 * 1024];
static uint16_t beads_reverb[32768];
static int beads_media = -1, beads_mono = -1;

extern "C" void mi_beads_init(void)
{
    uint8_t *buffers[3] = {beads_mem_a, beads_mem_a + 256 * 1024, beads_mem_b};
    // (The memories start zeroed as statics; a restart while playing must not stop the sound for a memset of
    // 800 KB: Configure's Reset makes the old contents unreachable, and the reverb's tail fades.)
    beads_proc.Init(buffers, beads_reverb);
    beads_media = beads_mono = -1;
}

static void param(RandomizedParameter *p, float base, float random)
{
    p->base = base;
    p->attenuverter = random;                    // -1 .. 1: < 0 random jumps, > 0 a random walk (no CV patched)
    p->cv = 0.0f;
    p->has_cv = false;
}

extern "C" void mi_beads_process(const mi_beads_params *q, const float *in, float *out, unsigned n)
{
    FloatFrame fi[12], fo[12];
    bool freeze[12];
    Parameters p;
    ModulatedParameter d;
    Processor::MixParameters m;
    if (q->media != beads_media || q->mono != beads_mono) {
        beads_media = q->media;
        beads_mono = q->mono;
        beads_proc.Configure(static_cast<Processor::Media>(q->media & 3), q->mono != 0);
    }
    param(&p.time, q->time, q->time_random);
    param(&p.size, q->size, q->size_random);
    param(&p.shape, q->shape, q->shape_random);
    param(&p.pitch, q->pitch, q->pitch_random);
    p.monophonic_mode = false;
    d.base = q->density;
    d.cv = 0.0f;
    m.feedback = q->feedback;
    m.dry_wet = 1.0f;                            // the Lab mixes
    m.reverb_amount = q->reverb;
    m.output_gate = false;
    while (n) {
        unsigned b = n > 12 ? 12 : n, k;
        for (k = 0; k < b; k++) {
            fi[k].l = in[2 * k];
            fi[k].r = in[2 * k + 1];
            freeze[k] = q->freeze != 0;
        }
        beads_proc.Process(SEED_BUTTON_STATE_LOCKED, NULL, freeze, d, p, m, fi, fo, b);
        for (k = 0; k < b; k++) {
            float l = fo[k].l, r = fo[k].r;
            out[2 * k] = l > 1.0f ? 1.0f : l < -1.0f ? -1.0f : l;
            out[2 * k + 1] = r > 1.0f ? 1.0f : r < -1.0f ? -1.0f : r;
        }
        in += 2 * b;
        out += 2 * b;
        n -= b;
    }
}

extern "C" float mi_beads_reserve(void) { return beads_proc.overwrite_reserve(); }
