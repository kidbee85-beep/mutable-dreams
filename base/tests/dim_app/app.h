/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The dim-levels test app (tests/run_tests.sh): the dim lights at any brightness, plat_dim_ns, as an app uses it. Held
 * buttons set the pulse: FX 500 ns, SEL 1000, ENV 2000, LFO 9999 (over the top), EDIT 10 (under the bottom), GLO hands
 * the dim lights back to the glow, SAVE held asks the glow's low strength (otherwise the high one). Every button and
 * key is shown dim. No sound, a black screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "DIMTEST"
#define APP_SPLASH_RGB RGB(240, 200, 80)
#define APP_BLOCK 256u
#define APP_DIM_LEVELS

enum { OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
