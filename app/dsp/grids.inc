/* SPDX-License-Identifier: GPL-3.0-only
 * Mutable Dreams · GRIDS (Mutable Instruments' topographic drum sequencer, Emilie Gillet, GPL-3.0-or-later).
 * The drum map (build/gen grids_map.h, extracted unchanged by tools/gen_grids.py) is read like the module's
 * pattern_generator.cc: 25 nodes on a 5 x 5 map, 3 instruments x 32 steps, the four nodes around (x, y) blended.
 * As a kit generator, it fills KICK (pad 0), SNARE (pad 1) and HAT (pad 2); the kit plays them as usual. */
#include "grids_map.h"

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
 * chaos 0..255 (how far each part strays at the start of a pattern). seed: the project's random state, so a locked
 * seed repeats the pattern (the sequencer's rule) and a free one gives a new one. Pads 4-8 are left as they are:
 * what was played or recorded there stays when GRIDS rolls again. */
static void grids_generate(md_kit &k, uint8_t x, uint8_t y, uint8_t density, uint8_t chaos, unsigned seed)
{
    k.length = 32;
    k.gates[0] = k.gates[1] = k.gates[2] = 0;
    unsigned r = seed ? seed : 1u;
    for (int i = 0; i < 3; i++)                    /* each part strays by its own amount, as in the module */
        grids_perturb[i] = (uint8_t)(((random_word(r) >> 24) * (uint32_t)(chaos * 63u >> 8)) >> 8);
    const uint8_t threshold = (uint8_t)~density;   /* a part plays when its level is above it */
    for (unsigned s = 0; s < 32; s++)
        for (int i = 0; i < 3; i++) {
            int level = grids_level((int)s, i, x, y);
            level = level < 255 - grids_perturb[i] ? level + grids_perturb[i] : 255;
            if ((uint8_t)level > threshold)
                k.gates[i] |= 1u << s;
        }
}
