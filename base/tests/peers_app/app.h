/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The Bluetooth devices test app (tests/run_tests.sh): an app with APP_BLE_PEERS keeping its own list, as an app with
 * remembered devices does: once Bluetooth is up it scans, takes each advertising report (app_ble_scan_report), and
 * connects to the first device that offers BLE-MIDI by its address once it has heard it three times
 * (plat_ble_connect_peer), after trying an address nobody has; the script checks what it saw (expect reports, peer_*,
 * connect_*). No sound, a black screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "PEERTEST"
#define APP_SPLASH_RGB RGB(80, 140, 240)
#define APP_BLOCK 256u
#define APP_BLE_PEERS
#define APP_HOST_EXPECT

enum { OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };            /* (the panel calibration, Bluetooth's address and pairing) */
#define APP_STORE_MAP {0xDC000u, 0xDE000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
int app_expect(const char *what, int *got);
void app_ble_scan_report(const uint8_t *pdu, uint32_t n, uint32_t rssi);
void app_ble_device_keys(const struct plat_ble_keys *k);
