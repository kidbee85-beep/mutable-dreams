// SPDX-License-Identifier: GPL-3.0-only
// Mutable Dreams for Trimui Brick, by Kid Bunka: the bridge to Mutable Instruments' Tides 2 (Emilie Gillet, MIT,
// third_party/mi/tides2, eurorack commit f4cfa76): its PolySlopeGenerator, looping, at control range, as the TIDE
// of VOICES. Frequency, slope (pw), shape, smoothness as on the module; its main output (GATES mode, shift = level 1)
// scaled to 0..1. A second generator of its own draws one cycle for the stage (never the one the sound uses).
// Compiled with -Dtides=mi_tides -Dstmlib=mi_stmlib.
#include "brick_mi.h"
#include <string.h>
#include "tides2/poly_slope_generator.h"

using namespace tides;

static PolySlopeGenerator tides_gen[6], tides_view;
static int tides_ready;
static const float tides_lo = -5.0f, tides_hi = 5.0f;   /* (its main output swings +-5, as on the module) */

static void tides_init(void)
{
    if (tides_ready)
        return;
    for (int i = 0; i < 6; i++)
        tides_gen[i].Init();
    tides_view.Init();
    tides_ready = 1;
}

static float tides_one(PolySlopeGenerator *g, float f, float slope, float shape, float smooth, unsigned n, float *trace)
{
    static stmlib::GateFlags gates[256];
    static PolySlopeGenerator::OutputSample out[256];
    float last = 0;
    while (n) {
        unsigned k = n > 256 ? 256 : n;
        g->Render(RAMP_MODE_LOOPING, OUTPUT_MODE_GATES, RANGE_CONTROL, f, slope, shape, smooth, 1.0f, gates, 0, out, k);
        for (unsigned i = 0; i < k; i++) {
            float v = (out[i].channel[0] - tides_lo) / (tides_hi - tides_lo);
            v = v < 0 ? 0 : v > 1 ? 1 : v;
            if (trace)
                *trace++ = v;
            last = v;
        }
        n -= k;
    }
    return last;
}

extern "C" float mi_tides_render(int voice, float hz, float slope, float shape, float smooth, unsigned n)
{
    tides_init();
    return tides_one(&tides_gen[voice & 7], hz / 44100.0f, slope, shape, smooth, n, 0);
}

/* one cycle of the same settings, npts points (a generator of its own, run fresh) */
extern "C" void mi_tides_preview(float slope, float shape, float smooth, float *pts, int npts)
{
    static float buf[1024];
    int per = 1024 / 2;
    tides_init();
    tides_view.Init();
    tides_one(&tides_view, 1.0f / (float)per, slope, shape, smooth, (unsigned)(per * 2), buf);   /* (settle, then a cycle) */
    for (int i = 0; i < npts; i++)
        pts[i] = buf[per + i * per / npts];
}
