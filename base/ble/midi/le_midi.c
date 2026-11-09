/* SPDX-License-Identifier: MPL-2.0 AND Apache-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-ble and fm1-ctl)
 * Portions (the connection handling, advertising set-up and connection-parameter requests) come from the
 * JieLi AC79 SDK's apps/common/ble/le_trans_data.c, Copyright (c) 2011-2016 JIELI (Zhuhai Jieli Technology
 * Co., Ltd.), under the Apache License 2.0 (LICENSES/Apache-2.0.txt). Changed for fm1-ble: the SDK's
 * test profile replaced by the BLE MIDI service, its packet parsing and batching, and the advertising data.
 * The rest is MPL-2.0 (LICENSE). */
/* BLE MIDI peripheral on JieLi's BLE stack: the MIDI service (03B80E5A-EDE8-4B33-A751-6CE34EC4C700) with
 * its one characteristic (7772E5DB-3868-4112-A1A9-F2669D106BF3: read, write without response, notify),
 * advertised with the service UUID and the name "FM-1 1F2D" (ble_midi_name: four hex digits of this FM-1's own).
 * Built on the structure of the SDK's
 * le_trans_data.c (its event handling and connection-parameter requests), without its test profile.
 *
 *   bt_ble_init()                 the stack is up (BT_STATUS_INIT_OK): profile, advertising
 *   ble_midi_send_many(link, pkts, ms, n)   USB-MIDI packets (cin | status << 8 | d1 << 16 | d2 << 24), each
 *                                 with the millisecond it happened, to link 0, the central, as many as fit one
 *                                 notification; returns how many went; from the main loop (the stack's context)
 *   ble_midi_receive(pkt, ts)     the firmware's: every MIDI message written by the central, with the
 *                                 sender's millisecond it happened (13 bits: BLE MIDI's timestamp)
 *   ble_midi_connected(link)      link 0: a central is connected and listens (CCC on); link 1 (BLE_MIDI_CENTRAL):
 *                                 the MIDI device we connected to is ready
 *
 * BLE_MIDI_CENTRAL (fm1.toml ble_central = true): the FM-1 is a central too, at the same time: it scans for
 * MIDI devices (the service in their advertising), connects to the one asked for, finds its MIDI
 * characteristic and subscribes to it; what it notifies arrives as ble_midi_receive on cable 1 (0x10 in byte
 * 0), and ble_midi_send_many(1, ...) writes to it, each link in notifications of its own. The stack then runs
 * both roles (ble_stack_gatt_role 2, the SDK's multi-link calls,
 * one link each: TRANS_MULTI_BLE_EN in its config), after the SDK's apps/common/ble/multi_demo
 * (le_multi_common.c, le_multi_client.c), Copyright (c) JIELI, Apache-2.0.
 *   ble_midi_scan(on)  ble_midi_found(i, name, rssi)  ble_midi_connect(i)  ble_midi_disconnect()
 *   ble_midi_central_state()      BLE_MC_* below */
#include "system/app_core.h"
#include "system/includes.h"
#include "app_config.h"
#include "btstack/btstack_task.h"
#include "btstack/bluetooth.h"
#include "btcontroller_modules.h"
#include "bt_common.h"
#include "le_common.h"
#include "third_party/common/ble_user.h"

#define LOG_TAG_CONST       BLE_MIDI
#define LOG_TAG             "[BLE_MIDI]"
#define LOG_INFO_ENABLE
#include "debug.h"
const char log_tag_const_i_BLE_MIDI = 1;           /* the SDK keeps these in log_config; ours here */

#ifndef BLE_MIDI_CENTRAL
#define BLE_MIDI_CENTRAL 0
#endif
extern void ble_midi_receive(uint32_t pkt, uint32_t ts);   /* ts: the sender's millisecond (13 bits) */
extern uint32_t shim_ms(void);
extern void ble_midi_name(char *name);              /* "FM-1 " + four hex digits: this FM-1's (device/radio.c) */

/* ---- the GATT database (the SDK's binary form: size, flags, handle, uuid, value). Flags: bit 1 read,
 * bit 2 write without response, bit 3 write, bit 4 notify, 0x100 dynamic, 0x200 128-bit uuid. */
#define MIDI_SVC_UUID  0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03
#define MIDI_CHR_UUID  0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1, 0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77
#define H_NAME  0x0003
#define H_MIDI  0x0006
#define H_CCC   0x0007
static const uint8_t profile_data[] = {
    /* 0x0001 primary service 1800 (GAP) */
    0x0a, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0x00, 0x18,
    /* 0x0002 characteristic 2a00 (device name) read, dynamic; 0x0003 its value */
    0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x02, 0x03, 0x00, 0x00, 0x2a,
    0x08, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00, 0x2a,
    /* 0x0004 primary service MIDI */
    0x18, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x28, MIDI_SVC_UUID,
    /* 0x0005 characteristic MIDI: read | write without response | write | notify (0x1e); 0x0006 its value */
    0x1b, 0x00, 0x02, 0x00, 0x05, 0x00, 0x03, 0x28, 0x1e, 0x06, 0x00, MIDI_CHR_UUID,
    0x16, 0x00, 0x1e, 0x03, 0x06, 0x00, MIDI_CHR_UUID,
    /* 0x0007 client characteristic configuration */
    0x0a, 0x00, 0x0a, 0x01, 0x07, 0x00, 0x02, 0x29, 0x00, 0x00,
    0x00, 0x00,
};

