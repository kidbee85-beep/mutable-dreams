/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-radio) */
/* WiFi (FM1B_WIFI): a station on JieLi's WiFi libraries (wl_wifi_sta_sfc, wpasupplicant, hsm: Apache-2.0, binary
 * only) and the SDK's lwIP, on the shim (radio.c, ble/shim, ble/wifi). On, off, a scan list, joining, and the
 * networks joined kept in the app's store object OBJ_WIFI (six, the most recent first), joined again at the next
 * plat_wifi_on. The app's network code runs in a task of its own (plat_net_task) and talks lwIP's sockets.
 * Part of the unity build (unity.c). */
#include "shim.h"

/* ---- the library's API (include_lib/net/wifi/wifi_connect.h) */
enum { STA_MODE = 1, AP_MODE, SMP_CFG_MODE, MP_TEST_MODE, P2P_MODE, NONE_MODE };
enum {
    WIFI_EVENT_MODULE_INIT, WIFI_EVENT_MODULE_START, WIFI_EVENT_MODULE_STOP,
    WIFI_EVENT_MODULE_START_ERR,
    WIFI_EVENT_AP_START, WIFI_EVENT_AP_STOP,
    WIFI_EVENT_STA_START, WIFI_EVENT_STA_STOP,
    WIFI_EVENT_STA_SCAN_COMPLETED,
    WIFI_EVENT_STA_CONNECT_SUCC,
    WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID,
    WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL,
    WIFI_EVENT_STA_CONNECT_ASSOCIAT_TIMEOUT,
    WIFI_EVENT_STA_DISCONNECT,
    WIFI_EVENT_SMP_CFG_START, WIFI_EVENT_SMP_CFG_STOP,
    WIFI_EVENT_SMP_CFG_TIMEOUT, WIFI_EVENT_SMP_CFG_COMPLETED,
    WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC,
    WIFI_EVENT_STA_NETWORK_STACK_DHCP_TIMEOUT,
};
struct wifi_store_info {
    int mode;
    u8 pwd[2][64];
    u8 ssid[2][33];
    int p2p_role;
    u8 sta_cnt;
    u8 connect_best_network;
} __attribute__((packed));
struct wifi_scan_ssid_info {
    char ssid[32];
    unsigned int ssid_len;
    unsigned char mac_addr[6];
    char rssi, snr, rssi_db, rssi_rsv;
    unsigned int channel_number;
    unsigned char SignalStrength, SignalQuality, SupportedRates[16];
    char enc;
};
struct netif_info { u32 ip, gw, netmask; };
extern int wifi_on(void);
extern int wifi_off(void);
extern int wifi_is_on(void);
extern int wifi_enter_sta_mode(char *sta_ssid, char *sta_pwd);
extern void wifi_set_event_callback(int (*cb)(void *, int));
extern void wifi_set_store_ssid_cnt(u32 cnt);
extern void wifi_set_sta_connect_timeout(int sec);
extern int wifi_set_default_mode(struct wifi_store_info *parm, char force, char store);
extern char wifi_get_rssi(void);
extern int wifi_scan_req(void);
extern struct wifi_scan_ssid_info *wifi_get_scan_result(u32 *ssid_cnt);
extern void wifi_clear_scan_result(void);
extern void free(void *p);                       /* (the shim's heap: wifi_get_scan_result's copy) */
extern void wifi_set_tx_rate_control_tab(u32 tab);
extern void lwip_get_netif_info(u8 is_wireless, struct netif_info *info);
extern void wifi_set_frame_cb(void (*cb)(void *rxwi, void *wh, void *data, u32 len, void *priv), void *priv);

/* reception: every frame the radio receives (anyone's), counted with its signal (RXWI word 2: RSSI0, bits 0-7;
 * fm1-radio's), a second at a time: plat_wifi_rx. A burst of the driver's "No BEACON" notes it in the log */
