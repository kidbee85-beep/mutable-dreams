/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-reader) */
/* device/announce.c on the host: the screen reader's protocol (fm1-reader PROTOCOL.md: silent until a HELLO, the
 * answer, a reader gone after 5 s, BYE, WHERE), the SAY frames (flags, cut at a word, 7-bit), and ann_later's
 * settling. (The words: fm1-reader's tests/words_test.mjs.)
 *   cc -std=c99 -Wall -Wextra -Werror -o announce_test tests/announce_test.c && ./announce_test */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ANN_NAME "TEST 1.0"
#define ANN_WORDS_URL "https://example.org/words/0123456789.json"
#include "../device/announce.c"

static uint8_t sent[4096];
static uint32_t sent_n, frames, refuse;
static int ann_wire_send(const uint8_t *p, uint32_t n)
{
    if (refuse)
        return -1;
    memcpy(sent + sent_n, p, n);
    sent_n += n;
    frames++;
    return 0;
}
static void clear(void) { sent_n = frames = 0; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void in(uint8_t cmd, uint32_t now)
{
    uint8_t f[5] = {ANN_H0, ANN_H1, ANN_H2, cmd, ANN_VER};
    CHECK(ann_frame(f, cmd == ANN_HELLO ? 5u : 4u, now) == 1, "frame %u not taken", cmd);
}
/* the text of the last SAY sent, and its flags */
static const char *last_said(int *flags)
{
    static char t[300];
    uint32_t i = 0, s = 0, k = 0;
    while (i < sent_n) {                       /* the last F0 .. F7 */
        if (sent[i] == 0xF0)
            s = i;
        i++;
    }
    CHECK(sent[s + 4] == ANN_SAY, "last frame is not a SAY");
    if (flags)
        *flags = sent[s + 5];
    for (i = s + 6; i < sent_n && sent[i] != 0xF7; i++) {
        CHECK(sent[i] < 0x80, "byte 0x%02X is not 7-bit", sent[i]);
        t[k++] = (char)sent[i];
    }
    t[k] = 0;
    return t;
}
int main(void)
{
    /* silent until a reader says HELLO */
    ann_say("SAVED", 0, ANN_NOW);
    ann_later("TUNE 5", ANN_NOW, 100, 0);
    ann_tick(500);
    CHECK(frames == 0, "%u frames sent with no reader", frames);
    CHECK(!ann_on(), "on with no reader");
    CHECK(!ann_where(), "where with no reader");

    /* HELLO: the answer (version, name), and the whole screen asked for once */
    in(ANN_HELLO, 1000);
    CHECK(ann_on(), "not on after HELLO");
    CHECK(frames == 1 && sent[4] == ANN_HELLO && sent[5] == ANN_VER && !memcmp(sent + 6, "TEST 1.0", 9) &&
              !memcmp(sent + 15, ANN_WORDS_URL, sizeof ANN_WORDS_URL - 1) && sent[15 + sizeof ANN_WORDS_URL - 1] == 0xF7,
          "HELLO answer wrong: want name, 0, link");
    CHECK(ann_where() && !ann_where(), "WHERE not asked once on a new reader");
    in(ANN_HELLO, 2000);
    CHECK(!ann_where(), "a second HELLO asked for the screen again");

    /* SAY: flags, words */
    clear();
    ann_say("MEMORY FULL: ", "NOT SAVED", ANN_NOW | ANN_KEEP);
    {
        int fl;
        const char *t = last_said(&fl);
        CHECK(fl == 3, "flags %d", fl);
        CHECK(!strcmp(t, "MEMORY FULL: NOT SAVED"), "said \"%s\" (the screen's text, as it is)", t);
    }

    /* ann_later: the newest, once it settles (or every 4 x quiet while it keeps changing) */
    clear();
    ann_later("TUNE 1", ANN_NOW, 100, 3000);
    ann_later("TUNE 2", ANN_NOW, 100, 3050);
    ann_tick(3100);
    CHECK(frames == 0, "said before it settled");
    ann_tick(3150);
    CHECK(frames == 1 && !strcmp(last_said(0), "TUNE 2"), "the settled one not said");
    clear();
    for (uint32_t t = 4000; t < 4500; t += 50) {
        ann_later(t & 64 ? "TUNE 3" : "TUNE 4", ANN_NOW, 100, t);
        ann_tick(t);
    }
    CHECK(frames >= 1, "a long run never heard");
    /* ann_say without NOW: what waits goes first */
    clear();
    ann_later("TUNE 9", ANN_NOW, 100, 5000);
    ann_say("SAVED", 0, 0);
    CHECK(frames == 2, "the waiting one not said first (%u)", frames);

    /* too long: cut at a word, within ANN_MAX */
    clear();
    {
        char big[600];
        int i;
        for (i = 0; i < 590; i++)
            big[i] = (i % 6 == 5) ? ' ' : 'W';
        big[590] = 0;
        ann_say(big, 0, 0);
        const char *t = last_said(0);
        CHECK(strlen(t) <= ANN_MAX && t[strlen(t) - 1] != ' ', "cut wrong (%zu)", strlen(t));
    }

    /* no room: not sent, nothing half sent */
    clear();
    refuse = 1;
    ann_say("SAVED", 0, 0);
    refuse = 0;
    CHECK(sent_n == 0, "half a frame sent");

    /* gone: no HELLO for 5 s */
    ann_tick(2000 + ANN_GONE_MS + 1);
    CHECK(!ann_on(), "still on 5 s after the last HELLO");
    in(ANN_HELLO, 9000);
    in(ANN_BYE, 9100);
    CHECK(!ann_on(), "on after BYE");
    in(ANN_HELLO, 9200);
    ann_where();
    in(ANN_WHERE, 9300);
    CHECK(ann_where(), "WHERE not asked");
    {
        uint8_t other[4] = {0x7D, 0x46, 0x4C, 1};    /* someone else's SysEx */
        CHECK(!ann_frame(other, 4, 9400), "took another protocol's frame");
    }

    printf(fails ? "announce: %d FAILED\n" : "announce: all passed\n", fails);
    return fails != 0;
}
