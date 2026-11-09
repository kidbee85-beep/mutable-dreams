/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* fm1-base's platform interface (from X0X's, by way of FoMni-1): everything the app needs from the hardware.
 *
 * The device implementation (plat_fm1.c) wraps Felucca's HAL and panel map; the
 * host implementation (host/plat_host.c) is a simulator fed by a script, which is
 * what lets the whole app — UI, sequencer, engines — run and be tested on a Mac.
 * Drawing goes through Felucca's lcd_fill / lcd_blit (lcd.c on the device, a
 * framebuffer on the host) and gfx.c on top. */
#pragma once
#include <stdint.h>

/* big zero-initialised buffers go in the .pool RAM region on the device */
#ifdef PLAT_HOST
#define PLAT_POOL
#else
#define PLAT_POOL __attribute__((section(".pool")))
#endif

/* PLAT_RAM_CODE: a function that runs from RAM (section .app_ram, copied at boot with .ram_text, at the start of
 * the RAMTEXT region they share: 32 KiB, 24 KiB with WiFi; with fm1.toml ram_layout = "packed", device/app_packed.ld,
 * at the start of the packed SRAM, as big as its one budget lets it), out of the way
 * of the XIP cache's misses: an audio path. It may call
 * only RAM code (other PLAT_RAM_CODE functions, felucca-bsp's RAM functions) and nothing through a pointer; the build
 * checks it over the disassembly, to any depth (tools/ramcode.py; fm1.toml ram_code_allow names the exceptions an app
 * vouches for). Not for code that runs with the flash off: it may read constants in the flash. XIP code it may not
 * call: the compiler's own calls (memcpy for a struct copy, a 64-bit divide), and a static inline helper the compiler
 * keeps out of line (at -Os it often does: fastmath.h's fm_exp2f), whose one copy is in XIP; a helper RAM code uses
 * is always_inline, or PLAT_RAM_CODE itself.
 * PLAT_FAR(fn): fn through a pointer the compiler cannot turn back into a direct call. RAM code and XIP code are
 * mostly farther apart than a direct call reaches (+-4 MiB), so XIP code calls RAM code this way (the linker refuses
 * a direct call that does not reach: "relocation truncated to fit"); a call from RAM code into XIP this way is one
 * the check refuses, unless allowed. The host: plain calls. */
#ifdef PLAT_HOST
#define PLAT_RAM_CODE
#define PLAT_FAR(fn) (&(fn))
#else
#define PLAT_RAM_CODE __attribute__((section(".app_ram")))
static inline __attribute__((always_inline)) void *plat_far_ptr(void *fn)
{
    void *volatile held = fn;           /* read back from memory: the compiler no longer knows the target */
    return held;
}
#define PLAT_FAR(fn) ((__typeof__(&(fn)))plat_far_ptr((void *)&(fn)))
#endif

/* panel controls, by printed label (Felucca's panel.c map turns these into matrix ids; Felucca
 * calls the second button SCL, the panel prints SEL) */
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

#define NKEYS 27                       /* F3 .. G5 */
/* key k by colour: the 16 white keys and the 11 black keys, left to right (initialisers for a uint8_t array) */
#define PLAT_WHITE_KEYS {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26}
#define PLAT_BLACK_KEYS {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25}

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

uint32_t plat_ms(void);
uint32_t plat_buttons(void);           /* held: bit B_* */
uint32_t plat_keys(void);              /* held: bit k = key k (0 = F3) */
int32_t plat_enc(int role);            /* detents since the last call, + = clockwise; EN_* */
uint32_t plat_master(void);            /* MASTER pot's position, 0..4096 (the render gets it as a gain: APP_GAIN) */
int plat_battery(void);                /* the battery: 0 (empty) .. 3 (full), 4 a USB host powers the FM-1, -1 not read
                                        * yet (the keys' readings own the converter: APP_KEY_SENSE) */
int32_t plat_battery_raw(void);        /* the reading plat_battery's bars come from: ADC channel 3 (the battery's divider),
                                        * the main loop's slow average, in the converter's counts (the bars' thresholds
                                        * 531 / 561 / 591); 0 = not read yet. Whether USB powers the FM-1 or not. For an
                                        * app that shows the volts itself; built for an app with APP_BATTERY_RAW (below) */
int plat_headphones(void);             /* the headphone jack's switch (PB5: 0 with a plug in), with fm1.toml headphones = true
                                        * (FM1B_HEADPHONES; without it -1 on the device, never read), read by the main loop every
                                        * pass and debounced (a state counts once the pin held it 100 ms: the contact
                                        * bounces ~150 ms a plug): 1 headphones in, 0 nothing plugged; -1 not read yet (the
                                        * host: its script's "jack"). The speaker mutes itself in hardware on a plug */

/* lights: bright buttons and keys, and dim ones (a short pulse each scan frame; a bright bit wins); glow 0 the dim
 * ones off, 1 low, 2 high. Bit NB in buttons / dim_buttons is PLAY's second, green LED */
void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys, int glow);
#define LED_PLAY_GREEN (1u << NB)
/* a third level, between the glow and lit (~12 % of lit; ~8 % with glow 1): these buttons and keys, unless lit, until
 * the next call (0, 0: none). Call it after plat_lights each time the lights change */
void plat_lights_mid(uint32_t buttons, uint32_t keys);
/* the dim ones at any brightness, not the glow's two (an app with APP_DIM_LEVELS): ns is the pulse each scan frame, in
 * nanoseconds (a lit LED is lit ~95 us a frame, plat_lights' glow 2 is 3,200 ns and glow 1 1,600 ns); the FM-1 takes
 * 500 .. 4,000, a value outside it is clamped to it. 0 hands the dim ones back to plat_lights' glow. One pulse serves
 * every dim LED (the buttons' glow and the keys': a scan frame has one), so it is the brightness of the whole dim layer;
 * the half level of plat_lights_mid is not changed. The host simulator keeps it (expect dim_ns, and "leds" prints it) */
void plat_dim_ns(uint32_t ns);
/* the same, simpler: the lit buttons and keys, the unlit buttons glowing if plat_glow(1); PLAY lights green, red
 * while plat_play_red(1) */
void plat_leds(uint32_t buttons, uint32_t keys);
void plat_glow(int on);
void plat_play_red(int red);

/* MIDI: 4-byte USB-MIDI event packets (byte 0 = cable / CIN, then status, data 1, data 2). In from USB, the TRS jack
 * (in only: the FM-1 has no MIDI out jack) and Bluetooth (FM1B_BLE); out to USB and Bluetooth. Bluetooth has two
 * links with FM1B_BLE_CENTRAL (below), the computer's and the device's: both come in on PORT_BLE, TO_BLE goes to
 * both, TO_BLE_HOST or TO_BLE_DEV to one. MIDI thru with nothing echoed back where it came from: after plat_midi_in,
 * plat_midi_send(pkt, (TO_USB | TO_BLE) & ~plat_midi_in_from()) */