#define ADV_INTERVAL_MIN     (160)              /* 0.625 ms units: 100 ms (the SDK demo: 500 ms) */
#define ATT_LOCAL_MTU_SIZE   (64)
#define ATT_SEND_CBUF_SIZE   (512 * (1 + BLE_MIDI_CENTRAL))   /* both links share it with the central */
#define ATT_RAM_BUFSIZE      (ATT_CTRL_BLOCK_SIZE + ATT_LOCAL_MTU_SIZE + ATT_SEND_CBUF_SIZE)
static u8 att_ram_buffer[ATT_RAM_BUFSIZE] __attribute__((aligned(4)));

static char gap_device_name[10];                 /* "FM-1 1F2D" (bt_ble_init): two FM-1s on one computer told apart */
#define GAP_NAME_LEN 9u
#define GAP_NAME_SHORT 4u                        /* "FM-1": what the advertising holds, its full name in the scan response */
#if BLE_MIDI_CENTRAL
#define ROLE_SERVER 0                            /* the SDK's MULTI_ROLE_SLAVE / MASTER */
#define ROLE_CLIENT 1
#define SEND(h, att, d, n, op) ble_op_multi_att_send_data((h), (att), (d), (n), (op))
#define CCC_GET(h, att) multi_att_get_ccc_config((h), (att))
#define CCC_SET(h, att, v) multi_att_set_ccc_config((h), (att), (v))
#else
#define SEND(h, att, d, n, op) ble_op_att_send_data((att), (d), (n), (op))
#define CCC_GET(h, att) att_get_ccc_config(att)
#define CCC_SET(h, att, v) att_set_ccc_config((att), (v))
#endif
static volatile hci_con_handle_t con_handle;
static u8 adv_data[ADV_RSP_PACKET_MAX], scan_rsp_data[ADV_RSP_PACKET_MAX];
static u8 ble_work_state, adv_ctrl_en;
static OS_MUTEX mutex;

/* connection parameters: ask for a short interval (MIDI latency and jitter): Apple's 11.25-15 ms for
 * MIDI first, then the SDK demo's */
static const uint8_t connection_update_enable = 1;
static uint8_t connection_update_cnt;
static const struct conn_update_param_t connection_param_table[] = {
    {9, 21, 0, 400},                /* 11.25..26.25 ms (1.25 ms units; Apple: max >= min + 15 ms), 4 s timeout */
    {9, 12, 0, 400},                /* 11.25..15 ms (BLE MIDI's own advice) */
    {12, 24, 0, 400},               /* 15..30 ms */
    {16, 32, 0, 400},
};
#define CONN_PARAM_TABLE_CNT (sizeof(connection_param_table) / sizeof(connection_param_table[0]))

static void set_ble_work_state(ble_state_e state)
{
    if (state != ble_work_state) {
        log_info("ble_work_st:%x->%x\n", ble_work_state, state);
        ble_work_state = state;
    }
}
static ble_state_e get_ble_work_state(void) { return ble_work_state; }

static void send_request_connect_parameter(u8 table_index)
{
    const struct conn_update_param_t *param = &connection_param_table[table_index];
    log_info("update_request:-%d-%d-%d-%d-\n", param->interval_min, param->interval_max, param->latency, param->timeout);
    if (con_handle)
        ble_op_conn_param_request(con_handle, (void *)param);
}
static void check_connetion_updata_deal(void)
{
    if (connection_update_enable && connection_update_cnt < CONN_PARAM_TABLE_CNT)
        send_request_connect_parameter(connection_update_cnt);
}
static void connection_update_complete_success(u8 *packet)
{
    int conn_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
    int conn_latency = hci_subevent_le_connection_update_complete_get_conn_latency(packet);
    int conn_timeout = hci_subevent_le_connection_update_complete_get_supervision_timeout(packet);
    printf("conn: interval %d latency %d\n", conn_interval, conn_latency);   /* 1.25 ms units, on the log */
}

