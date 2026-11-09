/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* USB audio around a render block (device/uac_mix.h) on felucca-bsp's own usb.c (uac_tap, uac_play): the USB recording
 * holds the app's render alone, never the computer's audio; the computer's audio comes in at plat_usb_play_gain's level
 * times MASTER (the square of its position), with USB AUDIO full after the tap at MASTER over both; muted, uac_play
 * still reads its ring (the read position moves as at any level, the stream stays primed).
 *   cc -Ibsp/chip -Ibsp/boards/fm1 tests/uac_mix_test.c [-DTEST_NO_OUT]   (TEST_NO_OUT: without usb_audio_out) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_UAC 1
#ifdef TEST_NO_OUT
#define FELUCCA_UAC_OUT 0
#else
#define FELUCCA_UAC_OUT 1
#endif
#define FELUCCA_UAC_48K 0
#define HALF_FRAMES 256u
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "usb.c"
#include "../device/uac_mix.h"

#define N 32u
#define APP 500000                      /* the app's render, 24-bit */
#define PC 1000                         /* the computer's audio, 16-bit: 256000 at 24-bit as sent */
static int fails;
static void check(int ok, const char *what, long got, long want)
{
    if (!ok) {
        printf("FAIL %s: got %ld, want %ld\n", what, got, want);
        fails++;
    }
}
static int near(long a, long b) { return a - b < 2 && b - a < 2; }

#if FELUCCA_UAC_OUT
static void feed(uint32_t n)            /* the computer's packets into the output ring */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        uo_ring[(uo_w + i) & (UO_N - 1u)] = (uint32_t)(uint16_t)PC | (uint32_t)(uint16_t)(-PC) << 16;
    uo_w += n;
}
#endif

/* one block: the app's render at its level, then uac_mix; *rec the recorded frame (16-bit L), out the output's L, R */
static void block(uint32_t gain, uint32_t master, int full, uint32_t play, int32_t *l, int32_t *r, int32_t *rec)
{
    int32_t o[2u * N];
    uint32_t i, w = ua_w;
    for (i = 0; i < N; i++)
        o[2u * i] = o[2u * i + 1u] = APP;
#if FELUCCA_UAC_OUT
    feed(N);
#endif
    uac_mix(o, N, gain, master, full, play);
    for (i = 1; i < N; i++)
        check(o[2u * i] == o[0] && o[2u * i + 1u] == o[1], "a block's frames alike", o[2u * i], o[0]);
    *l = o[0];
    *r = o[1];
    *rec = (int16_t)(ua_ring[w & (UA_N - 1u)] & 0xFFFFu);
    check(ua_w == w + N, "the recording took the block", (long)(ua_w - w), N);
    ua_r = ua_w;                        /* the computer read it */
}

int main(void)
{
    int32_t l, r, rec;
    const int32_t rec_app = (APP + 128) >> 8;
    usb.config = 1;
    uac.feed = 1;                       /* the computer records */
#if FELUCCA_UAC_OUT
    uo.alt = 1;                         /* the computer plays */
    feed(UO_TGT);                       /* primed */
    block(4096, 4096, 0, 4096, &l, &r, &rec);   /* MASTER up, the computer at unity */
    check(uo.go == 1, "primed", uo.go, 1);
    check(near(l, APP + PC * 256), "unity: the output's L", l, APP + PC * 256);
    check(near(r, APP - PC * 256), "unity: the output's R", r, APP - PC * 256);
    check(rec == rec_app, "unity: the recording, the app alone", rec, rec_app);

    block(4096, 4096, 0, 2048, &l, &r, &rec);   /* half */
    check(near(l, APP + PC * 128), "half: the output", l, APP + PC * 128);
    check(rec == rec_app, "half: the recording", rec, rec_app);

    block(1024, 2048, 0, 4096, &l, &r, &rec);   /* MASTER at half its travel: the square, a quarter (the app's own
                                                 * render would be at 1024 too: here it is given at full, as a stand-in) */
    check(near(l, APP + PC * 64), "MASTER half: the computer at a quarter", l, APP + PC * 64);

    {   /* muted: nothing added, and the ring read as at any level */
        uint32_t r0 = uo_r, r1, under = uo.underruns;
        block(4096, 4096, 0, 4096, &l, &r, &rec);
        r1 = uo_r;
        block(4096, 4096, 0, 0, &l, &r, &rec);
        check(l == APP && r == APP, "muted: the app alone out", l, APP);
        check(rec == rec_app, "muted: the recording", rec, rec_app);
        check(uo.go == 1 && uo.underruns == under, "muted: still primed", uo.go, 1);
        check(uo_r - r1 + 1u >= r1 - r0 && uo_r - r1 <= r1 - r0 + 1u, "muted: the ring read as at unity",
              (long)(uo_r - r1), (long)(r1 - r0));
        check(uo_w - uo_r < UO_TGT + N, "muted: the ring does not fill", (long)(uo_w - uo_r), UO_TGT);
    }

    /* USB AUDIO full: the render (at 4096) recorded, then the computer's at its level, then MASTER over both */
    block(1024, 2048, 1, 4096, &l, &r, &rec);
    check(rec == rec_app, "full: the recording at full level, the app alone", rec, rec_app);
    check(near(l, (APP + PC * 256) / 4), "full: MASTER over both", l, (APP + PC * 256) / 4);
    block(1024, 2048, 1, 0, &l, &r, &rec);
    check(near(l, APP / 4), "full, muted: MASTER over the app", l, APP / 4);
    check(rec == rec_app, "full, muted: the recording", rec, rec_app);

    uo.alt = 0;                         /* the computer stopped playing: the app alone */
    block(4096, 4096, 0, 4096, &l, &r, &rec);
    check(l == APP, "not playing: the app alone", l, APP);
#else
    block(4096, 4096, 0, 4096, &l, &r, &rec);   /* without usb_audio_out: the app, recorded, as ever */
    check(l == APP && r == APP && rec == rec_app, "no output: the app alone", l, APP);
    block(1024, 2048, 1, 0, &l, &r, &rec);
    check(rec == rec_app && near(l, APP / 4), "no output, full: recorded at full, MASTER after", l, APP / 4);
#endif
    printf(fails ? "uac_mix: %d failed\n" : "uac_mix: all passed\n", fails);
    return fails != 0;
}