enum { PORT_USB, PORT_BLE, PORT_TRS, NPORTS };
#define TO_USB (1u << PORT_USB)
#define TO_BLE_HOST (1u << PORT_BLE)   /* the computer (a phone, a tablet) connected to the FM-1 */
#define TO_BLE_DEV (1u << 3)           /* the Bluetooth MIDI device the FM-1 connected to (FM1B_BLE_CENTRAL) */
#define TO_BLE (TO_BLE_HOST | TO_BLE_DEV)
int plat_midi_in(uint32_t *pkt, int *port);   /* 1 = got one, from port PORT_* (port may be 0) */
/* when the last message plat_midi_in gave came in, on plat_ticks24's clock (called where plat_midi_in is): USB's and the
 * TRS jack's, with fm1.toml midi_stamp = true (FM1B_MIDI_STAMP; else when it was read), within 0.05 ms of landing on the FM-1 (a 10 kHz look at the endpoint and the jack's ring, inside the render
 * too, while the 2 kHz poll that takes them may wait for it); Bluetooth's: when it was read, or with fm1.toml ble_midi_time (ble_stack = "own") when the sender stamped it, the connection's bunching taken out (the transport then comes at once, not held to the sender's time). For an app that follows a
 * clock or places notes by time; host: when it was read */
uint32_t plat_midi_in_t24(void);
uint32_t plat_midi_in_from(void);      /* where the last one came from, as an out: TO_USB, TO_BLE_HOST or TO_BLE_DEV
                                        * (0: the TRS jack, which has no out); called where plat_midi_in is */
void plat_midi_send(uint32_t pkt, uint32_t to);   /* to: TO_USB | TO_BLE, or one link; main loop, the render or the
                                                   * tick */
void plat_midi_out(uint32_t pkt);      /* plat_midi_send(pkt, TO_USB) */
uint32_t plat_midi_out_room(uint32_t to);   /* the free slots in the out queues of to (TO_USB, TO_BLE, a link: each
                                        * has its own): the fewest among the ones someone listens on; 0 when nobody
                                        * does. A sequencer keeps room for the note-offs it owes before it sends a
                                        * note-on */
uint32_t plat_midi_out_link(void);     /* a number that changes at each new USB connection (a host to tell its
                                        * sounds again: programs, volumes) */
/* USB MIDI host (fm1.toml usb_host = true: FM1B_USB_HOST 1; felucca-bsp docs/USB_HOST.md): the USB port plays host to
 * a USB-MIDI controller or synth, directly or behind one powered hub; the FM-1 gives the port no power (a powered hub
 * or adapter). It comes in on PORT_USB and TO_USB goes to it; no computer meanwhile (no USB audio, backup, mirror).
 * Boot is always a device. plat_usb_host (main loop) asks; connecting blocks the main loop a moment (seconds behind a
 * hub), the sound plays on. plat_usb_host_msg: the latest change, once (not a queue). plat_usb_lost: 1 once after a
 * USB session ended (a device lost, the mode changed): release what its notes hold. Without FM1B_USB_HOST all stay
 * USBH_OFF / 0. */
enum { USBH_OFF, USBH_LOOKING, USBH_CONNECTED };
enum { USBH_MSG_NONE, USBH_MSG_ON, USBH_MSG_OFF, USBH_MSG_MIDI, USBH_MSG_NOMIDI, USBH_MSG_LOW, USBH_MSG_LOST };
void plat_usb_host(int on);
int plat_usb_host_state(void);
int plat_usb_host_msg(void);
int plat_usb_lost(void);
/* the connected device's product string, as it names itself ("Launchkey Mini MK3 MIDI"): ASCII, other characters as
 * '?', at most 31; "" while nothing is connected, and for a device without one. Built for an app with
 * APP_USB_HOST_NAME (below): felucca-bsp's FELUCCA_USB_HOST_NAME reads it at the connection (another app's image stays
 * as it was, byte for byte). Main loop */
const char *plat_usb_host_name(void);
uint32_t plat_midi_count(int port, int out);   /* messages through port PORT_* since the start (out: sent), real-time (clock...) left out */
uint32_t plat_midi_ago(int port, int out);     /* ms since the last one (0xFFFFFFFF: none yet) */

/* Bluetooth MIDI (fm1.toml ble = true: FM1B_BLE 1): JieLi's BT libraries on fm1-base's shim (ble/), started at
 * every boot unless the last Bluetooth run crashed (then by hand: plat_ble_start). The app gives it a store object,
 * OBJ_BLE (the radio's address and pairing). Without it (FM1B_BLE 0, tools/build.py --no-ble) nothing of JieLi's is
 * linked, and the state stays BLE_OFF */
#ifndef FM1B_BLE
#define FM1B_BLE 0
#endif
enum { BLE_OFF, BLE_STARTING, BLE_READY, BLE_FAILED };
int plat_ble_state(void);
int plat_ble_connected(void);          /* a central is connected and listening */
int plat_ble_linked(void);             /* a central is connected, listening or not yet (a Mac whose MIDI driver
                                        * never subscribed holds the link, and the FM-1 stops advertising) */
void plat_ble_start(void);
/* A stream of SysEx frames to the computer's link (ble_stack = "own"; a screen mirrored): the radio reads it in its own
 * interrupts, a connection event at a time, as much as the link takes (full notifications, as many as the stack puts
 * in an event), so it is never held to the main loop's pace or the out queue's 64 slots. peek: the next USB-MIDI
 * SysEx packet (CIN 4..7) written whole, 0 none (asked again until pop); pop: it went; mid: a frame begun and not
 * ended, which goes on before any message queued (plat_midi_send's) so none ends it; between frames those go
 * first. All three run in the radio's interrupts while a computer listens (plat_ble_connected): short, no lock, a
 * single-consumer ring the main loop fills. 1: taken; 0: not on this platform (the JieLi stack, no Bluetooth, the
 * host), send by plat_midi_send. NULL: none again */
struct plat_ble_stream {
    int (*peek)(uint32_t *pkt);
    void (*pop)(void);
    int (*mid)(void);
};
int plat_ble_stream(const struct plat_ble_stream *s);
/* the computer's link as agreed (ble_stack = "own"; 0 each when none, or another stack): v[0] the LL data length
 * (octets), v[1] the ATT MTU, v[2] the connection interval (x 1.25 ms), v[3..6] the central's own LL_LENGTH values
 * (MaxRxOctets, MaxRxTime, MaxTxOctets, MaxTxTime) */
void plat_ble_link_info(uint32_t v[7]);
uint32_t plat_ble_log_total(void);     /* lines written since the last start (survives a reset) */
const char *plat_ble_log_line(int back);   /* 0 = the newest */
/* Bluetooth MIDI devices (fm1.toml ble_central = true: FM1B_BLE_CENTRAL 1): the FM-1 connects to one (a keyboard,
 * a controller), beside a computer connected to it. Its MIDI comes in as PORT_BLE too (plat_midi_in_from:
 * TO_BLE_DEV), and TO_BLE goes to both (TO_BLE_DEV to the device alone, TO_BLE_HOST to the computer).
 * Scanning finds the devices that advertise the MIDI service (up to 8); connecting stops the scan. */