/* ---- MIDI in: a BLE MIDI packet is a header, then (timestamp, message) pairs with running status */
static int midi_len(u8 st)                       /* data bytes of a status byte; -1 for sysex */
{
    switch (st & 0xF0) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    case 0xC0: case 0xD0: return 1;
    case 0xF0:
        switch (st) {
        case 0xF1: case 0xF3: return 1;
        case 0xF2: return 2;
        case 0xF0: return -1;
        default: return 0;                       /* F6, F7, F8..FF */
        }
    }
    return 0;
}
static u8 cin_of(u8 st, int n)
{
    if (st >= 0xF8) return 0xF;                  /* realtime */
    if (st < 0xF0) return st >> 4;
    return n == 2 ? 3 : n == 1 ? 2 : 5;          /* system common */
}
static void midi_parse(const u8 *b, u16 len, u32 cable)   /* cable: 0 the central's, 0x10 the device's */
{
    u16 i = 1;
    u8 st = 0, hi = b[0] & 0x3F, lo = 0, seen = 0;  /* the time: the header's high six bits, each message's low seven */
    u32 ts = 0;
    while (i < len) {
        if (b[i] & 0x80) {                       /* a timestamp low byte, then maybe a status */
            u8 l = b[i] & 0x7F;
            if (seen && l < lo)                  /* (lower than the one before: the next 128 ms) */
                hi = (u8)((hi + 1u) & 0x3Fu);
            lo = l;
            seen = 1;
            ts = (u32)hi << 7 | lo;
            i++;
            if (i < len && (b[i] & 0x80))
                st = b[i++];
        }
        if (!st)
            return;
        {
            int n = midi_len(st), k;
            u8 d[2] = {0, 0};
            if (n < 0) {                         /* sysex: skip to the end flag */
                while (i < len && !(b[i] & 0x80))
                    i++;
                continue;
            }
            if (st >= 0xF8) {
                ble_midi_receive(cable | 0xFu | (uint32_t)st << 8, ts);
                continue;
            }
            for (k = 0; k < n; k++) {
                if (i >= len || (b[i] & 0x80))
                    return;
                d[k] = b[i++];
            }
            ble_midi_receive(cable | (uint32_t)cin_of(st, n) | (uint32_t)st << 8 | (uint32_t)d[0] << 16 |
                             (uint32_t)d[1] << 24, ts);
        }
    }
}

/* ---- MIDI out: as many messages as fit one packet (header, then timestamp + message each) */
#if BLE_MIDI_CENTRAL
static volatile hci_con_handle_t mc_handle;      /* the device we connected to */
static u16 mc_value;                             /* its MIDI characteristic's value handle (0: not found yet) */
static volatile u8 mc_state;
static u16 mc_mtu = 20;                          /* its ATT payload, as send_mtu is the central's */
static int mc_ready(void) { return mc_handle && mc_value && mc_state == 4; }
#else
static int mc_ready(void) { return 0; }
#endif
static int periph_ready(void) { return con_handle && CCC_GET(con_handle, H_CCC); }
int ble_midi_connected(int link) { return link ? mc_ready() : periph_ready(); }
int ble_midi_peripheral_connected(void) { return periph_ready(); }
int ble_midi_peripheral_linked(void) { return con_handle != 0; }   /* listening or not yet */
static u16 send_mtu = 20;                        /* ATT payload: 23 - 3 until the exchange says more */
int ble_midi_send_many(int link, const uint32_t *pkts, const uint16_t *ms, int n)
{
    /* each message carries the millisecond it happened (ms[], 13 bits): a receiver that honours the
     * timestamps (CoreMIDI does) replays the original timing, however the radio bunched the packets.
     * Within one packet the times may not go backwards and span under 128 ms (the header holds the
     * first's high bits; a low byte smaller than the one before means the next 128 ms). */
    u8 buf[64], run = 0;
    u32 len = 0, remain = 0, t0, tp, mtu = send_mtu;
    int i;
    static u32 remain_max[2];
    if (!ble_midi_connected(link) || n <= 0)
        return 0;
    /* one notification in the stack at a time (a link's: each has its own buffer in the multi-link stack): the
     * radio sends about one a connection interval, so what piles up meanwhile goes together in the next one
     * (with its own timestamps) instead of as a queue of small ones that each wait for a slot */
#if BLE_MIDI_CENTRAL
    ble_op_multi_att_get_remain(link ? mc_handle : con_handle, &remain);
    if (link)
        mtu = mc_mtu;
#else
    ble_op_att_get_remain(&remain);
#endif
    if (remain > remain_max[link])
        remain_max[link] = remain;
    if (remain < remain_max[link] || remain < 8u)
        return 0;
    t0 = tp = ms[0] & 0x1FFFu;
    buf[len++] = (u8)(0x80 | ((t0 >> 7) & 0x3F));
    for (i = 0; i < n; i++) {
        u8 st = (u8)(pkts[i] >> 8), cin = pkts[i] & 0xF, m[4];
        u32 k = 0, t = ms[i] & 0x1FFFu;
        if (((t - tp) & 0x1FFFu) > 4096u)        /* earlier than the one before: as late as it */
            t = tp;
        if (((t - t0) & 0x1FFFu) > 120u)         /* the rest in the next packet */
            break;
        /* running status (channel messages only): the same status at the same time needs neither the
         * timestamp nor the status again, a new time only the timestamp */
        if (st < 0xF0 && st == run && i) {
            if (t != tp)
                m[k++] = (u8)(0x80 | (t & 0x7F));
        } else {
            m[k++] = (u8)(0x80 | (t & 0x7F));
            m[k++] = st;
        }
        if (cin == 0xF || cin == 5) {
        } else if (cin == 2 || cin == 0xC || cin == 0xD) {
            m[k++] = (u8)(pkts[i] >> 16);
        } else {
            m[k++] = (u8)(pkts[i] >> 16);
            m[k++] = (u8)(pkts[i] >> 24);
        }
        if (len + k > mtu || len + k > sizeof buf)
            break;
        memcpy(buf + len, m, k);
        len += k;
        tp = t;
        if (st < 0xF0)
            run = st;
        else if (st < 0xF8)
            run = 0;                             /* system common ends running status; real time does not */
    }
    if (i == 0)
        return 0;
#if BLE_MIDI_CENTRAL
    if (link)
        return SEND(mc_handle, mc_value, buf, len, ATT_OP_WRITE_WITHOUT_RESPOND) ? 0 : i;
#endif
    return SEND(con_handle, H_MIDI, buf, len, ATT_OP_AUTO_READ_CCC) ? 0 : i;
}

