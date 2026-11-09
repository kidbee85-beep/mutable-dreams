/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The plat_core test app (tests/run_tests.sh): an app with the second core (fm1.toml cpu2 = true) that hands half of
 * each render to it, as an app keeping a scratch buffer per core does, and asks plat_core in the render, in the job and
 * in the main loop; the script checks the answers (expect core_render, core_job, core_frame, jobs). No sound, a black
 * screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "CORETEST"
#define APP_SPLASH_RGB RGB(200, 120, 240)
#define APP_BLOCK 256u
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