#ifndef FM1B_BLE_CENTRAL
#define FM1B_BLE_CENTRAL 0
#endif
enum { BLE_DEV_OFF, BLE_DEV_SCANNING, BLE_DEV_CONNECTING, BLE_DEV_SEARCHING, BLE_DEV_READY };
void plat_ble_scan(int on);
int plat_ble_found(int i, char *name, int *rssi);   /* device i of the scan (name: 21 bytes); 0 = none */
int plat_ble_connect(int i);           /* 0 = connecting */
void plat_ble_disconnect(void);
int plat_ble_device(void);             /* BLE_DEV_*: READY is connected, MIDI both ways */
int plat_ble_passkey(void);            /* a pairing with a device waits for its user to type this code (0..999999):
                                        * show it; -1: none (ble_stack = "own" only: an iPhone app asks for one) */
/* The devices one by one, for an app that keeps a list of its own (remembered devices, their bonds: APP_BLE_PEERS in
 * app.h; ble_stack = "own" only, else none of this does anything): every advertising report the scan hears goes to the
 * app too (app_ble_scan_report, main loop; pdu: the advertising PDU, its 2-byte header, AdvA and data, n bytes; rssi:
 * the radio's reading), plat_ble_connect_peer connects to one by its address with the bond the app kept, and the keys
 * a pairing with it gave come to the app (app_ble_device_keys, main loop). The state is
 * plat_ble_device's; the bytes are least significant first, as on the air. ble_central = 3: the central without pairing
 * (no keys, no passkey; a device that asks for security fails with PLAT_BLE_CF_NEED_PAIR) */
struct plat_ble_peer {
    uint8_t addr[6], addr_rand;        /* its address as heard (addr_rand: TxAdd, a random one) */
    uint8_t bonded;                    /* ltk / rand / ediv are its bond: the link is encrypted with them */
    uint8_t sec;                       /* PLAT_BLE_PEER_* */
    uint8_t ltk[16], rand[8];
    uint16_t ediv;
};
#define PLAT_BLE_PEER_AUTH 1u          /* its bond is authenticated (a passkey pairing) */
#define PLAT_BLE_PEER_MITM 2u          /* it needs an authenticated link: a pairing goes straight to the passkey */
struct plat_ble_keys {
    uint8_t has;                       /* PLAT_BLE_KEYS_* */
    uint8_t ltk[16], rand[8];
    uint16_t ediv;
    uint8_t irk[16], id[6], id_rand;   /* its identity: behind a private address, plat_ble_rpa_resolve finds it */
};
#define PLAT_BLE_KEYS_LTK 1u           /* ltk / rand / ediv: the bond */
#define PLAT_BLE_KEYS_ID 2u            /* irk / id / id_rand */
#define PLAT_BLE_KEYS_AUTH 4u          /* the pairing was authenticated (a passkey) */
enum { PLAT_BLE_CF_NONE, PLAT_BLE_CF_LOST, PLAT_BLE_CF_NO_MIDI, PLAT_BLE_CF_PAIRING, PLAT_BLE_CF_AUTH, PLAT_BLE_CF_GATT,
       PLAT_BLE_CF_NEED_MITM, PLAT_BLE_CF_KEY, PLAT_BLE_CF_NEED_PAIR };   /* why a connection to a device ended: lost,
                                        * no MIDI found, the pairing failed, the encryption refused, the GATT failed, it
                                        * needs a passkey (connect again with PLAT_BLE_PEER_MITM, no unauthenticated
                                        * bond), it refused our bond, it asked for security (ble_central = 3) */
#define PLAT_BLE_DF_PAIRING 1u         /* plat_ble_device_flags: a pairing of ours runs on the link */
#define PLAT_BLE_DF_PROMPTED 2u        /* one started on the last attempt's link (its user was asked) */
#define PLAT_BLE_DF_MITM 4u            /* the last attempt's device needs an authenticated link (a passkey) */
int plat_ble_connect_peer(const struct plat_ble_peer *p);   /* 0 = connecting (plat_ble_device), -1 not now */
int plat_ble_device_fail(uint32_t *code);   /* why the last connection to a device ended (PLAT_BLE_CF_*); code: its
                                        * code (the link layer's reason, the ATT error or the SMP reason) */
uint32_t plat_ble_device_flags(void);  /* PLAT_BLE_DF_* */
int plat_ble_rpa_resolve(const uint8_t irk[16], const uint8_t addr[6]);   /* 1: addr is a private address made from irk */
void app_ble_scan_report(const uint8_t *pdu, uint32_t n, uint32_t rssi);
void app_ble_device_keys(const struct plat_ble_keys *k);
/* Bluetooth on or off, as the player asks: off ends every link, stops advertising and scanning (the radio quiet);
 * on starts it (or advertises again). Whether it starts at power-on: APP_BLE_AT_BOOT (app.h, e.g. a saved setting;
 * default 1) */
void plat_ble_enable(int on);
/* What Bluetooth keeps (the own stack, ble_stack = "own"), by default in the app's store object OBJ_BLE. An app that
 * keeps it in its own saved data instead (a settings record it has, with these fields at its own places) defines
 * APP_BLE_STORE in app.h and gives the two calls below; it then needs no OBJ_BLE. app_ble_load: at Bluetooth's start
 * (main loop), 1 with r filled, 0 none kept (the platform makes the address then, and saves it); app_ble_save: main
 * loop, once something changed (a new address, a pairing, a remembered audio device), 0 kept (the app may write it
 * to the flash later, with its own data) */
struct plat_ble_rec {
    uint8_t addr[6];                   /* our BLE address (static random: the top two bits 1) */
    uint8_t bonded, rsv;               /* the computer's bond (ble_pairing): bonded, ediv, rand, ltk */
    uint16_t bond_ediv;
    uint8_t bond_rand[8], bond_ltk[16];
    uint8_t bta_addr[6], bta_addr_set;   /* Bluetooth audio (bt_audio): our BR/EDR address, once made */
    uint8_t bta_n;                     /* the remembered audio devices, the last one first */
    struct plat_bta_dev {
        uint8_t addr[6], bonded, link_key[16];   /* link_key: from its pairing (air order), when bonded */
        char name[17];
    } bta[4];
};
int app_ble_load(struct plat_ble_rec *r);
int app_ble_save(const struct plat_ble_rec *r);

/* Bluetooth audio out (fm1.toml bt_audio = true, with ble_stack = "own": FM1B_BT_AUDIO 1): an A2DP source to headphones
 * and speakers (SBC), beside Bluetooth MIDI on the one radio. While it streams, the render goes to the device and the
 * FM-1's own output is muted. Four devices are remembered with their keys (in OBJ_BLE), the last one first. Expect
 * 150-300 ms from a key to the ear (the device's own buffer): for listening, not for playing in time. */
#ifndef FM1B_BT_AUDIO
#define FM1B_BT_AUDIO 0
#endif
enum { PLAT_BTA_OFF, PLAT_BTA_SCANNING, PLAT_BTA_FOUND, PLAT_BTA_NONE, PLAT_BTA_PAGING, PLAT_BTA_LINKED, PLAT_BTA_SETUP,
       PLAT_BTA_STREAMING, PLAT_BTA_REFUSED, PLAT_BTA_NO_LAST, PLAT_BTA_FAILED, PLAT_BTA_READY };
