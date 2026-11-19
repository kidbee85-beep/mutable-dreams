// SPDX-License-Identifier: GPL-3.0-only
// Mutable Dreams for Trimui Brick, by Kid Bunka: eight complete Plaits voices (Mutable Instruments, Emilie Gillet,
// MIT; third_party/mi/plaits at 44.1 kHz, see PATCHES.md) for the POLY voice: all 24 models, each voice with the
// level patched (the Lab's envelope opens Plaits' low pass gate) and its trigger input as the gate: high while the
// note is held (the 6-op FM models keep their envelopes only then), pulled low for one frame at a new note.
// Compiled with -Dstmlib=mi_stmlib -Dplaits=mi_plaits: never meets the 2.1 engine's own Plaits parts.
#include "brick_mi.h"
#include <string.h>
#include "plaits/dsp/voice.h"

enum { POLY_MAX = 8, POLY_MEM = 32768 };
static plaits::Voice poly_voice[POLY_MAX];
static char poly_mem[POLY_MAX][POLY_MEM];

extern "C" void mi_plaits_init(void)
{
    for (int i = 0; i < POLY_MAX; i++) {
        stmlib::BufferAllocator allocator(poly_mem[i], POLY_MEM);
        poly_voice[i].Init(&allocator);
    }
}

extern "C" void mi_plaits_render(int v, const mi_plaits_patch *q, float level, int gate, int retrigger, float *out,
                                 float *aux, unsigned n)
{
    plaits::Patch p;
    plaits::Modulations m;
    plaits::Voice::Frame f[plaits::kMaxBlockSize];
    memset(&p, 0, sizeof p);
    memset(&m, 0, sizeof m);
    p.note = q->note;
    p.harmonics = q->harmonics;
    p.timbre = q->timbre;
    p.morph = q->morph;
    p.engine = q->engine;
    p.decay = q->decay;
    p.lpg_colour = q->lpg_colour;
    m.level = level;
    m.level_patched = true;
    m.trigger_patched = true;
    while (n) {
        unsigned b = n > plaits::kMaxBlockSize ? plaits::kMaxBlockSize : n, k;
        if (retrigger && gate)
            b = 1;                               // one frame low: the next is a rising edge
        m.trigger = gate && !retrigger ? 1.0f : 0.0f;
        retrigger = 0;
        poly_voice[v].Render(p, m, f, b);
        for (k = 0; k < b; k++) {
            out[k] = f[k].out * (1.0f / 32768.0f);
            aux[k] = f[k].aux * (1.0f / 32768.0f);
        }
        out += b;
        aux += b;
        n -= b;
    }
}