/* ---- ATT */
static uint16_t att_read_callback(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    uint16_t len = 0;
    switch (att_handle) {
    case H_NAME:
        len = GAP_NAME_LEN;
        if (offset >= len || offset + buffer_size > len)
            break;
        if (buffer) {
            memcpy(buffer, &gap_device_name[offset], buffer_size);
            len = buffer_size;
        }
        break;
    case H_MIDI:                                 /* the spec: a read returns nothing */
        len = 0;
        break;
    case H_CCC:
        if (buffer) {
            buffer[0] = (uint8_t)CCC_GET(connection_handle, att_handle);
            buffer[1] = 0;
        }
        len = 2;
        break;
    }
    return len;
}
static int att_write_callback(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    switch (att_handle) {
    case H_CCC:
        set_ble_work_state(BLE_ST_NOTIFY_IDICATE);
        check_connetion_updata_deal();
        log_info("ccc %02x\n", buffer[0]);
        CCC_SET(connection_handle, att_handle, buffer[0]);
        break;
    case H_MIDI:
        if (buffer_size >= 2)
            midi_parse(buffer, buffer_size, 0);
        break;
    }
    return 0;
}

#if BLE_MIDI_CENTRAL
/* ---- the central: MIDI devices around, one connected (after the SDK's le_multi_client.c) */
enum { BLE_MC_OFF, BLE_MC_SCANNING, BLE_MC_CONNECTING, BLE_MC_SEARCHING, BLE_MC_READY };   /* mc_state */
#define MC_MAX 8
typedef struct { char name[21]; u8 addr[6], addr_type; s8 rssi; } mc_dev_t;
static mc_dev_t mc_dev[MC_MAX];
static volatile u8 mc_n;
static u8 search_ram_buffer[512] __attribute__((aligned(4)));
static struct create_conn_param_t mc_conn;
static u8 mc_tries;                              /* connection attempts left (0x3E: the device never answered) */
static u32 mc_t0;                                /* when the connection was asked for (it has no timeout of its own) */
static u8 mc_pair;                               /* the device refused its notes: 1 pairing asked, 2 encrypted, asked again,
                                                  * 3 done (its notes on, or the link let go) */
static u8 mc_retry;                              /* asks since the link was encrypted */
static volatile u32 mc_due;                      /* when to ask for the notes again (0: not waiting) */
static void bt_ble_adv_enable(u8 enable);
static void mc_create(void)                      /* advertising paused meanwhile: its slots can crowd out the link's first */
{
    bt_ble_adv_enable(0);
    mc_state = BLE_MC_CONNECTING;
    mc_t0 = shim_ms();
    ble_op_create_connection(&mc_conn);
}
static const u8 midi_svc_le[16] = {MIDI_SVC_UUID}, midi_chr_le[16] = {MIDI_CHR_UUID};
static void mc_notes_on(void)                    /* the device's notes on: its CCC, the descriptor after the value */
{
    static u16 on = 1;
    int r = SEND(mc_handle, mc_value + 1, (u8 *)&on, 2, ATT_OP_WRITE);
    if (r)
        printf("ccc not sent %d\n", r);
}

static int uuid_is(const u8 *u, const u8 *le)    /* a 128-bit uuid as the stack reports it, either order */
{
    int i, fwd = 1, rev = 1;
    for (i = 0; i < 16; i++) {
        fwd &= u[i] == le[i];
        rev &= u[i] == le[15 - i];
    }
    return fwd || rev;
}

/* an advertising report: a device advertising the MIDI service goes in the list (its name from this report or
 * its scan response; the strongest reading kept) */
static void mc_adv(const adv_report_t *r)
{
    const u8 *d = r->data;
    u8 len = r->length, i = 0, midi = 0, nl = 0, k;
    const u8 *name = 0;
    while (i + 1u < len) {
        u8 l = d[i], t = d[i + 1];
        if (!l || i + 1u + l > len)
            break;
        if ((t == 0x06 || t == 0x07) && l >= 17)                 /* 128-bit service uuids */
            for (k = 0; k + 16u <= (u8)(l - 1); k += 16)
                midi |= (u8)uuid_is(&d[i + 2 + k], midi_svc_le);
        if (t == 0x08 || t == 0x09) {                            /* the name */
            name = &d[i + 2];
            nl = (u8)(l - 1);
        }
        i = (u8)(i + 1 + l);
    }
    for (k = 0; k < mc_n; k++)
        if (!memcmp(mc_dev[k].addr, r->address, 6))
            break;
    if (k == mc_n) {
        if (!midi || mc_n >= MC_MAX)
            return;                                              /* (a scan response alone does not add one) */
        memset(&mc_dev[k], 0, sizeof mc_dev[k]);
        memcpy(mc_dev[k].addr, r->address, 6);
        mc_dev[k].addr_type = r->address_type;
        mc_dev[k].rssi = r->rssi;
        mc_n++;
    }
    if (name) {
        if (nl > 20)
            nl = 20;
        memcpy(mc_dev[k].name, name, nl);
        mc_dev[k].name[nl] = 0;
    }
    if (r->rssi > mc_dev[k].rssi)
        mc_dev[k].rssi = r->rssi;
}

