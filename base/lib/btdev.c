/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from CTL-1 and the template) */
/* Bluetooth MIDI devices for an app's page: lib/btdev.h. Part of the unity build (device/unity.c, host/plat_host.c)
 * when the app defines APP_BTDEV. */
#include "plat.h"
#include "btdev.h"

static struct {
    uint8_t pick, up, was_dev, was_comp, q_r, q_w, letting_go;   /* letting_go: SAVE did it, not lost */
    uint8_t q[8];
    char name[21];                       /* the device asked to connect: kept, the scan's list empties */
} bd;

static void bd_put(int ev)
{
    if ((uint8_t)(bd.q_w - bd.q_r) < sizeof bd.q)
        bd.q[bd.q_w++ % sizeof bd.q] = (uint8_t)ev;
}

int btdev_found(int i, char *name, int *rssi) { return FM1B_BLE_CENTRAL && plat_ble_found(i, name, rssi); }
int btdev_count(void)
{
    int n = 0;
    while (n < 8 && btdev_found(n, 0, 0))
        n++;
    return n;
}
int btdev_pick(void) { return bd.pick; }
void btdev_choose(int i)
{
    int n = btdev_count();
    bd.pick = (uint8_t)(i < 0 || n == 0 ? 0 : i >= n ? n - 1 : i);
}
int btdev_busy(void)
{
    int d = plat_ble_device();
    return d == BLE_DEV_CONNECTING || d == BLE_DEV_SEARCHING || d == BLE_DEV_READY;
}
int btdev_ready(void) { return plat_ble_device() == BLE_DEV_READY; }
const char *btdev_name(void) { return bd.name; }

void btdev_page(int up)
{
    /* A central may send MIDI writes without subscribing to our notifications.
     * Track the physical link so that its loss releases notes and controllers,
     * even for a write-only sender. Listening describes MIDI output readiness. */
    int d = plat_ble_device(), c = plat_ble_linked();
    if (d != bd.was_dev) {               /* a device connected, given up or lost */
        if (d == BLE_DEV_READY)
            bd_put(BTDEV_CONNECTED);
        else if (d == BLE_DEV_OFF && (bd.was_dev == BLE_DEV_CONNECTING || bd.was_dev == BLE_DEV_SEARCHING))
            bd_put(BTDEV_NO_ANSWER);
        else if (d == BLE_DEV_OFF && bd.was_dev == BLE_DEV_READY && !bd.letting_go)
            bd_put(BTDEV_LOST);
        if (d == BLE_DEV_OFF)
            bd.letting_go = 0;
        bd.was_dev = (uint8_t)d;
    }
    if (c != bd.was_comp) {
        bd_put(c ? BTDEV_COMPUTER : BTDEV_COMPUTER_GONE);
        bd.was_comp = (uint8_t)c;
    }
    up = FM1B_BLE_CENTRAL && up && plat_ble_state() == BLE_READY;
    if (up && d == BLE_DEV_OFF) {        /* the page is up and nothing is connected: look */
        if (!bd.up)
            bd.pick = 0;
        plat_ble_scan(1);
    } else if (!up && d == BLE_DEV_SCANNING) {
        plat_ble_scan(0);
    }
    bd.up = (uint8_t)up;
}

int btdev_event(void) { return bd.q_r == bd.q_w ? BTDEV_NONE : bd.q[bd.q_r++ % sizeof bd.q]; }

const char *btdev_word(int ev)
{
    static const char *const W[] = {"", "CONNECTING", "CONNECTED", "NO ANSWER", "DEVICE LOST", "DISCONNECTED",
                                    "NO DEVICE", "COMPUTER", "COMPUTER GONE"};
    return ev >= 0 && ev < (int)(sizeof W / sizeof W[0]) ? W[ev] : "";
}

int btdev_save(void)
{
    if (btdev_busy()) {
        bd.letting_go = 1;
        plat_ble_disconnect();
        return BTDEV_LET_GO;
    }
    if (btdev_found(bd.pick, bd.name, 0) && plat_ble_connect(bd.pick) == 0)
        return BTDEV_CONNECTING;
    return BTDEV_NO_DEVICE;
}

void btdev_title(char *t)
{
    const char *a = btdev_ready() ? "Connected: " : btdev_busy() ? "Connecting: " : 0, *b = bd.name;
    char *q = t;
    if (!a) {
        a = btdev_count() ? "MIDI devices" : "Looking for MIDI devices";
        b = "";
    }
    while (*a)
        *q++ = *a++;
    while (*b && q < t + 31)
        *q++ = *b++;
    *q = 0;
}