void plat_bta_on(int on);              /* on: the radio up (Bluetooth MIDI's start, if not yet), ready to scan / connect */
void plat_bta_scan(int on);            /* look for devices (about 10 s): 1 a fresh list, 2 again with the list kept
                                        * (only when nothing else is under way: a page goes on), 0 stop */
int plat_bta_found(int i, char *name); /* found device i: its name (17 bytes) -> 0: none */
int plat_bta_known(int i, char *name); /* remembered device i (0 the last one) -> 0: none */
void plat_bta_pick(int i);             /* connect found device i (pairing if new; it becomes the last one) */
int plat_bta_reconnect(int i);         /* connect remembered device i -> 0 paging */
void plat_bta_forget(int i);
void plat_bta_hangup(void);
int plat_bta_state(void);              /* PLAT_BTA_* */
/* while it streams, where the sound goes: 1 the device, 0 the FM-1's own output (the stream suspended, the link
 * kept: back to the device at once, without a new connection) */
void plat_bta_output(int to_device);
int plat_bta_out_device(void);         /* 1: the sound goes to the device (the default) */
/* a link that ended by itself (the device left, or refused): where it had got to, once (then PLAT_BTA_LOST_NONE) */
enum { PLAT_BTA_LOST_NONE, PLAT_BTA_LOST_LINK, PLAT_BTA_LOST_PAIRING, PLAT_BTA_LOST_ENCRYPTION, PLAT_BTA_LOST_SETUP,
       PLAT_BTA_LOST_STREAMING, PLAT_BTA_LOST_REFUSED };
int plat_bta_lost(void);
int plat_bta_timed_out(void);          /* PLAT_BTA_REFUSED because the set-up timed out (the device never said no) */
int32_t plat_bta_latency_us(void);     /* what the stream adds on the FM-1's side (its queue), us; -1: not streaming.
                                        * An app that follows a clock may play that much earlier (the device's own
                                        * buffer comes on top and is not known) */
/* Bluetooth audio's big buffers in the app's memory (fm1.toml bt_audio_mem = "app": FM1B_BTA_MEM 1): the audio packets'
 * queue and the receptions' ring, which only a run of the Classic radio uses (a scan, a page, a link), are a block the
 * app lends while an AUDIO mode is on, instead of static RAM: an app that switches Bluetooth audio off has that RAM
 * back. Without it (FM1B_BTA_MEM 0) they are static and these say so (bytes 0, the block refused).
 *   plat_bta_mem_bytes()  the block's size (4-aligned), 0: static buffers
 *   plat_bta_mem(p)       p: the block lent (before plat_bta_on(1), which does nothing without one) -> 0;
 *                         0: taken back -> 0 done (the radio's code and interrupts no longer read it), -1 still in use
 *                         (an AUDIO mode on, or its run not over yet: plat_bta_on(0) first, then again later) */
#ifndef FM1B_BTA_MEM
#define FM1B_BTA_MEM 0
#endif
uint32_t plat_bta_mem_bytes(void);
int plat_bta_mem(void *p);
int plat_bta_streams(void);            /* 1: the last render went to Bluetooth (the FM-1's own output muted for it); for the
                                        * render (the audio interrupt) to plan its block by. 0 without Bluetooth audio */

/* WiFi (fm1.toml wifi = true: FM1B_WIFI 1): a station on JieLi's WiFi libraries and the SDK's lwIP, on the same shim.
 * Off until the app turns it on; then it joins the networks it joined before (kept in the app's store object OBJ_WIFI,
 * six, the most recent first). The app's network code runs in a task of its own (plat_net_task) with lwIP's sockets
 * (lwip/sockets.h): a socket call waits, and the main loop must not. A build has Bluetooth or WiFi, not both (yet). */
#ifndef FM1B_WIFI
#define FM1B_WIFI 0
#endif
enum { WIFI_OFF, WIFI_STARTING, WIFI_ON, WIFI_JOINING, WIFI_ADDRESS, WIFI_CONNECTED, WIFI_FAILED };
void plat_wifi_on(int on);             /* on: the radio starts (its first second holds the sound) and joins */
int plat_wifi_state(void);             /* WIFI_*: ON is on with no network; ADDRESS, joined and asking for one */
const char *plat_wifi_why(void);       /* WIFI_FAILED: "not found", "wrong password?", "no address"... */
const char *plat_wifi_ssid(void);      /* the network joined, or being joined */
uint32_t plat_wifi_ip(void);           /* connected: the address, its first byte lowest; else 0 */
int plat_wifi_rssi(void);              /* connected: the signal */
int plat_wifi_scan(void);              /* the networks around: plat_wifi_found once it is done (0 = asked) */
int plat_wifi_found(int i, char *ssid, int *rssi, int *secured);   /* the scan's network i (ssid: 33 bytes), the
                                                                     * strongest first; 0 = none */
int plat_wifi_found_channel(int i);    /* its channel (1..14), 0 = none */
int plat_wifi_join(const char *ssid, const char *pass);   /* pass 0: the saved one's, or none; kept once joined */
int plat_wifi_saved(int i, char *ssid);   /* the networks kept, the most recent first; 0 = none */
void plat_wifi_forget(int i);
int plat_wifi_kept(const char *ssid);  /* the network is kept (its password known): plat_wifi_join(ssid, 0) joins it */
void plat_wifi_rx(uint32_t *frames, int *rssi);   /* reception: frames received in the last second (anyone's), their
                                                   * average signal */
uint32_t plat_wifi_scans(void);        /* scans done since the start: a change means plat_wifi_found has new results */
int plat_net_task(void (*fn)(void *), void *arg, const char *name);   /* a task for the app's network code (8 KiB
                                                                        * of stack); 0 = made */

/* the 1 kHz tick (app_tick, an app with APP_TICK) runs in the timer interrupt: the main loop locks it out around
 * what they share. It nests, and plat_unlock takes what its plat_lock returned: the outermost unlock switches the
 * interrupts back on only if they were on at its lock, so a lock is safe inside an interrupt (app_render, app_tick)
 * or with them already off: they stay off */
uint32_t plat_lock(void);
void plat_unlock(uint32_t f);
void plat_yield(void);
/* the FM-1 restarts, on purpose (an app's own restore done, a session that starts from a clean boot): the sound
 * stopped, USB let go (the computer sees the cable out and in), not counted as a failed start (no safe mode), the
 * crash record and .noinit kept. Main loop; it does not return (the host simulator: the restart at the end of the
 * millisecond, a "reboot"). Built for an app whose app.h defines APP_RESTART */