void ble_midi_scan(int on)                       /* main loop: start (the list starts empty) or stop scanning */
{
    if (on && mc_state == BLE_MC_OFF) {
        mc_n = 0;
        ble_op_set_scan_param(SCAN_ACTIVE, 48, 8);               /* 30 ms interval, 5 ms window (0.625 ms): the
                                                                  * radio free for a computer's link meanwhile */
        ble_op_scan_enable2(1, 0);
        mc_state = BLE_MC_SCANNING;
    } else if (!on && mc_state == BLE_MC_SCANNING) {
        ble_op_scan_enable2(0, 0);
        mc_state = BLE_MC_OFF;
    }
}
int ble_midi_found(int i, char *name, int *rssi)   /* device i of the scan: 0 when there is none */
{
    if (i < 0 || i >= mc_n)
        return 0;
    if (name) {
        memcpy(name, mc_dev[i].name[0] ? mc_dev[i].name : "MIDI device", 21);
        name[20] = 0;
    }
    if (rssi)
        *rssi = mc_dev[i].rssi;
    return 1;
}
int ble_midi_connect(int i)                      /* connect to device i of the scan */
{
    if (i < 0 || i >= mc_n || mc_handle || (mc_state != BLE_MC_SCANNING && mc_state != BLE_MC_OFF))
        return -1;
    if (mc_state == BLE_MC_SCANNING)
        ble_op_scan_enable2(0, 0);
    mc_conn.conn_interval = 24;                  /* 30 ms (1.25 ms units), as the SDK's; the device may ask for less */
    mc_conn.conn_latency = 0;
    mc_conn.supervision_timeout = 400;           /* 4 s */
    mc_conn.peer_address_type = mc_dev[i].addr_type;
    memcpy(mc_conn.peer_address, mc_dev[i].addr, 6);
    mc_tries = 3;
    mc_create();
    return 0;
}
void ble_midi_disconnect(void)
{
    if (mc_handle)
        ble_op_disconnect(mc_handle);
    else if (mc_state == BLE_MC_CONNECTING) {
        ble_op_create_connection_cancel();
        mc_state = BLE_MC_OFF;
    }
}
/* main loop: a connection the device has not answered within 5 s is given up (a device that stopped advertising,
 * or left), and so is a search not done 5 s after the link came up: the state goes back to off, the app says so */
void ble_midi_poll(void)
{
    if (mc_state == BLE_MC_CONNECTING && !mc_handle && shim_ms() - mc_t0 > 5000u) {
        printf("central: no answer\n");
        ble_op_create_connection_cancel();
        mc_state = BLE_MC_OFF;
        bt_ble_adv_enable(1);
    } else if (mc_state == BLE_MC_SEARCHING && mc_handle && shim_ms() - mc_t0 > 5000u) {
        printf("central: search timed out\n");
        mc_t0 = shim_ms();                       /* (once: the disconnection then turns the state off) */
        ble_op_disconnect(mc_handle);
    } else if (mc_state == BLE_MC_READY && mc_due && (int32_t)(shim_ms() - mc_due) >= 0) {
        mc_due = 0;
        mc_notes_on();
    } else if (mc_state == BLE_MC_READY && (mc_pair == 1 || mc_pair == 2) && shim_ms() - mc_t0 > 10000u) {
        printf("paired: no answer\n");          /* not encrypted, or its notes never turned on, 10 s after pairing was
                                                  * asked: a link without its notes is let go, so the screen says so */
        mc_pair = 3;
        ble_op_disconnect(mc_handle);
    }
}
int ble_midi_central_state(void) { return mc_state; }

/* the stack's search: each service and characteristic as found, then (void *)-1 at the end */
void user_client_report_search_result(search_result_t *r)
{
    if (r == (void *)-1) {
        if (mc_value) {
            mc_pair = 0;
            mc_retry = 0;
            mc_due = 0;
            mc_notes_on();
            mc_state = BLE_MC_READY;
            printf("central: MIDI ready %x\n", mc_value);
            bt_ble_adv_enable(1);                /* set up: the computer may find us again */
        } else {
            printf("central: no MIDI characteristic\n");
            ble_op_disconnect(mc_handle);
        }
        return;
    }
    if (!r->characteristic.uuid16 && uuid_is(r->characteristic.uuid128, midi_chr_le))
        mc_value = r->characteristic.value_handle;
}
void user_client_report_descriptor_result(charact_descriptor_t *d) { (void)d; }
/* the device's answers: the write that turns its notes on (an error a device that wants a paired link gives:
 * the FM-1 pairs, Just Works, and asks again), and the notes */
