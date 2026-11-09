/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The restart test app (tests/emu_reboot_test.mjs): letting go of FX restarts the FM-1 (plat_restart), SEL counts up a saved
 * value and saves it. The lit buttons show the saved value (bits 0..13), the lit keys show how many times app_init has
 * run since this RAM was cleared (key n = n starts): a restart that clears the RAM shows key 1 again. No sound, a
 * black screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "RBTEST"
#define APP_SPLASH_RGB RGB(120, 220, 160)
#define APP_BLOCK 256u
#define APP_RESTART

enum { OBJ_STATE, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
