/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-ble and fm1-ctl) */
/* A build without Bluetooth (FM1B_BLE 0, tools/build.py --no-ble): what radio.c's Bluetooth part gives the rest of
 * the firmware, with no Bluetooth behind it. Nothing from JieLi's BT libraries is linked. Included by radio.c (a
 * WiFi build) or radio_none.c. */
static void ble_start(void) {}
static void ble_poll(void) {}
static int ble_midi_in(uint32_t *pkt, uint32_t *from) { (void)pkt, (void)from; return 0; }
static void ble_midi_tap(uint32_t pkt, uint32_t to) { (void)pkt, (void)to; }
static uint32_t ble_midi_out_room(uint32_t to) { (void)to; return 0; }

/* ---- plat.h */
int plat_ble_state(void) { return BLE_OFF; }
int plat_ble_connected(void) { return 0; }
int plat_ble_linked(void) { return 0; }
int plat_ble_stream(const struct plat_ble_stream *s) { (void)s; return 0; }   /* (send by plat_midi_send) */
void plat_ble_link_info(uint32_t v[7])
{
    int i;
    for (i = 0; i < 7; i++)
        v[i] = 0;
}
void plat_ble_start(void) {}
void plat_ble_scan(int on) { (void)on; }
int plat_ble_found(int i, char *name, int *rssi) { (void)i, (void)name, (void)rssi; return 0; }
int plat_ble_connect(int i) { (void)i; return -1; }
void plat_ble_disconnect(void) {}
int plat_ble_device(void) { return BLE_DEV_OFF; }
int plat_ble_passkey(void) { return -1; }
#include "ble_peers_none.c"
void plat_ble_enable(int on) { (void)on; }