void user_client_report_data_callback(att_data_report_t *d)
{
    if (d->conn_handle != mc_handle)
        return;
    if (d->packet_type == GATT_EVENT_QUERY_COMPLETE && d->value_handle == mc_value + 1) {
        u8 st = d->blob_length ? d->blob[0] : 0;
        printf("ccc %x pair %d\n", st, mc_pair);
        if ((st == 0x05 || st == 0x08 || st == 0x0F) && !mc_pair) {   /* insufficient authentication, authorization,
                                                                        * encryption */
            mc_pair = 1;
            mc_t0 = shim_ms();
            sm_api_request_pairing(mc_handle);
        } else if ((st == 0x05 || st == 0x08 || st == 0x0F) && mc_pair == 2 && ++mc_retry < 6) {
            mc_due = shim_ms() + 700u;           /* an FP-30X still says so just after the encryption: asked again */
            if (!mc_due)
                mc_due = 1;
        } else if (st) {
            printf("ccc refused, let go\n");
            mc_pair = 3;
            ble_op_disconnect(mc_handle);
        } else if (mc_pair)
            mc_pair = 3;                         /* on: the 10 s give-up stands down */
        return;
    }
    if (d->packet_type == GATT_EVENT_NOTIFICATION && d->value_handle == mc_value && d->blob_length >= 2)
        midi_parse(d->blob, d->blob_length, 0x10u);
}
int l2cap_connection_update_request_just(u8 *packet, hci_con_handle_t handle) { (void)packet; (void)handle; return 0; }

static void client_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if (packet_type != HCI_EVENT_PACKET)
        return;
    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_ENCRYPTION_CHANGE:            /* paired as the device wanted: its notes asked for again */
        if (little_endian_read_16(packet, 3) != mc_handle || mc_pair != 1)
            break;
        printf("paired: %x\n", packet[2]);
        if (!packet[2]) {                        /* asked again a moment later, as an iPad does: the device's own
                                                  * side may not count the link as encrypted yet (an FP-30X refused
                                                  * a write sent at once); a failure waits for the stack's second
                                                  * try (pair_redo); ble_midi_poll lets the link go 10 s after
                                                  * pairing was asked if its notes are still off */
            mc_pair = 2;
            mc_retry = 0;
            mc_due = shim_ms() + 600u;
            if (!mc_due)
                mc_due = 1;
        }
        break;
    case GAP_EVENT_ADVERTISING_REPORT:
        if (mc_state == BLE_MC_SCANNING)
            mc_adv((const adv_report_t *)&packet[2]);
        break;
    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
            if (hci_subevent_le_connection_complete_get_role(packet))
                break;                           /* a central connected to us: the server's */
            if (packet[3]) {
                printf("central: connect failed %x\n", packet[3]);
                mc_state = BLE_MC_OFF;
                bt_ble_adv_enable(1);
                break;
            }
            /* advertising stays paused until the search is done: a computer that connected meanwhile (a Mac that
             * remembered the FM-1) stalled it, and the device never got as far as MIDI */
            mc_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
            mc_t0 = shim_ms();
            mc_value = 0;
            mc_mtu = 20;
            printf("central: connected %x\n", mc_handle);
            ble_op_multi_att_send_conn_handle(mc_handle, 0, ROLE_CLIENT);
            user_client_init(mc_handle, search_ram_buffer, sizeof search_ram_buffer);
            mc_state = BLE_MC_SEARCHING;
            ble_op_search_profile_all();
            break;
        }
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        if (little_endian_read_16(packet, 3) != mc_handle)
            break;
        printf("central: disconnected %x\n", packet[5]);
        ble_op_multi_att_send_conn_handle(0, 0, ROLE_CLIENT);
        mc_handle = 0;
        mc_value = 0;
        mc_due = 0;
        if (packet[5] == 0x3E && mc_state != BLE_MC_READY && mc_tries && --mc_tries) {
            printf("central: again (%d left)\n", mc_tries);
            mc_create();                         /* never established: try again */
            break;
        }
        mc_state = BLE_MC_OFF;
        bt_ble_adv_enable(1);
        break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        if (little_endian_read_16(packet, 2) == mc_handle) {
            u16 mtu = att_event_mtu_exchange_complete_get_MTU(packet) - 3;
            ble_op_multi_att_set_send_mtu(mc_handle, mtu);
            mc_mtu = (u16)(mtu > 64 ? 64 : mtu);
        }
        break;
    }
}

/* one HCI handler for both roles: each event to the role it belongs to (le_multi_common.c's way) */
static void cbk_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void both_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    int to_server = 1, to_client = 1;
    if (packet_type == HCI_EVENT_PACKET) {
        switch (hci_event_packet_get_type(packet)) {
        case GAP_EVENT_ADVERTISING_REPORT:
            to_server = 0;
            break;
        case HCI_EVENT_LE_META:
            if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_CONNECTION_COMPLETE ||
                hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE) {
                int slave = hci_subevent_le_connection_complete_get_role(packet);
                to_server = slave;
                to_client = !slave;
            }
            break;
        }
    }
    if (to_server)
        cbk_packet_handler(packet_type, channel, packet, size);
    if (to_client)
        client_packet_handler(packet_type, channel, packet, size);
}
#endif

