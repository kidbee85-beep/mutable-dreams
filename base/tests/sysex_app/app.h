/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SysEx test app (tests/run_tests.sh): the platform's SysEx with the computer as an app sees it. It answers frames
 * F0 7D 53 58 cmd args F7 ("SX"); cmd 1 echo: the same frame back; 2 room: plat_sysex_room, plat_sysex_queued,
 * plat_usb_session (3 x 7 bits each, as the frame's three numbers); 3 flood N: N frames of 63 bytes at once, then the
 * number sent whole (plat_sysex_send 0); 4 stream N: a frame of 7 x N data bytes (0, 1, .. 127, 0 ..) streamed in puts of 7
 * (plat_sysex_begin, put, end), then its end's result (expect stream: 0 whole, 1 not).; 5 restart: plat_restart (expect boots: app_init's since the start). Nothing else: no sound, a black screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "SXTEST"
#define APP_SPLASH_RGB RGB(80, 160, 240)
#define APP_BLOCK 256u

enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* a SysEx frame from USB the platform does not take (p: the bytes between F0 and F7): 1 = the app's */
#define APP_SYSEX
#define APP_RESTART                      /* (plat_restart: cmd 5) */
int app_sysex(const uint8_t *p, uint32_t n);

/* every SysEx byte from USB as it comes (the USB interrupt): counted, for frames longer than app_sysex takes */
#define APP_SYSEX_BYTE
void app_sysex_byte(uint8_t b);

/* the host simulator's checks: frames (taken by the app), other (offered, not the app's),
 * bytes (seen by app_sysex_byte), starts (its F0s) */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
