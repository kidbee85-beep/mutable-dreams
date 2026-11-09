/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The Bluetooth devices test app: see app.h */
static const uint8_t MIDI_UUID[16] = {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,   /* 03B80E5A-EDE8-4B33-A751- */
                                      0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03};  /* 6CE34EC4C700, as on the air */
static struct plat_ble_peer peer;                  /* the device the app keeps (from its report) */
static int32_t reports, midi_reports, peer_rand = -1, peer_rssi = -1, name_ok, scanning, bad_rc = 9, connect_rc = 9;
static int32_t keys_seen;
static uint8_t have_peer;

/* (no libc on the device: the few byte loops the app needs) */
static int same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}
static void copy(void *d, const void *s, uint32_t n)
{
    uint8_t *o = d;
    const uint8_t *i = s;
    while (n--)
        *o++ = *i++;
}

/* an advertising PDU (2-byte header, AdvA, data): its AD structures; 1 when it offers the BLE-MIDI service */
static int midi_in(const uint8_t *pdu, uint32_t n, char *name, uint32_t name_max)
{
    uint32_t i = 8u, midi = 0;
    name[0] = 0;
    while (i + 1u < n && pdu[i] && i + 1u + pdu[i] <= n) {
        uint32_t len = pdu[i], type = pdu[i + 1u];
        if (type == 0x07u && len == 17u && same(pdu + i + 2u, MIDI_UUID, 16))
            midi = 1;
        if (type == 0x09u && len - 1u < name_max) {
            copy(name, pdu + i + 2u, len - 1u);
            name[len - 1u] = 0;
        }
        i += 1u + len;
    }
    return (int)midi;
}

void app_ble_scan_report(const uint8_t *pdu, uint32_t n, uint32_t rssi)
{
    char name[32];
    reports++;
    if (n < 8u || !midi_in(pdu, n, name, sizeof name))
        return;
    midi_reports++;
    name_ok = same((const uint8_t *)name, (const uint8_t *)"BLE MIDI", 9);
    if (!have_peer) {
        static const struct plat_ble_peer none;
        peer = none;
        copy(peer.addr, pdu + 2, 6);
        peer.addr_rand = (uint8_t)((pdu[0] >> 6) & 1u);   /* TxAdd */
        peer_rand = peer.addr_rand;
        peer_rssi = (int32_t)rssi;
        have_peer = 1;
    }
}
void app_ble_device_keys(const struct plat_ble_keys *k) { keys_seen += k != 0; }

void app_init(void) {}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) {}
void app_ui_init(void) { lcd_fill(0, 0, 240, 240, C_BLACK); }
void app_frame(void)
{
    if (plat_ble_state() != BLE_READY)
        return;
    if (!scanning) {
        plat_ble_scan(1);
        scanning = 1;
    }
    if (have_peer && midi_reports >= 3 && connect_rc == 9) {   /* (three reports first: they repeat) */
        struct plat_ble_peer nobody = peer;
        nobody.addr[0] ^= 0xFFu;
        bad_rc = plat_ble_connect_peer(&nobody);    /* an address no device has: not now */
        connect_rc = plat_ble_connect_peer(&peer);
    }
}
void app_input_only(void) {}
void app_say(const char *a, const char *b) { (void)a, (void)b; }
#ifdef PLAT_HOST                                   /* (the simulator's checks: not in the device build) */
int app_expect(const char *what, int *got)
{
    uint32_t code = 7;
    if (!strcmp(what, "reports"))
        *got = (int)reports;
    else if (!strcmp(what, "midi_reports"))
        *got = (int)midi_reports;
    else if (!strcmp(what, "peer_name_ok"))
        *got = (int)name_ok;
    else if (!strcmp(what, "peer_rand"))
        *got = (int)peer_rand;
    else if (!strcmp(what, "peer_rssi"))
        *got = (int)peer_rssi;
    else if (!strcmp(what, "connect_other_rc"))
        *got = (int)bad_rc;
    else if (!strcmp(what, "connect_rc"))
        *got = (int)connect_rc;
    else if (!strcmp(what, "keys_seen"))
        *got = (int)keys_seen;
    else if (!strcmp(what, "device_fail")) {
        *got = plat_ble_device_fail(&code);
        if (code)
            *got = 100 + (int)code;
    } else if (!strcmp(what, "device_flags"))
        *got = (int)plat_ble_device_flags();
    else
        return 0;
    return 1;
}
#endif
