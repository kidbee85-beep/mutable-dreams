/* SPDX-License-Identifier: GPL-3.0-only
 * Mutable Dreams · GRIDS (Mutable Instruments' topographic drum sequencer, Emilie Gillet, GPL-3.0-or-later).
 * The drum map (build/gen grids_map.h, extracted unchanged by tools/gen_grids.py) is read like the module's
 * pattern_generator.cc: 25 nodes on a 5 x 5 map, 3 instruments x 32 steps, the four nodes around (x, y) blended.
 * As a kit generator, it fills KICK (pad 0), SNARE (pad 1) and HAT (pad 2); the kit plays them as usual. */
#include "grids_map.h"

static uint32_t grids_rng = 0x1234567u;
static uint8_t grids_perturb[3];

static uint8_t grids_mix(uint8_t a, uint8_t b, uint8_t bal) { return (uint8_t)((a * (255 - bal) + b * bal) >> 8); }

/* the level of instrument's step at (x, y): the four nodes around it, blended */
static uint8_t grids_level(int step, int inst, uint8_t x, uint8_t y)
{
    uint8_t i = x >> 6, j = y >> 6;
    const uint8_t *a = GRIDS_NODES[GRIDS_MAP[i][j]], *b = GRIDS_NODES[GRIDS_MAP[i + 1][j]];
    const uint8_t *c = GRIDS_NODES[GRIDS_MAP[i][j + 1]], *d = GRIDS_NODES[GRIDS_MAP[i + 1][j + 1]];
    int o = inst * 32 + step;
    return grids_mix(grids_mix(a[o], b[o], (uint8_t)(x << 2)), grids_mix(c[o], d[o], (uint8_t)(x << 2)), (uint8_t)(y << 2));
}

/* fill KICK/SNARE/HAT gates from the map. x/y: position 0..255 (the two macro knobs), density 0..255 (threshold),
 * chaos 0..255 (how far each part strays at the start of a pattern). */
static void grids_generate(md_kit &k, uint8_t x, uint8_t y, uint8_t density, uint8_t chaos)
{
    k.length = 32;
    memset(k.gates, 0, sizeof k.gates);
    grids_rng = grids_rng * 1664525u + 1013904223u;
    for (int i = 0; i < 3; i++)
        grids_perturb[i] = (uint8_t)(((grids_rng >> 24) * (uint32_t)(chaos * 63u >> 8)) >> 8);
    const uint8_t threshold = (uint8_t)~density;   /* a part plays when its level is above it */
    for (unsigned s = 0; s < 32; s++)
        for (int i = 0; i < 3; i++) {
            int level = grids_level((int)s, i, x, y);
            level = level < 255 - grids_perturb[i] ? level + grids_perturb[i] : 255;
            if ((uint8_t)level > threshold)
                k.gates[i] |= 1u << s;
        }
}
