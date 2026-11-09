/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0
 * Copyright (C) 2026 Henri David */
/* Host test of a BLE-MIDI message's time on our clock (bt/ble/ble_time.c ble_in_time), on a clock of 24 ticks a us:
 *   bunching   MIDI clock at 120 BPM (20.8 ms apart, the sender's 13-bit ms stamps), delivered in connection events
 *              30 ms apart, each event a few ms late at random: the times given are the sent ones a constant offset
 *              later, within a millisecond (the stamps' grain), never after the arrival
 *   one late   a message 60 ms late (a packet that waited for the next event) keeps the offset: its time is where it
 *              was sent, not when it came
 *   late beat  24 in a row more than 100 ms late: the offset taken again from them (the times move up)
 *   still      a sender whose stamps do not move (some send 0): each message's time its arrival
 *   wrap       the 13-bit stamps wrapping (8.192 s) leave the spacing as sent
 * Exit status: the number of failed checks. */
#include <stdint.h>
#include <stdio.h>
#define BLE_API static
#include "../../bt/ble/ble_time.c"

#define TMS (1000u * 24u)                         /* ticks a millisecond */
static int fails;
static void check(int ok, const char *what)
{
    printf("ble_time: %-80s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t rnd_s = 12345u;
static uint32_t rnd(uint32_t n) { rnd_s = rnd_s * 1103515245u + 12345u; return (rnd_s >> 16) % n; }

int main(void)
{
    struct ble_in_clock c = {0};
    uint32_t base = 1000000u, i, ev, bad = 0, after = 0, off0 = 0, worst = 0;
    /* bunching: tick i sent at i * 20.8 ms (sender ms, wrapped to 13 bits), arriving at the first 30 ms event after
     * it plus 0..4 ms, the packet's newest stamp the last tick in that event */
    for (i = 0; i < 400u; i++) {
        uint32_t sent_us = i * 20833u, evn = sent_us / 30000u + 1u, ev_us = evn * 30000u + (evn * 2654435761u >> 8) % 4000u;   /* (one lateness an event) */
        uint32_t last_i = i, now, t, d;
        while ((last_i + 1u) * 20833u < (sent_us / 30000u + 1u) * 30000u)
            last_i++;                             /* (the newest tick in this packet) */
        now = base + ev_us * 24u;
        t = ble_in_time(&c, now, TMS, (uint16_t)((sent_us / 1000u) & 0x1FFFu), (uint16_t)(((last_i * 20833u) / 1000u) & 0x1FFFu));
        if ((int32_t)(t - now) > 0)
            after++;
        d = t - (base + sent_us * 24u);           /* the offset: constant, a few ms */
        if (i == 50u)
            off0 = d;
        if (i > 50u) {
            uint32_t e = (uint32_t)((int32_t)(d - off0) < 0 ? -(int32_t)(d - off0) : (int32_t)(d - off0));
            if (e > worst)
                worst = e;
            if (e > 2u * TMS)
                bad++;
        }
    }
    printf("ble_time: bunching: offset %u us, worst change %u us\n", off0 / 24u, worst / 24u);
    check(!after, "bunching: never a time after the arrival");
    check(!bad && worst <= TMS + TMS / 2u, "bunching: the sent spacing kept within 1.5 ms (connection events 30 ms apart)");
    /* one late: the next tick arrives 60 ms late */
    {
        uint32_t sent_us = 400u * 20833u, now = base + (sent_us + 60000u) * 24u, t;
        uint16_t ts = (uint16_t)((sent_us / 1000u) & 0x1FFFu);
        t = ble_in_time(&c, now, TMS, ts, ts);
        ev = t - (base + sent_us * 24u);
        check((uint32_t)((int32_t)(ev - off0) < 0 ? -(int32_t)(ev - off0) : (int32_t)(ev - off0)) <= 2u * TMS,
              "one late message: its time where it was sent (the offset kept)");
    }
    /* a late beat: 24 in a row 150 ms late, then the offset follows them */
    {
        uint32_t k, t = 0, sent_us = 0;
        for (k = 0; k < 26u; k++) {
            sent_us = (401u + k) * 20833u;
            uint32_t now = base + (sent_us + 150000u) * 24u;
            uint16_t ts = (uint16_t)((sent_us / 1000u) & 0x1FFFu);
            t = ble_in_time(&c, now, TMS, ts, ts);
        }
        check(t - (base + sent_us * 24u) >= 140u * TMS, "24 late in a row: the offset taken from them");
    }
    /* still: stamps that never move */
    {
        struct ble_in_clock s = {0};
        uint32_t k, ok = 1;
        for (k = 0; k < 50u; k++) {
            uint32_t now = base + k * 30000u * 24u + rnd(2000u) * 24u;
            uint32_t t = ble_in_time(&s, now, TMS, 0, 0);
            if ((uint32_t)((int32_t)(now - t) < 0 ? -(int32_t)(now - t) : (int32_t)(now - t)) > 31u * TMS)
                ok = 0;
        }
        check(ok, "a sender whose stamps stay 0: the times its arrivals (within an event)");
    }
    /* wrap: 13-bit stamps around 8191 */
    {
        struct ble_in_clock w = {0};
        uint32_t k, t, prev = 0, ok = 1;
        for (k = 0; k < 20u; k++) {
            uint32_t sent_ms = 8100u + k * 10u, now = base + (sent_ms + 5u) * TMS;
            uint16_t ts = (uint16_t)(sent_ms & 0x1FFFu);
            t = ble_in_time(&w, now, TMS, ts, ts);
            if (k && (t - prev < 9u * TMS || t - prev > 11u * TMS))
                ok = 0;
            prev = t;
        }
        check(ok, "the stamps wrapping at 8.192 s: spaced as sent");
    }
    printf("ble_time: %d failed\n", fails);
    return fails;
}
