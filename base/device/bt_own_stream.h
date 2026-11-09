/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The MIDI the own stack's ATT sends to the computer (bt/ble/ble_att.c att_midi_out, in the event interrupt, a
 * connection event at a time): the out queue (plat_midi_send's, through radio.c's per-link rings) and the app's stream
 * (plat_ble_stream: a screen mirrored), merged. A frame of the stream begun goes on before anything queued, so no
 * message ends it; between frames the queue goes first. The stream is read straight from the app's ring, as much as the
 * notifications of an event take, so its rate is the link's, not the main loop's (ble_midi_drain moves at most the
 * queue's 64 packets an event: 192 octets, 6.1 KB/s at a Mac's 30 ms; measured 2026-10-28).
 *
 * The includer defines, before it: own_q_peek / own_q_pop (the queue: peek 0 when empty), own_stream_on (a computer
 * listens: the stream is read only then) and own_stream_ms (the time a stream packet goes with, ms). Part of
 * bt_own.c; tests/bt/ble_stack_test.c -DBLE_STREAM_TEST builds it on the stack with a fake queue. */
static const struct plat_ble_stream *volatile own_stream;
static uint8_t own_from_stream;                   /* the packet peeked is the stream's (the interrupts' alone) */

int plat_ble_stream(const struct plat_ble_stream *s)
{
    own_stream = s && s->peek && s->pop && s->mid ? s : 0;
    return 1;
}

BLE_API int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms)
{
    const struct plat_ble_stream *s = own_stream;
    uint32_t q, qt;
    int queued = own_q_peek(&q, &qt);
    own_from_stream = 0;
    if (s && own_stream_on() && (!queued || s->mid()) && s->peek(pkt)) {
        own_from_stream = 1;
        *t_ms = own_stream_ms() & 0x1FFFu;
        return 1;
    }
    if (!queued)
        return 0;
    *pkt = q;
    *t_ms = qt;
    return 1;
}
BLE_API void ble_app_midi_pop(void)
{
    const struct plat_ble_stream *s = own_stream;
    if (own_from_stream) {
        own_from_stream = 0;
        if (s)
            s->pop();
        return;
    }
    own_q_pop();
}