static volatile uint32_t fr_n;
static volatile int32_t fr_rssi;
static uint32_t rx_fps, rx_t0;
static int32_t rx_rssi;
static void wifi_frame_cb(void *rxwi, void *wh, void *data, u32 len, void *priv)
{
    const uint32_t *w = rxwi;
    (void)wh, (void)data, (void)len, (void)priv;
    if (!w)
        return;
    fr_n++;
    fr_rssi += (int8_t)w[2];
}
static void rx_second(void)                       /* wifi_poll: a second's frames */
{
    uint32_t now = shim_ms();
    if (now - rx_t0 < 1000u)
        return;
    rx_t0 = now;
    rx_fps = fr_n;
    rx_rssi = fr_n ? fr_rssi / (int32_t)fr_n : 0;
    fr_n = 0;
    fr_rssi = 0;
}
static void wifi_log_watch(const char *s)         /* radio.c shim_log: the first "No BEACON" of a burst, when */
{
    static uint32_t last;
    const char *p;
    for (p = s; *p && !(p[0] == 'N' && p[1] == 'o' && p[2] == ' ' && p[3] == 'B' && p[4] == 'E'); p++)
        ;
    if (!*p)
        return;
    if (shim_ms() - last > 5000u) {
        char b[40] = "beacons lost at ", *q = b + 16;
        uint32_t v[3] = {shim_ms() / 1000u, rx_fps, (uint32_t)-rx_rssi}, i, k;
        static const char *const L[3] = {" s, rx ", "/s ", ""};
        last = shim_ms();
        for (i = 0; i < 3u; i++) {
            char t[12];
            int n = 0;
            k = v[i];
            do {
                t[n++] = (char)('0' + k % 10u);
                k /= 10u;
            } while (k);
            if (i == 2u)
                *q++ = '-';
            while (n)
                *q++ = t[--n];
            for (p = L[i]; *p;)
                *q++ = *p++;
        }
        *q = 0;
        shim_log(b);
    }
    last = shim_ms();
}

/* the station's MAC (without one the library joins as 00:00:00:00:00:00): made from the chip's unique ID, so the
 * same every start, update and firmware (it was the BT address kept in syscfg, random and remade whenever the store
 * was: a router's list filled with "new" FM-1s); no ID, the BT address. A locally administered unicast one */
int init_net_device_mac_addr(char *macaddr, char ap_mode)
{
    const uint8_t *u = plat_chip_uid();
    unsigned i = 1;
    (void)ap_mode;
    while (u && i < 16u && u[i] == u[0])          /* (a part that does not answer 0x4B: all FF or all 0) */
        i++;
    if (u && i < 16u) {
        for (i = 0; i < 6u; i++)                  /* the ID's 16 bytes folded into 6 */
            macaddr[i] = (char)(u[i] ^ u[i + 6u] ^ (i < 4u ? u[i + 12u] : 0));
    } else {
        const uint8_t *b = bt_get_mac_addr();
        for (i = 0; i < 6u; i++)
            macaddr[i] = (char)b[i];
    }
    macaddr[0] = (char)(((uint8_t)macaddr[0] & 0xFC) | 0x02);
    macaddr[5] ^= 0x01;
    return 0;
}

/* ---- the networks joined (OBJ_WIFI), the most recent first */
#define WF_NET_N 6
typedef struct { char ssid[33], pass[64]; } wf_net_t;
static struct {
    uint32_t magic;
    wf_net_t n[WF_NET_N];
} wf_nets;
#define WF_NETS_MAGIC 0x57494632u                    /* "WIF2" */
static uint8_t wf_nets_loaded;

