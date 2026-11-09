/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* USB audio around one render block (main_fm1.c render_block; tests/uac_mix_test.c), in this order:
 *   1. the USB recording (usb_audio_in: uac_tap) takes the app's render alone, so recording the FM-1 never records
 *      the computer's own playback;
 *   2. the computer's audio (usb_audio_out: uac_play) is added for the speaker and headphones, at plat_usb_play_gain's
 *      level times MASTER's (the square of its position). uac_play runs at level 0 too: it still reads the ring, so its
 *      rate tracking stays locked and turning it up again starts in step;
 *   3. with USB AUDIO full (plat_usb_audio_full; the render came at 4096), MASTER for the speaker after the tap, over
 *      both: the computer's audio was added at plat_usb_play_gain's level alone, MASTER here.
 * The includer has uac_tap and uac_play (bsp/chip/usb.c) when FELUCCA_UAC / FELUCCA_UAC_OUT are on. */
#ifndef FM1B_UAC_MIX_H
#define FM1B_UAC_MIX_H
#include <stdint.h>

/* o: n stereo frames of the render (24-bit, as the I2S takes them; at 4096 when full, else at gain); gain: MASTER's gain
 * (APP_GAIN), put on after the tap when full; master: MASTER's position 0..4096; play_q12: the computer's audio's level,
 * 0 muted .. 4096 as sent */
static inline void uac_mix(int32_t *o, uint32_t n, uint32_t gain, uint32_t master, int full, uint32_t play_q12)
{
#if FELUCCA_UAC
    uac_tap(o, n);                                  /* the USB audio input: the app's output, before the computer's */
#endif
#if FELUCCA_UAC_OUT
    uac_play(o, n, full ? play_q12 : (master * master >> 12) * play_q12 >> 12);   /* the computer's audio, at MASTER */
#else
    (void)master, (void)play_q12;
#endif
    if (full) {                                     /* the speaker and headphones: MASTER after the tap */
        const float g = (float)gain * (1.0f / 4096.0f);
        uint32_t i;
        for (i = 0; i < 2u * n; i++)
            o[i] = (int32_t)((float)o[i] * g);
    }
}
#endif
