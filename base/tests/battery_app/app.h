/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The battery test app (tests/run_tests.sh): an app built with APP_BATTERY_RAW, reading plat_battery_raw every frame
 * as an app showing its own volts does; the script checks what the app read (expect app_battery_raw) beside the
 * simulator's own (expect battery_raw). No sound, a black screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "BATTEST"
#define APP_SPLASH_RGB RGB(80, 200, 120)
#define APP_BLOCK 256u
#define APP_BATTERY_RAW
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
