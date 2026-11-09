/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0
 * Copyright (C) 2026 Henri David */
/* A BLE-MIDI message's time on our clock, from the sender's own (its 13-bit millisecond timestamps): the connection
 * interval's bunching taken out (Optimist's io/midi/midi_ble.c ble_in_time, its clock follower's input). */
#ifndef BLE_TIME_H
#define BLE_TIME_H
#include <stdint.h>
#include "ble_cfg.h"

struct ble_in_clock {
    uint32_t s, o, at;                   /* the sender's time unwrapped, ms; the offset, ticks; our last arrival, ticks */
    uint16_t ts;
    uint8_t ok, late;                    /* late: messages in a row more than BLE_TIME_LATE_MS late */
};
/* a message stamped ts (ms, 13 bits) in a packet whose newest stamp is last_ts, arriving at now (ticks, tick_ms of
 * them a millisecond) -> the time it was sent, on that clock (now at the latest) */
BLE_API uint32_t ble_in_time(struct ble_in_clock *c, uint32_t now, uint32_t tick_ms, uint16_t ts, uint16_t last_ts);
#endif
