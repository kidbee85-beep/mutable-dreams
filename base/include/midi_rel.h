/* SPDX-License-Identifier: MPL-2.0 */
/* Copyright (C) 2026 Charles Vestal */
/* Endless encoders over MIDI: a CC's value as a turn, not a place, for an app's CC input (an app keeps its own setting,
 * absolute or endless, and its own map: LOOM-8's Setup > SEL > CC IN). The two codes controllers use for relative
 * CCs, told apart by the value:
 *   about 64 (binary offset): 65 is +1, 63 is -1, 64 nothing;
 *   signed (two's complement): 1 is +1, 127 is -1, 0 nothing.
 * One detent sends a small turn, so a value within 8 of 64 is read as the first code and anything else as the second
 * (a signed turn of 56 or more in one message is not an encoder's). No state, no allocation: include it anywhere. */
#pragma once

/* the turn a relative CC's value (0..127) asks for: -64..63, 0 for none */
static inline int midi_rel_delta(int v)
{
    v &= 0x7F;
    if (v >= 56 && v <= 72)
        return v - 64;
    return v < 64 ? v : v - 128;
}

/* a value lo..hi turned by a relative CC's value v, held at its ends */
static inline int midi_rel_apply(int cur, int v, int lo, int hi)
{
    int n = cur + midi_rel_delta(v);
    return n < lo ? lo : n > hi ? hi : n;
}