/* ---- advertising */
static int make_set_adv_data(void)
{
    static const u8 svc[16] = {MIDI_SVC_UUID};
    u8 offset = 0, *buf = adv_data;
    offset += make_eir_packet_val(&buf[offset], offset, HCI_EIR_DATATYPE_FLAGS, 0x06, 1);
    offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_128BIT_SERVICE_UUIDS, (void *)svc, 16);
    /* 31 bytes: the flags and the service leave 8 for a name, so "FM-1" shortened here; a computer's scan reads
     * the whole name from the scan response */
    offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_SHORTENED_LOCAL_NAME, (void *)gap_device_name, GAP_NAME_SHORT);
    if (offset > ADV_RSP_PACKET_MAX)
        return -1;
    log_info("adv_data(%d)\n", offset);
    ble_op_set_adv_data(offset, buf);
    return 0;
}
static int make_set_rsp_data(void)
{
    u8 offset = 0, *buf = scan_rsp_data;
    offset += make_eir_packet_data(&buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)gap_device_name, GAP_NAME_LEN);
    ble_op_set_rsp_data(offset, buf);
    return 0;
}
static void advertisements_setup_init(void)
{
    ble_op_set_adv_param(ADV_INTERVAL_MIN, ADV_IND, ADV_CHANNEL_ALL);
    if (make_set_adv_data() | make_set_rsp_data())
        puts("advertisements_setup_init fail\n");
}
static int set_adv_enable(void *priv, u32 en)
{
    ble_state_e next_state = en ? BLE_ST_ADV : BLE_ST_IDLE, cur_state;
    if ((!adv_ctrl_en && en) || (en && con_handle))
        return APP_BLE_OPERATION_ERROR;
    cur_state = get_ble_work_state();
    switch (cur_state) {
    case BLE_ST_ADV: case BLE_ST_IDLE: case BLE_ST_INIT_OK: case BLE_ST_NULL: case BLE_ST_DISCONN:
        break;
    default:
        return APP_BLE_OPERATION_ERROR;
    }
    if (cur_state == next_state)
        return APP_BLE_NO_ERROR;
    log_info("adv_en:%d\n", en);
    set_ble_work_state(next_state);
    if (en)
        advertisements_setup_init();
    ble_op_adv_enable(en);
    return APP_BLE_NO_ERROR;
}
static void bt_ble_adv_enable(u8 enable) { set_adv_enable(0, enable); }

/* ---- the stack's events */
static void server_profile_start(u16 handle)
{
#if BLE_MIDI_CENTRAL
    ble_op_multi_att_send_conn_handle(handle, 0, ROLE_SERVER);
#else
    ble_op_att_send_init(handle, att_ram_buffer, ATT_RAM_BUFSIZE, ATT_LOCAL_MTU_SIZE);
#endif
    set_ble_work_state(BLE_ST_CONNECT);
}
static void cbk_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    int mtu;
    u32 tmp;
    u8 status;
    if (packet_type != HCI_EVENT_PACKET)
        return;
    switch (hci_event_packet_get_type(packet)) {
    case ATT_EVENT_CAN_SEND_NOW:
        break;
    case HCI_EVENT_LE_META:
        switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE:
            if (BLE_MIDI_CENTRAL && !hci_subevent_le_enhanced_connection_complete_get_role(packet))
                break;                           /* we are the central on this one */
            status = hci_subevent_le_enhanced_connection_complete_get_status(packet);
            if (status) {
                log_info("connection fail %x\n", status);
                set_ble_work_state(BLE_ST_DISCONN);
                break;
            }
            con_handle = hci_subevent_le_enhanced_connection_complete_get_connection_handle(packet);
            printf("computer: connected (enh) %x\n", con_handle);
            server_profile_start(con_handle);
            break;
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
            if (BLE_MIDI_CENTRAL && !hci_subevent_le_connection_complete_get_role(packet))
                break;                           /* we are the central on this one */
            if (packet[3])
                break;                           /* failed */
            con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
            printf("computer: connected %x\n", con_handle);
            connection_update_complete_success(packet + 8);
            server_profile_start(con_handle);
            break;
        case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
            if (little_endian_read_16(packet, 4) == con_handle)
                connection_update_complete_success(packet);
            break;
        }
        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        if (little_endian_read_16(packet, 3) != con_handle)
            break;
        printf("computer: disconnected %x\n", packet[5]);
#if BLE_MIDI_CENTRAL
        multi_att_clear_ccc_config(con_handle);
        ble_op_multi_att_send_conn_handle(0, 0, ROLE_SERVER);
        con_handle = 0;
#else
        con_handle = 0;
        ble_op_att_send_init(con_handle, 0, 0, 0);
#endif
        set_ble_work_state(BLE_ST_DISCONN);
        os_mutex_pend(&mutex, 0);
#if BLE_MIDI_CENTRAL
        if (mc_state != BLE_MC_CONNECTING && mc_state != BLE_MC_SEARCHING)   /* (the central resumes it when done) */