void plat_restart(void);
void plat_log(const char *line);
/* the flash's data area, 0x97000..0xDFFFF, for an app's own big data (a table it builds, samples): its store objects
 * live there too, at the offsets it gave them (APP_STORE_MAP), so it keeps to the rest. Main loop only; an erase or a
 * write stops everything for a moment (the sound too: no flash reads meanwhile).
 * Two more windows for big data, outside the store: PLAT_XDATA0 (16 KiB, just above the app area) and PLAT_XDATA1
 * (92 KiB, above the update's staging). The update loader clears an update record it finds in the last 256 bytes of
 * a sector there (bsp/loader/ldr_core.c: "AT" at bytes 0xF06..0xF07, a nonzero CRC-16 at 0xF00 over the 78 bytes
 * from 0xF02), so in these two an app keeps bytes 0xF00..0xF07 of every sector erased (0xFF), or else makes sure
 * those bytes never read as a record: never "AT" at 0xF06 (LOOM-8's sample bank moves its data so, and refuses a
 * bank that has it).
 * A fourth, on request (fm1.toml xdata2 = true: FM1B_XDATA2): PLAT_XDATA2, the three sectors 0xFC000..0xFEFFF
 * (12 KiB) between the SDK's last area (USR, up to 0xFBFFF) and its key_mac sector (0xFF000, never written), where
 * Felucca keeps its globals: an app that was a Felucca firmware reads and writes its own bytes there. The update loader
 * looks for records below 0xFC000 only, so no 0xF00 rule here. Store objects may sit there too (the template's
 * settings pair is 0xFC000 / 0xFD000): with the window on, plat_data_erase and plat_data_write refuse a store
 * object's sectors in it, so an app with this window puts its objects elsewhere (or keeps to the rest) */
#define PLAT_DATA_LO 0x97000u
#define PLAT_DATA_HI 0xE0000u
#define PLAT_XDATA0_LO 0x93000u
#define PLAT_XDATA0_HI 0x97000u
#define PLAT_XDATA1_LO 0xE5000u
#define PLAT_XDATA1_HI 0xFC000u
#define PLAT_XDATA2_LO 0xFC000u
#define PLAT_XDATA2_HI 0xFF000u
#if FM1B_XDATA2
#define PLAT_DATA_OK(off, n) (plat_in((off), (n), PLAT_DATA_LO, PLAT_DATA_HI) || \
                              plat_in((off), (n), PLAT_XDATA0_LO, PLAT_XDATA0_HI) || \
                              plat_in((off), (n), PLAT_XDATA1_LO, PLAT_XDATA1_HI) || \
                              plat_in((off), (n), PLAT_XDATA2_LO, PLAT_XDATA2_HI))
#else
#define PLAT_DATA_OK(off, n) (plat_in((off), (n), PLAT_DATA_LO, PLAT_DATA_HI) || \
                              plat_in((off), (n), PLAT_XDATA0_LO, PLAT_XDATA0_HI) || \
                              plat_in((off), (n), PLAT_XDATA1_LO, PLAT_XDATA1_HI))
#endif
static inline int plat_in(uint32_t off, uint32_t n, uint32_t lo, uint32_t hi)   /* [off, off + n) in [lo, hi) */
{
    return off >= lo && off <= hi && n <= hi - off;
}
const uint8_t *plat_data(uint32_t off);   /* read in place; 0 = outside the windows */
int plat_data_erase(uint32_t off);        /* the 4 KiB sector at off (aligned); 0 = done */
int plat_data_write(uint32_t off, const void *src, uint32_t n);   /* src in RAM; 0 = done */
/* plat_data_write programs, as NOR flash does: each byte becomes what it was AND what is written (bits go from 1 to
 * 0 only, and only an erase sets them again). So it writes into erased flash, and it may also clear bits in place, in
 * bytes written before, without an erase: a record's state byte marked used, then dead (0xFF, 0x7F, 0x3F), a bitmap's
 * bits taken one by one. Guaranteed on the device (felucca-bsp's fl_write: page program, no erase, no read-back) and
 * in the host simulator (the same AND). A 1 written over a 0 stays 0, and is not an error: plat_data_write does not
 * read back; an app that must know reads plat_data after. */
/* SysEx to the computer over USB (main loop): whole frames, F0 .. F7, into felucca-bsp's SysEx ring (sent before
 * the notes), which the USB interrupt empties as the computer reads. plat_sysex_room, plat_sysex_queued and
 * plat_usb_session (and plat_sysex_send's -1 without a computer) are built for an app with APP_SYSEX (below) */
int plat_sysex_send(const uint8_t *frame, uint32_t n);   /* 0 = queued whole; -1 = not sent: no computer has set
                                                         * the FM-1 up (plat_usb_session 0), or it stopped reading
                                                         * (no room for 200 ms; what was queued goes out) */
uint32_t plat_sysex_room(void);        /* the bytes of SysEx the ring takes now without a wait: 0 with no computer, the
                                        * bus asleep, or a send under way. A send that must never wait checks it */
uint32_t plat_sysex_queued(void);      /* the bytes still in the ring, not yet read (0: all out) */
/* a frame streamed as it is made, for one too long to build first (a screen mirrored as it is drawn): begin, its bytes
 * F0 .. F7 in any number of puts, end. The ring is the stream's from begin to end (no other SysEx, no note out in
 * between: keep it short), each packet going in as soon as it is whole. A put waits while the ring is full and the
 * computer reads; one that waited PLAT_SYSEX_STALL_MS without a packet read gives up: the frame is cut there (the
 * computer drops a frame cut short at the next F0), the rest of its puts and its end return -1. Main loop; built for
 * an app with APP_SYSEX */
#define PLAT_SYSEX_STALL_MS 30u
int plat_sysex_begin(void);            /* 0 = the stream is open; -1 = no computer, or a send under way */
int plat_sysex_put(const uint8_t *p, uint32_t n);   /* 0 = in the ring (the last bytes held until more or end) */
int plat_sysex_end(void);              /* the last packet out, the ring free again: 0 = the frame went whole */
uint32_t plat_usb_session(void);       /* 0: no computer has set the FM-1 up; else a number that changes at every new
                                        * one (a bus reset, the cable out and in): what an app keeps for a connection
                                        * (a client's session) it drops when this changes */
/* the resource region (fm1.toml resources = FILE, FM1B_RES; app.h APP_RES_BASE, APP_RES_SECTORS: device/res.c):
 * the file's bytes read in place, *len their count; 0 (and *len 0) while the FM-1 does not hold this build's (not
 * yet written after an update, torn, another build's): the app does without. The host writes it
 * (tools/fm1_resources.py; the installer after an update); a sound reading it is stopped first (app_panic) */
const uint8_t *plat_res(uint32_t *len);

/* the USB serial console (fm1.toml usb_console = true: FM1B_CDC 1): a CDC-ACM function beside USB MIDI, for the app's
 * own text (logs, a command line): the platform carries the bytes, the app says what they mean. Main loop only. Not with
 * usb_audio_out_feedback or usb_audio_split (they take the console's endpoint, EP3). Without it, and on the host: never
 * open, nothing read, nothing written */