static int wf_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static void wf_cpy(char *d, const char *s, unsigned max)
{
    unsigned i;
    for (i = 0; i + 1u < max && s && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}
static void wf_nets_load(void)
{
    if (wf_nets_loaded)
        return;
    wf_nets_loaded = 1;
    memset(&wf_nets, 0, sizeof wf_nets);
    if (plat_store_load(OBJ_WIFI, &wf_nets, sizeof wf_nets) != (int)sizeof wf_nets || wf_nets.magic != WF_NETS_MAGIC) {
        memset(&wf_nets, 0, sizeof wf_nets);
        wf_nets.magic = WF_NETS_MAGIC;
    }
}
static void wf_nets_save(void) { plat_store_save(OBJ_WIFI, &wf_nets, sizeof wf_nets); }
static int wf_nets_count(void)
{
    int i;
    for (i = 0; i < WF_NET_N && wf_nets.n[i].ssid[0]; i++)
        ;
    return i;
}
static int wf_nets_find(const char *ssid)
{
    int i;
    for (i = 0; i < WF_NET_N && wf_nets.n[i].ssid[0]; i++)
        if (wf_eq(wf_nets.n[i].ssid, ssid))
            return i;
    return -1;
}
/* to the front (joined): the others move down, the oldest falls off */
static void wf_nets_front(const char *ssid, const char *pass)
{
    wf_net_t e;
    int i = wf_nets_find(ssid);
    memset(&e, 0, sizeof e);
    wf_cpy(e.ssid, ssid, sizeof e.ssid);
    wf_cpy(e.pass, pass, sizeof e.pass);
    if (i < 0)
        i = WF_NET_N - 1;
    for (; i > 0; i--)
        wf_nets.n[i] = wf_nets.n[i - 1];
    wf_nets.n[0] = e;
    wf_nets_save();
}

/* ---- the state */
static volatile uint8_t wifi_st;                  /* WIFI_* (plat.h) */
static uint8_t wifi_powered;                      /* wifi_on() done, wifi_off() not since */
static uint32_t wifi_ip;
static char wifi_ssid[33], wifi_pass[64];         /* the network joined / being joined */
static const char *wifi_why = "";                 /* WIFI_FAILED: what went wrong */
static int8_t wifi_rssi;
static int wifi_try;                              /* joining the saved networks: the one being tried */
static volatile uint8_t wifi_join_due, wifi_scan_done;
static uint32_t wifi_retry_at;                    /* all failed: the saved networks again then (0: none) */
/* which saved network to join: a scan first, then the strongest saved one it heard (not the saved list in order: a
 * network not around cost a 20 s attempt each, and the library refused every scan meanwhile, so the networks page
 * showed nothing); none heard, the saved ones in turn. wifi_tried: this round's tries (a wrong password, a network
 * that went: the next strongest) */
static volatile uint8_t wifi_pick;                /* a scan of ours, to choose from */
static uint32_t wifi_tried;
static uint32_t wifi_scan_retry_at;               /* a scan the library refused: asked again then (0: none) */
static uint8_t wifi_scan_tries;

static void wifi_failed(const char *why, uint32_t again_ms)
{
    wifi_st = WIFI_FAILED;
    wifi_why = why;
    wifi_retry_at = shim_ms() + again_ms;
    if (!wifi_retry_at)
        wifi_retry_at = 1;
}

static int wifi_scan_now(void)                    /* the library idle first: it refuses a scan while it joins */
{
    if (wifi_st == WIFI_JOINING || wifi_st == WIFI_FAILED)
        wifi_enter_sta_mode("", "");
    if (wifi_scan_req() == 0)
        return 0;
    wifi_scan_tries = 5;                          /* again each second, a few times */
    wifi_scan_retry_at = shim_ms() + 1000u;
    if (!wifi_scan_retry_at)
        wifi_scan_retry_at = 1;
    return -1;
}

static void wifi_pick_start(void)                 /* a scan, then the strongest saved network it heard */
{
    wifi_pick = 1;
    wifi_scan_now();
}

/* a join failed (or a dropped network's rejoin): the next strongest saved network a new scan hears; when none is
 * left, failed, and again in 30 s (a radio left on comes back when its network does) */
static void wifi_next(const char *why)
{
    wifi_why = why;
    if (wifi_try >= WF_NET_N) {                   /* joined by hand: no going on to the saved ones */
        wifi_failed(why, 30000u);
        return;
    }
    wifi_join_due = 3;                            /* (the main loop: the library's events come from its tasks) */
}

static void wifi_join(const char *ssid, const char *pass)
{
    wf_cpy(wifi_ssid, ssid, sizeof wifi_ssid);
    wf_cpy(wifi_pass, pass, sizeof wifi_pass);
    wifi_ip = 0;
    wifi_why = "";
    wifi_st = WIFI_JOINING;
    wifi_enter_sta_mode(wifi_ssid, wifi_pass);
}

static int wifi_event_cb(void *ctx, int event)
{
    char lb[24] = "wifi ev ";
    (void)ctx;
    lb[8] = (char)('0' + event / 10 % 10);
    lb[9] = (char)('0' + event % 10);
    lb[10] = 0;
    shim_log(lb);
    switch (event) {
    case WIFI_EVENT_MODULE_INIT: {                /* not started yet: the network it starts on */
        static struct wifi_store_info m;
        memset(&m, 0, sizeof m);
        m.mode = STA_MODE;
        wifi_try = 0;
        wifi_ssid[0] = 0;                         /* no network yet: a scan picks it (wifi_pick_start) */
        wifi_set_sta_connect_timeout(20);
        wifi_set_default_mode(&m, 1, 0);
        break;
    }
    case WIFI_EVENT_MODULE_START:                 /* the send rates allowed: every one, 1M CCK .. MCS7, as the SDK's
                                                   * demo_wifi, unless the app keeps to fewer (APP_WIFI_TX_RATES, bit
                                                   * 0 1M CCK .. bit 19 MCS7: the SDK's demo_audio lists them) */
#ifndef APP_WIFI_TX_RATES
#define APP_WIFI_TX_RATES 0xFFFFFu
#endif
        wifi_set_tx_rate_control_tab(APP_WIFI_TX_RATES);
        break;
    case WIFI_EVENT_STA_START:
        if (wifi_st == WIFI_STARTING) {
            wifi_st = WIFI_ON;
            wifi_tried = 0;
            if (wf_nets_count())
                wifi_join_due = 3;                /* the main loop: a scan, then the strongest saved network */
        }
        break;
    case WIFI_EVENT_STA_SCAN_COMPLETED:
        wifi_scan_done = 1;
        break;
    case WIFI_EVENT_STA_CONNECT_SUCC:
        wifi_st = WIFI_ADDRESS;
        break;
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_SUCC: {
        struct netif_info ni = {0};
        lwip_get_netif_info(1, &ni);
        wifi_ip = ni.ip;
        wifi_st = WIFI_CONNECTED;
        wifi_join_due = 2;                        /* the main loop: this network to the front */
        wifi_try = 0;                             /* (where it will be: if it drops, the others after it) */
        wifi_retry_at = 0;
        break;
    }
    case WIFI_EVENT_STA_DISCONNECT:
        if (wifi_st == WIFI_CONNECTED || wifi_st == WIFI_ADDRESS) {
            wifi_st = WIFI_JOINING;               /* the library joins again by itself */
            wifi_ip = 0;
        }
        break;
    case WIFI_EVENT_STA_CONNECT_TIMEOUT_NOT_FOUND_SSID:
        wifi_next("not found");
        break;
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_FAIL:
    case WIFI_EVENT_STA_CONNECT_ASSOCIAT_TIMEOUT:
        wifi_next("could not join");             /* (a wrong password, or a network that went) */
        break;
    case WIFI_EVENT_STA_NETWORK_STACK_DHCP_TIMEOUT:
        wifi_next("no address");
        break;
    case WIFI_EVENT_MODULE_START_ERR:
        wifi_st = WIFI_FAILED;
        wifi_why = "radio did not start";
        break;
    default:
        break;
    }
    return 0;
}

/* ---- the scan list */
#define WF_SCAN_N 12
typedef struct { char ssid[33]; int8_t rssi; uint8_t secured, channel, missed; } wf_scan_t;
static wf_scan_t wf_scan_list[WF_SCAN_N];
static int wf_scan_n;
static uint32_t wifi_scans;                       /* scans collected (plat_wifi_scans) */

/* after WIFI_EVENT_STA_SCAN_COMPLETED (a scan of ours, or a join's): the results, strongest first, each network
 * once (the strongest of its access points); then the ones the last scans heard and this one missed, with their
 * last signal, until three scans in a row miss them (a scan hears a channel ~100 ms: a weak network's beacon, lost
 * now and then, took it off the list) */
#define WF_SCAN_KEEP 3u
static void wifi_scan_collect(void)
{
    u32 n = 0, i;
    struct wifi_scan_ssid_info *r = wifi_get_scan_result(&n);
    wf_scan_t old[WF_SCAN_N];
    int old_n = wf_scan_n, o;
    memcpy(old, wf_scan_list, sizeof old);
    wf_scan_n = 0;
    for (i = 0; r && i < n && wf_scan_n < WF_SCAN_N; i++) {
        wf_scan_t *s = &wf_scan_list[wf_scan_n];
        unsigned k, len = r[i].ssid_len < 32u ? r[i].ssid_len : 32u;
        int j;
        if (!len)
            continue;                             /* hidden */
        for (k = 0; k < len; k++)
            s->ssid[k] = r[i].ssid[k];
        s->ssid[k] = 0;
        for (j = 0; j < wf_scan_n && !wf_eq(wf_scan_list[j].ssid, s->ssid); j++)
            ;
        if (j < wf_scan_n)
            continue;
        s->rssi = (int8_t)r[i].rssi;
        s->secured = r[i].enc != 0;
        s->channel = (uint8_t)r[i].channel_number;
        s->missed = 0;
        for (j = wf_scan_n; j > 0 && wf_scan_list[j - 1].rssi < s->rssi; j--) {
            wf_scan_t t = wf_scan_list[j];
            wf_scan_list[j] = wf_scan_list[j - 1];
            wf_scan_list[j - 1] = t;
        }
        wf_scan_n++;
    }
    for (o = 0; o < old_n && wf_scan_n < WF_SCAN_N; o++) {   /* the missed ones, after */
        int j;
        if (old[o].missed + 1u > WF_SCAN_KEEP)
            continue;
        for (j = 0; j < wf_scan_n && !wf_eq(wf_scan_list[j].ssid, old[o].ssid); j++)
            ;
        if (j < wf_scan_n)
            continue;
        wf_scan_list[wf_scan_n] = old[o];
        wf_scan_list[wf_scan_n].missed++;
        wf_scan_n++;
    }
    if (wifi_pick) {                              /* the strongest saved network heard now, not tried this round */
        int k, best = -1;
        wifi_pick = 0;
        for (k = 0; k < wf_scan_n && best < 0; k++) {
            int s = wf_scan_list[k].missed ? -1 : wf_nets_find(wf_scan_list[k].ssid);
            if (s >= 0 && s < 32 && !(wifi_tried >> s & 1u))
                best = s;
        }
        for (k = 0; k < wf_nets_count() && best < 0; k++)   /* none heard: the saved ones in turn (a hidden network
                                                             * is never in a scan; a weak one not in every one) */
            if (!(wifi_tried >> k & 1u))
                best = k;
        if (best >= 0) {
            wifi_try = best;
            wifi_tried |= 1u << best;
            wifi_join(wf_nets.n[best].ssid, wf_nets.n[best].pass);
        } else {
            wifi_failed(wifi_tried ? wifi_why : "not found", wifi_tried ? 30000u : 15000u);
        }
    }
    if (r) {
        wifi_clear_scan_result();
        free(r);                                  /* the results are a copy of the caller's (the SDK's demos free it
                                                   * too): kept, every scan leaked 2.3 KB, and the library's own
                                                   * scans while connected ran the heap out overnight */
    }
    wifi_scans++;
}

/* the main loop's turn: timers, every task that can run, then what the events left to do */
static void wifi_poll(void)
{
    if (wifi_st == WIFI_OFF)
        return;
    shim_timers();
    shim_run();
    radio_stuck_check();
    radio_settle(wifi_st >= WIFI_ON);
    if (wifi_st == WIFI_FAILED && wifi_retry_at && (int32_t)(shim_ms() - wifi_retry_at) >= 0 && wf_nets_count()) {
        wifi_retry_at = 0;                        /* the saved networks again: a new round */
        wifi_try = 0;
        wifi_tried = 0;
        wifi_join_due = 3;
    }
    if (wifi_scan_retry_at && (int32_t)(shim_ms() - wifi_scan_retry_at) >= 0) {
        wifi_scan_retry_at = 0;
        if (wifi_scan_req() == 0) {
            wifi_scan_tries = 0;
        } else if (wifi_scan_tries && --wifi_scan_tries) {
            wifi_scan_retry_at = shim_ms() + 1000u;
        } else if (wifi_pick) {                   /* the library would not scan: no choice, try again later */
            wifi_pick = 0;
            wifi_failed("no scan", 15000u);
        }
    }
    if (wifi_join_due == 3) {
        wifi_join_due = 0;
        wifi_pick_start();
    } else if (wifi_join_due == 2) {
        extern void shim_stacks_release(void);
        wifi_join_due = 0;
        wf_nets_front(wifi_ssid, wifi_pass);         /* joined: kept, the most recent first */
        shim_stacks_release();                    /* and the last session's tasks, which ended after this one began,
                                                   * their stacks back (~16 KB after a WiFi off and on) */
    }
    rx_second();
    if (wifi_st == WIFI_CONNECTED && wifi_is_on())
        wifi_rssi = (int8_t)wifi_get_rssi();
    if (wifi_scan_done) {
        wifi_scan_done = 0;
        wifi_scan_collect();
    }
}

/* ---- plat.h */
void plat_wifi_on(int on)
{
    wf_nets_load();
    if (on && wifi_st == WIFI_OFF) {
        static uint8_t first = 1;
        wifi_ip = 0;
        wifi_ssid[0] = 0;
        wifi_why = "";
        wifi_rssi = 0;
        wifi_st = WIFI_STARTING;
        {
            extern void shim_stacks_release(void);
            shim_stacks_release();                /* (the last session's ended tasks' stacks back: os.c) */
        }
        radio_begin("WiFi: starting");
        if (first) {
            first = 0;
            wifi_set_store_ssid_cnt(1);
            wifi_set_event_callback(wifi_event_cb);
        }
        wifi_powered = 1;
        if (wifi_on())
            shim_log("WiFi: wifi_on failed");
        wifi_set_frame_cb(wifi_frame_cb, 0);
    } else if (!on && wifi_st != WIFI_OFF) {
        wifi_st = WIFI_OFF;
        wifi_ip = 0;
        if (wifi_powered) {
            wifi_powered = 0;
            wifi_off();
            shim_log("WiFi: off");
        }
        radio_settle(1);                          /* off while starting: the interrupts back (radio.c) */
        radio_clean_exit();
    }
}
int plat_wifi_state(void) { return wifi_st; }
const char *plat_wifi_why(void) { return wifi_st == WIFI_FAILED ? wifi_why : ""; }
const char *plat_wifi_ssid(void) { return wifi_ssid; }
uint32_t plat_wifi_ip(void) { return wifi_st == WIFI_CONNECTED ? wifi_ip : 0; }
int plat_wifi_rssi(void) { return wifi_st == WIFI_CONNECTED ? wifi_rssi : 0; }
int plat_wifi_scan(void)                         /* (failed: the radio is still on) */
{
    if (wifi_st < WIFI_ON)
        return -1;
    return wifi_scan_now() == 0 || wifi_scan_retry_at ? 0 : -1;   /* (refused now: asked again shortly) */
}
int plat_wifi_found(int i, char *ssid, int *rssi, int *secured)
{
    if (i < 0 || i >= wf_scan_n)
        return 0;
    if (ssid)
        wf_cpy(ssid, wf_scan_list[i].ssid, 33);
    if (rssi)
        *rssi = wf_scan_list[i].rssi;
    if (secured)
        *secured = wf_scan_list[i].secured;
    return 1;
}
int plat_wifi_found_channel(int i) { return i >= 0 && i < wf_scan_n ? wf_scan_list[i].channel : 0; }
int plat_wifi_join(const char *ssid, const char *pass)
{
    int i;
    wf_nets_load();
    if (wifi_st == WIFI_OFF || wifi_st == WIFI_STARTING || !ssid || !ssid[0])
        return -1;
    i = wf_nets_find(ssid);
    if (!pass)
        pass = i >= 0 ? wf_nets.n[i].pass : "";
    wifi_try = WF_NET_N;                             /* (by hand: no going on to the saved ones) */
    wifi_retry_at = 0;
    wifi_join(ssid, pass);
    return 0;
}
int plat_wifi_saved(int i, char *ssid)
{
    wf_nets_load();
    if (i < 0 || i >= wf_nets_count())
        return 0;
    if (ssid)
        wf_cpy(ssid, wf_nets.n[i].ssid, 33);
    return 1;
}
void plat_wifi_forget(int i)
{
    wf_nets_load();
    if (i < 0 || i >= wf_nets_count())
        return;
    for (; i < WF_NET_N - 1; i++)
        wf_nets.n[i] = wf_nets.n[i + 1];
    memset(&wf_nets.n[WF_NET_N - 1], 0, sizeof wf_nets.n[0]);
    wf_nets_save();
}
int plat_wifi_kept(const char *ssid)
{
    wf_nets_load();
    return ssid && wf_nets_find(ssid) >= 0;
}
uint32_t plat_wifi_scans(void) { return wifi_scans; }
void plat_wifi_rx(uint32_t *frames, int *rssi)
{
    if (frames)
        *frames = wifi_st >= WIFI_ON ? rx_fps : 0;
    if (rssi)
        *rssi = wifi_st >= WIFI_ON ? (int)rx_rssi : 0;
}
int plat_net_task(void (*fn)(void *), void *arg, const char *name) { return task_create(fn, arg, name); }
