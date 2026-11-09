/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The USB host test app (tests/run_tests.sh): the USB port as a MIDI host, as an app uses it (plat_usb_host, its
 * messages, plat_usb_lost, plat_usb_host_name). SEL tapped switches the port between device and host; every note in
 * goes out again an octave up to TO_USB (the computer, or the host's device). Its own checks for the script (expect):
 * app_msg (the last plat_usb_host_msg other than NONE), app_lost (plat_usb_lost's 1s), app_in (notes in), app_name
 * (plat_usb_host_name's length), app_host (what it asked). No sound, a black screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "USBHTEST"
#define APP_SPLASH_RGB RGB(80, 200, 240)
#define APP_BLOCK 256u
#define APP_USB_HOST_NAME
#define APP_HOST_EXPECT

enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
int app_expect(const char *what, int *got);