int plat_console_open(void);              /* a computer has the port open (its DTR) */
int plat_console_read(void);              /* the next byte from the computer; -1 none */
uint32_t plat_console_room(void);         /* the bytes plat_console_write takes now (0 while not open) */
uint32_t plat_console_write(const void *p, uint32_t n);   /* -> the bytes taken (at most the room: never waits) */
void plat_console_present(int on);        /* the console presented (the default) or not, e.g. from a setting: in app_init
                                           * before USB starts; later, the FM-1 leaves the bus and comes back as the
                                           * other device */

/* the app's own commands in the update's SysEx framing (F0 pack7(00 59 cmd len24 body ~sum) F7: felucca-bsp ota.c),
 * an app with APP_USB_CMD: app_usb_cmd(d, n) gets each frame whose cmd the platform does not take (d: the decoded
 * frame, cmd d[2], the body from d + 6). Inside it the app may pull data from the host as an update does: */
int plat_host_read(uint32_t addr, uint8_t *dst, uint32_t n);   /* n bytes (512 at most) at the host's addr; 0 = ok.
                                                                 * Waits (tries: 3); main loop only */
void plat_host_patience(uint32_t try_ms, uint32_t total_ms);   /* each try's wait (default 5000) and the whole
                                                                 * transfer's (0: no limit) */

/* USB audio full (FM1B_UAC): 1, the USB audio input at full level (the app's render at gain 4096), MASTER for the
 * speaker and headphones only; 0, both at MASTER (the default) */
void plat_usb_audio_full(int on);
/* the computer's audio (FM1B_UAC_OUT, usb_audio_out: the computer plays through the FM-1): the level it is mixed in
 * at, 0 muted .. 4096 as sent (the default; above is 4096), times MASTER. Muted, the FM-1 still takes the stream, so
 * the computer sees no change and turning it up again starts in step. The USB recording (usb_audio_in) is taken
 * before the computer's audio is mixed in, whatever this is: recording the FM-1 never records the computer's own
 * playback (device/uac_mix.h). Without usb_audio_out, nothing */
void plat_usb_play_gain(uint32_t q12);
/* the app's own channels of the USB audio input (fm1.toml usb_audio_channels = N, 4..10, even: FM1B_UAC_CH): channels 3..N,
 * PLAT_USB_CH of them, beside the master on 1 and 2, as 16-bit samples (32767: the master's full scale, the render's
 * 1 << 22 at 4096). In app_render, the frame off frames into the block being rendered: its channels at p[0] ..
 * p[PLAT_USB_CH - 1], the next frame's PLAT_USB_CH_STRIDE on, for *run frames (where the ring wraps: ask again from
 * there). They start at 0 each block; the app adds into them, on either core (one writer a sample at a time), and the
 * platform sends them with the master's frames once app_render returns. NULL (*run 0) when no computer records (the
 * app then skips the work), and always with 2 channels; the host: a block's buffer, always taken */
#ifndef FM1B_UAC_CH
#define FM1B_UAC_CH 2
#endif
#define PLAT_USB_CH (FM1B_UAC_CH - 2)
#define PLAT_USB_CH_STRIDE FM1B_UAC_CH
int16_t *plat_usb_ch(uint32_t off, uint32_t *run);

/* the keys' velocity and pressure (an app with APP_KEY_SENSE): on, the scan reads the keys on the analog rows and
 * hands each reading to the app (app_key_sample, app_key_probe); off by default */
void plat_sense(int on);
uint32_t plat_analog_mask(void);       /* bit k: key k is on an analog row (PA7 / PA8; the top row is probed) */

/* 16 bytes unique to this FM-1 (its flash part's factory ID; the same across updates, firmwares and a wiped store):
 * for an address that must not change (the WiFi MAC, the Bluetooth name); 0 = unknown (and in a build without a radio) */
const uint8_t *plat_chip_uid(void);

/* persistent storage (Felucca storage.c objects: A/B sectors, CRC, torn-write safe) */
int plat_store_load(uint32_t obj, void *dst, uint32_t max);   /* bytes loaded, < 0 = none / bad (an empty object is
                                                               * none: a restore writes one for an absent object) */
int plat_store_head(uint32_t obj, void *dst, uint32_t n);     /* its first n bytes (a header): n, < 0 = none / bad /
                                                               * shorter */
int plat_store_size(uint32_t obj);                           /* its length in bytes, < 0 = none / bad (an app that
                                                               * wants it whole or not at all checks it first) */
int plat_store_save(uint32_t obj, const void *src, uint32_t len);   /* 0 = ok; -8: the pool is full (nothing written) */
/* an app with a pool (app.h APP_STORE_POOL: storage.c): the sectors free for its pooled objects' next copies (a save
 * of n objects that finds n keeps every previous copy); -1 no pool */
int plat_store_room(void);
#define PLAT_STORE_MAX 3840u           /* bytes per object */
/* An app may pool some objects (APP_STORE_POOL, APP_STORE_POOL_BASE / _N / _OBJ0 / _OBJN / _OLD: storage.c): those
 * share sectors instead of a pair each, and APP_STORE_MAP's entries for them are not used. APP_STORE_POOL_EMPTY(obj, p,
 * n), if the app defines it: true for an old copy that holds nothing worth a sector (a placeholder of the layout
 * before the pool): the move into the pool drops the object instead (every copy of it erased).
 * The objects are the app's (app.h): an enum that includes OBJ_PANEL (the panel calibration, the platform's) and
 * ends with OBJ_NOBJ, and APP_STORE_MAP, each object's flash offset: two 4 KiB sectors (A/B) from there, inside the
 * data area (0x97000..0xDFFFF) or Felucca's settings sector pair at 0xFC000. An object's number is written in its
 * record, so an app keeps its numbering from one release to the next. */

/* audio load in % of the render budget, and xruns since boot (device: from the ISR) */
uint32_t plat_cpu_pct(void);
uint32_t plat_audio_peak_us(void);
void plat_audio_peak_reset(void);
uint32_t plat_cpu_khz(void);           /* the CPU's clock, kHz: 360000 when its registers hold the stock set-up (M-VAVE's
                                        * 360 MHz, which the start sets whichever boot loader ran: fm1_clock.h), 0 when
                                        * not (unknown: the start could not set it) and on the host. For an app that
                                        * turns the render's microseconds into instructions (a CPU guard's cost model);
                                        * built for an app with APP_CPU_KHZ (below) */
uint32_t plat_xruns(void);

/* a free-running count of PLAT_TICKS_PER_US ticks a microsecond, for timing spans shorter than plat_ms can: a part of
 * the render, say. Device: TIMER4, from the 24 MHz crystal (one register read); host: its monotonic clock, at the same
 * rate. No lock and no state, so the audio interrupt and the tick may call it too. It wraps every 2^32 ticks (179 s):
 * a span is the unsigned difference of two reads */
#define PLAT_TICKS_PER_US 24u
uint32_t plat_ticks24(void);

/* Disk mode (fm1.toml msc = true: FM1B_MSC; device/msc.c on felucca-bsp's FELUCCA_MSC): the FM-1 shows the computer a
 * USB drive, the app's blocks, until it goes back to itself; meanwhile it has no USB MIDI or audio. The app keeps the
 * blocks (plat_disk_t) and calls plat_disk_poll from its main loop (APP_LOOP: every pass) while on. Host: nothing
 * comes (PLAT_DISK_WAITING) */