#endif
            bt_ble_adv_enable(1);
        os_mutex_post(&mutex);
        connection_update_cnt = 0;
        break;
    case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
        if (little_endian_read_16(packet, 2) != con_handle)
            break;
        mtu = att_event_mtu_exchange_complete_get_MTU(packet) - 3;
        log_info("ATT MTU = %u\n", mtu);
#if BLE_MIDI_CENTRAL
        ble_op_multi_att_set_send_mtu(con_handle, mtu);
#else
        ble_op_att_set_send_mtu(mtu);
#endif
        send_mtu = (u16)(mtu > 64 ? 64 : mtu);
        break;
    case L2CAP_EVENT_CONNECTION_PARAMETER_UPDATE_RESPONSE:
        if (little_endian_read_16(packet, 2) != con_handle)
            break;
        tmp = little_endian_read_16(packet, 4);
        printf("conn param %d: %s\n", connection_update_cnt, tmp ? "refused" : "accepted");   /* on the log */
        if (tmp) {
            connection_update_cnt++;
            check_connetion_updata_deal();
        } else {
            connection_update_cnt = CONN_PARAM_TABLE_CNT;
        }
        break;
    case HCI_EVENT_ENCRYPTION_CHANGE:
        printf("encryption %d on %x\n", packet[2], little_endian_read_16(packet, 3));
        break;
    }
}

static void cbk_sm_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    if (packet_type != HCI_EVENT_PACKET)
        return;
    switch (hci_event_packet_get_type(packet)) {
    case SM_EVENT_JUST_WORKS_REQUEST:
        sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
        log_info("sm: just works\n");
        break;
    case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
    case SM_EVENT_PASSKEY_DISPLAY_CANCEL:
    case SM_EVENT_PASSKEY_INPUT_NUMBER:
        break;
    case SM_EVENT_PAIR_PROCESS: {                /* on the log: 1 reconnect, 2 key missing, 3 failed, 4 timeout, 5 encryption failed */
        sm_just_event_t *e = (void *)packet;
        printf("pairing %x on %x\n", e->data[0], e->con_handle);
        break;
    }
    }
}

void ble_profile_init(void)
{
    printf("ble profile init (MIDI)\n");
    le_device_db_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING);
    sm_set_encryption_key_size_range(7, 16);
    sm_set_request_security(0);                  /* MIDI needs no pairing; a central may still ask */
#if BLE_MIDI_CENTRAL
    /* as a central: no pairing asked for (BLE MIDI needs none; an OXI E16 refuses it, and the service search did
     * not survive the attempt); Just Works if a device asks, and again when it remembers a bond we lost */
    sm_set_master_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_master_request_pair(0);
    sm_set_master_pair_redo(1);
#endif
    sm_event_callback_set(&cbk_sm_packet_handler);
#if BLE_MIDI_CENTRAL
    ble_stack_gatt_role(2);                      /* server and client */
#endif
    att_server_init(profile_data, att_read_callback, att_write_callback);
    att_server_register_packet_handler(cbk_packet_handler);
#if BLE_MIDI_CENTRAL
    gatt_client_init();
    gatt_client_register_packet_handler(client_packet_handler);
    hci_event_callback_set(&both_packet_handler);
    le_l2cap_register_packet_handler(&both_packet_handler);
    ble_vendor_set_default_att_mtu(ATT_LOCAL_MTU_SIZE);
    ble_op_multi_att_send_init(att_ram_buffer, ATT_RAM_BUFSIZE, ATT_LOCAL_MTU_SIZE);
#else
    hci_event_callback_set(&cbk_packet_handler);
    le_l2cap_register_packet_handler(&cbk_packet_handler);
    ble_vendor_set_default_att_mtu(ATT_LOCAL_MTU_SIZE);
#endif
}

void bt_ble_init(void)
{
    log_info("ble init (MIDI)\n");
    ble_midi_name(gap_device_name);
    if (!os_mutex_valid(&mutex))
        os_mutex_create(&mutex);
    set_ble_work_state(BLE_ST_INIT_OK);
    os_mutex_pend(&mutex, 0);
    adv_ctrl_en = 1;
    bt_ble_adv_enable(1);
    os_mutex_post(&mutex);
}
/* Bluetooth switched off or on by the player (the stack stays up): off ends every link, stops advertising and
 * scanning, so the radio goes quiet; on advertises again */
void ble_midi_enable(int on)
{
    os_mutex_pend(&mutex, 0);
    if (on) {
        adv_ctrl_en = 1;
        bt_ble_adv_enable(1);
        os_mutex_post(&mutex);
        return;
    }
    os_mutex_post(&mutex);
#if BLE_MIDI_CENTRAL
    ble_midi_scan(0);
    ble_midi_disconnect();
#endif
    bt_ble_exit();
}

void bt_ble_exit(void)
{
    os_mutex_pend(&mutex, 0);
    if (con_handle) {
        adv_ctrl_en = 0;
        ble_op_disconnect(con_handle);
    } else {
        bt_ble_adv_enable(0);
        adv_ctrl_en = 0;
    }
    os_mutex_post(&mutex);
}
u16 bt_ble_is_connected(void) { return con_handle; }
