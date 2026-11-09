/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Bluetooth MIDI devices on the own stack (fm1.toml ble_central = true with ble_stack = "own"): the FM-1 scans for
 * BLE-MIDI peripherals and connects to one as the central (bt/ble: ble_ll_scan, ble_scan.c's table, ble_central.c;
 * docs/bt/BLE-STACK.md §9). One link at a time: while connected to a device, a computer cannot connect to the FM-1
 * (JieLi's build keeps both). Its MIDI comes in as PORT_BLE from TO_BLE_DEV (cable 1, bt_own.c ble_app_midi_in).
 * Part of bt_own.c. */
static struct ble_scan_tab own_scan;
static uint32_t own_scan_aged;

_Static_assert(sizeof(struct plat_ble_peer) == sizeof(struct ble_peer) && sizeof(struct plat_ble_keys) ==
               sizeof(struct ble_keys), "plat.h plat_ble_peer / plat_ble_keys: bt/ble/ble.h ble_peer / ble_keys");
#if BLE_CEN_PAIR
#ifdef APP_BLE_PEERS
static struct ble_keys own_keys;                  /* a pairing's keys, for the app (own_central_poll, the main loop) */
static volatile uint8_t own_keys_new;
#endif
BLE_API void ble_app_central_keys(const struct ble_keys *k)
{
#ifdef APP_BLE_PEERS
    own_keys = *k;                                /* (the BLE interrupts' context: handed over in the main loop) */
    own_keys_new = 1;
#else
    (void)k;                                      /* (a device's bond is not kept: it pairs again next time) */
#endif
}
#endif

static void own_central_poll(void)                /* ble_poll: the scan's reports into the table */
{
    uint8_t pdu[40];
    uint16_t rssi;
    uint8_t n;
    while ((n = ble_ll_scan_take(pdu, &rssi)) != 0) {
        ble_scan_add(&own_scan, pdu, n, rssi, shim_ms());
#ifdef APP_BLE_PEERS
        app_ble_scan_report(pdu, n, rssi);        /* (the app's own list of the devices: plat.h) */
#endif
    }
#if BLE_CEN_PAIR && defined(APP_BLE_PEERS)
    if (own_keys_new) {
        struct plat_ble_keys k;
        uint32_t f = irq_save();                  /* (a copy the interrupts do not write meanwhile) */
        ble_cpy((uint8_t *)&k, (const uint8_t *)&own_keys, sizeof k);
        own_keys_new = 0;
        irq_restore(f);
        app_ble_device_keys(&k);
    }
#endif
    if (shim_ms() - own_scan_aged > 1000u) {
        own_scan_aged = shim_ms();
        ble_scan_age(&own_scan, own_scan_aged);
    }
}

void plat_ble_scan(int on)
{
    if (ble_state != BLE_READY)
        return;
    if (on)
        ble_scan_clear(&own_scan);
    own_held(1);
    ble_ll_scan(on);
    own_held(0);
}
int plat_ble_found(int i, char *name, int *rssi)
{
    uint8_t idx[BLE_SCAN_N];
    uint32_t n = ble_state == BLE_READY ? ble_scan_list(&own_scan, idx) : 0u, k;
    const struct ble_found *e;
    if (i < 0 || (uint32_t)i >= n)
        return 0;
    e = &own_scan.e[idx[i]];
    if (name) {
        for (k = 0; k < 20u && e->name[k]; k++)
            name[k] = e->name[k];
        name[k] = 0;
    }
    if (rssi)                                     /* (no gain table: the list's bars, as rough dBm) */
        *rssi = -100 + 20 * (int)ble_scan_bars(&own_scan, idx[i]);
    return 1;
}
int plat_ble_connect(int i)
{
    uint8_t idx[BLE_SCAN_N];
    uint32_t n = ble_state == BLE_READY ? ble_scan_list(&own_scan, idx) : 0u;
    struct ble_peer p;
    int ok;
    if (i < 0 || (uint32_t)i >= n)
        return -1;
    ble_zero((uint8_t *)&p, sizeof p);
    ble_cpy(p.addr, own_scan.e[idx[i]].addr, 6);
    p.addr_rand = own_scan.e[idx[i]].addr_rand;
    own_held(1);
    ok = ble_central_connect(&p);
    own_held(0);
    if (ok)
        shim_log("device: connecting");
    return ok ? 0 : -1;
}
#ifdef APP_BLE_PEERS                              /* (the devices one by one: an app that keeps its own, plat.h) */
int plat_ble_connect_peer(const struct plat_ble_peer *p)
{
    struct ble_peer q;
    int ok;
    if (ble_state != BLE_READY)
        return -1;
    ble_cpy((uint8_t *)&q, (const uint8_t *)p, sizeof q);
    own_held(1);
    ok = ble_central_connect(&q);
    own_held(0);
    if (ok)
        shim_log("device: connecting");
    return ok ? 0 : -1;
}
int plat_ble_device_fail(uint32_t *code)
{
    if (code)
        *code = ble_central_code();
    return ble_central_fail();
}
uint32_t plat_ble_device_flags(void)
{
#if BLE_CEN_PAIR
    return (ble_central_pairing() ? PLAT_BLE_DF_PAIRING : 0u) | (ble_central_prompted() ? PLAT_BLE_DF_PROMPTED : 0u) |
           (ble_central_mitm() ? PLAT_BLE_DF_MITM : 0u);
#else
    return 0;
#endif
}
int plat_ble_rpa_resolve(const uint8_t irk[16], const uint8_t addr[6])
{
#if BLE_CEN_PAIR
    return ble_rpa_resolve(irk, addr);
#else
    (void)irk, (void)addr;
    return 0;
#endif
}
#endif
void plat_ble_disconnect(void)
{
    if (ble_state != BLE_READY)
        return;
    own_held(1);
    ble_central_cancel();
    own_held(0);
}
int plat_ble_passkey(void)
{
#if BLE_CEN_PAIR
    return ble_state == BLE_READY && ble_central_passkey() != BLE_NO_PASSKEY ? (int)ble_central_passkey() : -1;
#else
    return -1;
#endif
}
int plat_ble_device(void)
{
    if (ble_state != BLE_READY)
        return BLE_DEV_OFF;
    switch (ble_central_state()) {
    case BLE_CS_CONNECTING:
        return BLE_DEV_CONNECTING;
    case BLE_CS_SETUP:
        return BLE_DEV_SEARCHING;
    case BLE_CS_READY:
        return BLE_DEV_READY;
    default:
        return ble_ll_scanning() ? BLE_DEV_SCANNING : BLE_DEV_OFF;
    }
}

/* ---- the USB remote (main_fm1.c; tools/fm1_remote.py dev ...): F0 7D 4C 44 cmd [arg] F7
 *   1 scan on  2 scan off  3 the devices found, a line each ("<i> <name>")  4 i: connect  5 disconnect
 *   6 the state ("<BLE_DEV_*> <fail> <code>") */
static void dev_line(const char *t)
{
    uint8_t f[48];
    uint32_t n = 0;
    f[n++] = 0xF0; f[n++] = 0x7D; f[n++] = 0x4C; f[n++] = 0x44; f[n++] = 0x10;
    while (*t && n < sizeof f - 1u)
        f[n++] = (uint8_t)(*t++ & 0x7Fu);
    f[n++] = 0xF7;
    ota_wire_send(f, n);
}
static int dev_frame(const uint8_t *p, uint32_t n)
{
    static const char *const ST[] = {"off", "scanning", "connecting", "searching", "ready"};
    static const uint8_t end[6] = {0xF0, 0x7D, 0x4C, 0x44, 0x11, 0xF7};
    char b[40], nm[24], *q;
    int i, rssi, st;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x44u)
        return 0;
    switch (p[3]) {
    case 1:
        plat_ble_scan(1);
        q = b;                                    /* (what the link layer did with it: our state, its state, enabled) */
        for (const char *t = "scan: "; *t; )
            *q++ = *t++;
        q = put_dec(q, ble_state);
        *q++ = ' ';
        q = put_dec(q, bll.state);
        *q++ = ' ';
        q = put_dec(q, bll.enabled);
        *q++ = ' ';
        put_dec(q, bll.scan_want);
        dev_line(b);
        break;
    case 2: plat_ble_scan(0); break;
    case 3:
        for (i = 0; plat_ble_found(i, nm, &rssi); i++) {
            q = put_dec(b, (uint32_t)i);
            *q++ = ' ';
            for (const char *t = nm; *t && q < b + sizeof b - 1; )
                *q++ = *t++;
            *q = 0;
            dev_line(b);
        }
        break;
    case 4: (void)plat_ble_connect(n >= 5u ? p[4] : 0); break;
    case 5: plat_ble_disconnect(); break;
    case 6:
        st = plat_ble_device();
        q = b;
        for (const char *t = st >= 0 && st <= BLE_DEV_READY ? ST[st] : "?"; *t; )
            *q++ = *t++;
        *q++ = ' ';
        q = put_dec(q, ble_central_fail());
        *q++ = ' ';
        q = put_dec(q, ble_central_code());
#if BLE_CEN_PAIR
        if (ble_central_passkey() != BLE_NO_PASSKEY) {   /* (a passkey pairing waits: the phone's user types it) */
            uint32_t k = ble_central_passkey(), d;
            for (const char *t = " passkey "; *t; )
                *q++ = *t++;
            for (d = 100000u; d; d /= 10u)
                *q++ = (char)('0' + k / d % 10u);
            *q = 0;
        }
#endif
        dev_line(b);
        break;
    default: break;
    }
    ota_wire_send(end, sizeof end);
    return 1;
}