typedef struct {
    uint32_t blocks;                                   /* 512-byte blocks */
    const uint8_t *(*read)(uint32_t lba);              /* the block (512 bytes, kept until the next call); 0: zeros */
    int (*write)(uint32_t lba, const uint8_t *b);      /* 0 = written (it may hold it until flush) */
    void (*flush)(void);                               /* what is held, written (the computer synced or ejected) */
    const char *vendor, *product;                      /* what the computer calls it: 8 and 16 characters at most */
} plat_disk_t;
enum { PLAT_DISK_OFF, PLAT_DISK_WAITING, PLAT_DISK_MOUNTED, PLAT_DISK_EJECTED };
void plat_disk_mode(const plat_disk_t *d);             /* d: the FM-1 drops off USB and comes back as this disk; 0:
                                                        * flushed, then back as itself */
int plat_disk_poll(void);                              /* main loop: serve the computer -> PLAT_DISK_* (EJECTED: the
                                                        * computer let it go) */
uint32_t plat_disk_idle_ms(void);                      /* since the computer's last command */

/* code loaded at run time (fm1-ripple's currents: plug-ins copied from its disk into the pool and run there): the
 * guard's RAM code window (felucca-bsp fm1_guard.h, normally .ram_text alone) widened from .ram_text up to end, so
 * code there may run. Main loop, after the guards are on (app_init or later); host: nothing */
void plat_code_window(const void *end);

/* The second core (fm1.toml cpu2 = true: FM1B_CPU2; felucca-bsp's chip/fm1_cpu1.h, Melodee's): a worker for a part of
 * the render. Start it in app_init (the one time it may start: at power-on, before the audio and the timers); the
 * render hands it a job, does its own share, then waits for it. One job at a time. A failure (it misread a job, it
 * faulted, a job was not back in 8 ms, it did not answer at its start) restarts the FM-1, the reason in the crash
 * record (vec 0x101..0x105: gave_up's numbers below); two failed starts are safe mode. With cpu2_fallback = true
 * instead: it is given up (held; the render does the work itself) and barred, one core at every start until
 * power-off or a new build (FM1B_BUILD), and one core after a failed start. Host: a job runs at once, in order (an app
 * with its own simulation defines APP_HOST_CPU2 and has them all). */
int plat_cpu2_set(int on);             /* on 1, in app_init: 0 = it runs; -1 = one core (not now, barred, or it did not
                                        * answer). On 0: held (from the main loop: no job outstanding) */
int plat_cpu2_on(void);                /* it takes jobs */
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg);   /* render: 1 = it has fn(arg); 0 = do it yourself */
int plat_cpu2_wait(void);              /* render: 0 = done; -1 = it was given up and the job dropped: do it yourself
                                        * (cpu2_fallback only) */
void plat_cpu2_sync(void);             /* a barrier between the cores' reads and writes (a job's own sharing) */
int plat_cpu2_busy(void);              /* 1 = the job handed over is not done yet (no waiting) */
int plat_cpu2_gave_up(void);           /* why it is off: 0 no, 1 it misread a job, 2 one was late, 3 barred, 4 it
                                        * faulted, 5 it did not answer at its start */
uint32_t plat_cpu2_bad(void);          /* the jobs it misread */
int plat_cpu2_barred(void);            /* 1: one core until power-off (given up, or the first core crashed beside it) */
uint32_t plat_cpu2_stack(void);        /* how deep its stack (2816 B; fm1.toml cpu2_stack) has gone, in bytes; 0: not
                                       * marked this start (it never started, or was barred) */
/* which core runs the caller: 0 the first, 1 the second (a job plat_cpu2_run handed it). For state a job keeps per core
 * (a scratch buffer each core renders into). No lock, no state: any core, any context; from code that runs from RAM
 * through a pointer, as any flash function. Device: the core's number register (felucca-bsp's fm1_cnum); host: 1
 * while the simulated second core runs a job (an app with APP_HOST_CPU2 has its own) */
uint32_t plat_core(void);
/* Cache RAM (fm1.toml cache_ram_ways = 2, 4 or 7: FM1B_CACHE_RAM; felucca-bsp's chip/fm1_cache_ram.h): that many of the
 * data cache's 8 ways of 4 KiB used as RAM at 0x01F28000, set and tested at the start before anything else runs; a
 * failed test puts the cache back and gives 0 bytes. Objects the app places there itself (section .cache_ram, NOLOAD:
 * zeroed by the test, never RAM after a failure) come first; base is what is left after them, for the app's own use
 * (main loop memory: whether the second core and DMA read these ways has not been measured, so nothing they read goes
 * there). The flash's data goes through the ways kept as a cache: fewer of them make big reads from the flash slower
 * (Optimist's FM-1, 7 ways as RAM: reads over 16 KiB 57x slower; 2 ways in daily use for days). Host: a buffer of the
 * same size stands in; FM1B_CACHE_RAM_FAIL=1 in the environment: its test failed */
enum { PLAT_CR_OFF, PLAT_CR_OK, PLAT_CR_BUSY, PLAT_CR_REG, PLAT_CR_BAD };   /* not built / ready / the cache never idled /
                                                                          * the masks not taken / the test failed */
struct plat_cache_ram {
    uint8_t *base;                     /* the free part's start, 0: none */
    uint32_t bytes;                    /* its bytes (the ways less the .cache_ram objects), 0: none */
    uint32_t ways, status;             /* the ways asked for (fm1.toml), PLAT_CR_* */
    uint32_t dway, iway, bad_at;       /* the data and instruction way masks after, the first failing address */
};
const struct plat_cache_ram *plat_cache_ram(void);


/* Debug stats (device/dbgstats.c; fm1.toml debug_stats = true, FM1B_DBGSTATS): a 64-byte snapshot of the platform's counters (the audio's halves, late ones and
 * longest render, the timer's gaps, the longest interrupts-off and main-loop pass, the second core's and the radios'
 * state, starts and crashes) and the app's own PLAT_STATS_APP bytes, kept in flash every 5 minutes of uptime while
 * on, and one with every crash whatever the switch; the last 60 (5 hours) read over USB-MIDI (F0 7D 4C 53 F7,
 * tools/fm1_remote.py stats). Off at every start: an app with a setting for it turns it on after loading it. The
 * flash is the update's staging's last sector: every install starts the stats again. The app's side (app.h):
 *   APP_STATS        void app_stats(uint8_t out[PLAT_STATS_APP])   its bytes, once a second (main loop; zeroed before)
 *   APP_STATS_LAYOUT "name width ..."   what they are, for the readout: each field a name and its width in bytes (1,
 *                    2 or 4, little-endian), in order ("c10 1 drops 2"); without it the bytes are printed raw
 *   APP_STATS_QUIET  int app_stats_quiet(void)   1: a flash write now disturbs nothing (the transport stopped). A
 *                    snapshot waits for it up to 4 minutes, then is written anyway (one 64-byte page); without it a
 *                    snapshot is written at once
 * The host simulator keeps them too (its flash dump has the sector) */
