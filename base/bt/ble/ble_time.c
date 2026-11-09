/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0
 * Copyright (C) 2026 Henri David */
/* A BLE-MIDI message's time on our clock (ble_time.h). The sender's millisecond (ts, 13 bits; unwrapped into c->s,
 * the packet's newest last_ts at most 8 s on) plus the offset o of the least delayed message lately: o follows each
 * earlier one at once and drifts up 1/4096 of the time since (the two crystals' drift, ~250 ppm). A sender whose times
 * do not move (some send 0: the same stamp again 20 ms or more later) or a gap of 4 s: o from this one (its arrival,
 * less how much older than the packet's newest it is). A late message keeps o (a packet lost waits for the next connection event: 30 ms and more while Bluetooth
 * audio streams beside it); only BLE_TIME_LATE_N in a row more than BLE_TIME_LATE_MS late take o from the last one
 * (2026-10-25, on an FM-1 following a Mac: one message 40 ms late did, and at 30 ms events that moved the clock every
 * few ticks). From Optimist's io/midi/midi_ble.c. */
#include "ble_time.h"

#define BLE_TIME_LATE_MS 100u
#define BLE_TIME_LATE_N 24u                       /* (a beat of F8s) */
#define BLE_TIME_STILL_MS 20u                     /* the same stamp again this much later: a sender whose times do not move */

BLE_API uint32_t ble_in_time(struct ble_in_clock *c, uint32_t now, uint32_t tick_ms, uint16_t ts, uint16_t last_ts)
{
    uint32_t arr, t, rise;
    uint32_t back = (uint32_t)(uint16_t)(last_ts - ts) & 0x1FFFu;   /* ms older than the packet's newest */
    arr = now - back * tick_ms;                   /* (its arrival, less that) */
    if (c->ok && now - c->at < 4000u * tick_ms && (ts != c->ts || now - c->at < BLE_TIME_STILL_MS * tick_ms))
        c->s += (uint32_t)(uint16_t)(ts - c->ts) & 0x1FFFu;
    else
        c->s = 0, c->o = arr, c->ok = 1;
    rise = (now - c->at) >> 12;
    c->ts = ts;
    c->at = now;
    t = arr - c->s * tick_ms;                     /* the offset this message gives */
    if ((int32_t)(t - (c->o + rise)) < 0)
        c->o = t;                                 /* less delayed: the offset */
    else
        c->o += rise;
    t = c->o + c->s * tick_ms;
    c->late = (int32_t)(now - t) > (int32_t)(BLE_TIME_LATE_MS * tick_ms) ? (uint8_t)(c->late + 1u) : 0u;
    if (c->late >= BLE_TIME_LATE_N || (int32_t)(t - now) > 0) {
        c->s = 0;                                 /* late a beat long, or ahead of now: from this one */
        c->o = arr;
        c->late = 0;
        t = arr;
    }
    return t;
}
