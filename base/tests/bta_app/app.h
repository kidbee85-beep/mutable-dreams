/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* A test app for Bluetooth audio's calls (plat.h plat_bta_*) in the host simulator: tests/bta_app/tests/bta.txt */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "BTA TEST"
#define APP_SPLASH_RGB RGB(80, 160, 240)
#define APP_BLOCK 256u
/* what Bluetooth keeps: in the app's own object (APP_BLE_STORE, plat.h struct plat_ble_rec), not OBJ_BLE */
#define APP_BLE_STORE
enum { OBJ_PANEL, OBJ_BTREC, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u, 0xDE000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