#define PLAT_STATS_APP 7u
/* The stacks' room (device/stacks.c, fm1.toml crash_history = true or debug_stats = true; without them plat_stack_left
 * and plat_stack_least_ever are -1 on the device, and the stats are off): CPU0's main and interrupt stacks are painted
 * at every start, the second core's marked by felucca-bsp; the bytes never used. plat_stack_left: since this start (-1: not measured, the second core
 * off or not built). plat_stack_least_ever: the least ever seen, across restarts (.noinit) and power-offs (the debug
 * stats' newest record in flash, and the crash records' room at each crash); -1 not known yet. Main loop. Each crash
 * record keeps every stack's room at the crash (F0 7D 4C 48 F7's "stk" line) */
enum { PLAT_STK_MAIN, PLAT_STK_IRQ, PLAT_STK_C1 };
int32_t plat_stack_left(int k);
int32_t plat_stack_least_ever(int k);
void plat_stats_enable(int on);        /* the snapshots every 5 minutes on (1) or off (0) */
int plat_stats_on(void);
void app_stats(uint8_t out[PLAT_STATS_APP]);
int app_stats_quiet(void);

/* the app's optional hooks (app.h defines the macro when it has the function):
 *   APP_TICK        void app_tick(void)           1 kHz, in the timer interrupt (a clock, a sequencer)
 *   APP_BOOT        void app_boot(void)           once, after the platform is up, before app_ui_init (a radio)
 *   APP_LOOP        void app_loop(void)           every pass of the main loop (a radio's tasks)
 *   APP_YIELD       void app_yield(void)          from plat_yield, inside long work
 *   APP_BLE_PEERS   app_ble_scan_report, app_ble_device_keys   the devices one by one (plat_ble_connect_peer, above):
 *                   each advertising report a scan hears, a pairing's keys
 *   APP_OVERLOAD    void app_overload(void)       the render took over 90 % of its time (shed voices)
 *   APP_SPLASH      void app_splash(void)         the first screen at boot, instead of the name
 *   APP_GAIN(m)     the render's gain from MASTER's position m (0..4096; default the square, m * m >> 12)
 *   APP_BATTERY_RAW (no function) plat_battery_raw is built (the battery's reading, for an app's own volts)
 *   APP_CPU_KHZ     (no function) plat_cpu_khz is built (the CPU's clock)
 *   APP_DIM_LEVELS  (no function) plat_dim_ns is built (the dim lights at any of many brightnesses)
 *   APP_QUIET       void app_quiet(int32_t *lr, uint32_t n)   the render while the flash must not be read (the
 *                   radio's first second, FM1B_BLE): keep time without sound (default: silence)
 *   APP_KEY_SENSE   void app_key_sample(uint32_t key, uint32_t v, uint32_t ticks, int driven) (rows PA7 / PA8),
 *                   int app_key_drive(uint32_t key) (read it driven), void app_key_probe(uint32_t key, uint32_t v,
 *                   uint32_t pressed) (the top row), int app_key_led_ok(uint32_t key) (0: its column dark when read);
 *                   from the scan's interrupt
 *   APP_SYSEX       int app_sysex(const uint8_t *p, uint32_t n)   a SysEx frame from USB the platform does not take
 *                   (p: the bytes between F0 and F7); 1 = the app's. Its answers: plat_sysex_send. Main loop
 *   APP_SYSEX_BYTE  void app_sysex_byte(uint8_t b)   every byte of a SysEx from USB, F0 and F7 too (realtime bytes
 *                   left out; another status inside ends the frame, unseen here), as it comes: an app's own frames longer
 *                   than APP_SYSEX's (640 bytes between F0 and F7; a DX7 bank is 4104). In the USB interrupt: keep it short
 *   APP_USB_CMD     void app_usb_cmd(const uint8_t *d, uint32_t n)   a frame in the update's framing (above). Main loop
 *   APP_USB_HOST_NAME (no function) plat_usb_host_name is built (the USB host reads the device's product string)
 *   APP_REDRAW      void app_redraw(void)        draw the whole screen again at the next frame (the mirror lost a part)
 *   APP_MIRROR_TEST int app_mirror_test(int on)  the mirror's remote asks for a test session (on 1, off 0): 1 done
 *   APP_CSTART      void app_cstart(void)         first thing at a start, before the RAM is cleared and with nothing
 *                   set up (registers only, no RAM): an app's own second core that a reset left running, held
 *                   (fm1-base's own, cpu2 = true, needs none of these)
 *   APP_BT_FRAMES   const int32_t *app_bt_frames(const int32_t *lr, uint32_t n)   the frames Bluetooth audio takes from
 *                   the render just made (lr, n frames, 24-bit; 0 = silence): for an app whose own output differs from
 *                   what Bluetooth should get. Audio interrupt, after each render (Bluetooth audio built). Without it
 *                   Bluetooth takes the output
 *   APP_FAULT       void app_fault(const fm1_crash_t *c)   a crash, before the crash screen (c: the record): an app's
 *                   own second core given up; or the crash is on that core (c->sp on its stacks): it stops there
 *   APP_LOG         void app_log(void (*line)(const char *t))   the development readout over USB-MIDI (F0 7D 4C 47 F7,
 *                   "LG"): the app's own lines, each through line (ASCII, 40 characters at most), before its "end".
 *                   Main loop
 *
 * Screen mirroring (fm1.toml mirror = true: FM1B_MIRROR 1, device/mirror.c): the screen and the panel live on a
 * computer (base/web/mirror/, tools/fm1_mirror.py), and the panel played from it. Nothing for the app to call; an app
 * with APP_REDRAW gets back what a slow link dropped.
 *
 * Backup and restore over USB (fm1.toml backup = true: FM1B_BACKUP 1, device/backup.c; tools/fm1_backup.py): the
 * app's saved objects, all or nothing. The app's app.h:
 *   APP_BACKUP_OBJS {OBJ_PROJ, ...}   the objects, in order (not OBJ_PANEL: the calibration is the unit's)
 *   APP_BACKUP_MAGIC 0x...u           its image's magic (4 ASCII bytes, little-endian), APP_BACKUP_VERSION its format
 *   APP_BACKUP_WHAT "replaces all songs"   the restore question's second line
 *   APP_BACKUP_BUF io_buf             optional: the app's own buffer of at least 3840 B (uint8_t name[], not static,
 *                                     4-aligned) lent to a restore, which then needs none of its own (3840 B of the
 *                                     pool); a scratch buffer the app fills and empties within a call (a restore runs
 *                                     with the app's frame stopped and reloads everything after). Never one an
 *                                     interrupt or the second core uses. A resource transfer (res.c) borrows it too,
 *                                     and a refused or failed restore leaves it overwritten as well (no
 *                                     app_backup_restored then: the app's frame starts again from app_ui_init)
 * and these: int app_backup_playing(void) (1: no restore now, STOP FIRST), void app_backup_flush(void) (what is not
 * saved yet, saved: a backup starts), void app_backup_hold(int on) (no saving while a backup streams),
 * void app_backup_restored(void) (everything from the flash again) */
