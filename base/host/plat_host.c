/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* fm1-base on the host: the whole app (UI, engine, saved state) against a simulated FM-1 driven by a script.
 * Audio goes to a WAV, the screen to PNGs, the lights to text, MIDI out to a log. The device-only
 * parts (the BSP, USB, update, flash driver) are replaced by plat.h implemented here. (From X0X's x0x_host, by way
 * of FoMni-1's.) Built by tools/build.py APP_DIR --host into APP_DIR/build/host/NAME_host.
 *
 *   NAME_host SCRIPT [OUTDIR]
 *
 * Script, one command per line (# comments):
 *   wait MS                      run the device for MS milliseconds
 *   press BTN | release BTN | tap BTN     (BTN: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   key K down|up [VEL [PRESS]] | tapkey K [VEL]   (K: 0..26 = F3..G5, or w0..w15 white keys, b0..b10 black keys;
 *                                with APP_KEY_SENSE, the key lands at VEL 1..127, default 100, and is pressed PRESS
 *                                0..100 % more: the app's app_host_key makes the readings)
 *   strum A B [MS]               white keys A..B pressed in turn (down then up), MS apart (default 25)
 *   turn ENC N [MS]              (ENC: SELECT ALGO PRESET K1 K2 K3 K4; N detents, MS apart, default 80)
 *   spin ENC N                   N detents at once, exactly N (no acceleration)
 *   master N                     MASTER pot 0..4096 (then 40 ms, as the device's smoothing settles)
 *   midi [PORT] B0 B1 B2         incoming MIDI message (PORT: USB, the default, BLE, BLE-DEV or TRS; hex bytes;
 *                                BLE is the computer's link, BLE-DEV the device's: both PORT_BLE, FM1B_BLE_CENTRAL)
 *   ble connect | ble link | ble disconnect   a central listening, or linked and not yet listening (FM1B_BLE: the
 *                                stand-in stack is ready a second after the start)
 *   bta add NAME | bta drop NAME a Bluetooth audio device around, or gone (FM1B_BT_AUDIO: a link to it is lost)
 *   jack in | jack out           headphones into the jack, or out (plat_headphones; nothing plugged at the start)
 *   wav FILE | wavstop           start / stop recording the output
 *   usbwav FILE | usbwavstop     start / stop recording the USB audio input as a computer gets it: usb_audio_channels
 *                                (FM1B_UAC_CH) of 16 bits, the master on 1 and 2, the app's own after it (plat_usb_ch)
 *   peakreset                    restart the output peak (expect peak_db_min / peak_db_max)
 *   shot FILE.png                save the screen
 *   leds                         print the lit buttons and keys (keys: # bright, + dim)
 *   out                          print the MIDI sent since the last "out" (and forget it; clocks not shown)
 *   expect_out PORT B0 B1 B2     check that this message went out to PORT since the last "out" (USB, BLE, BLE-DEV, USBH)
 *   expect_no_out PORT B0 B1 B2  check that it did not
 *   expect WHAT VALUE            check state: exit 1 on mismatch; VALUE "<N" / ">N" is a bound (WHAT: store_writes,
 *                                store_tries, clocks_out, midi_out, usb_play_gain, usb_audio_full, ... below; data.ADDR: the
 *                                flash's byte at ADDR, as plat_data reads it; -1 outside its windows)
 *   reboot                       re-run boot from the simulated flash (persistence test)
 *                                (the app's plat_restart does the same, at the end of its millisecond; expect restarts)
 *   crash [PC [VEC]]             a CPU fault (hex; 02012344, 3): the .noinit crash record as felucca-bsp's fm1_fault_c
 *                                writes it (its count one up, the registers a pattern), then the restart; the boot keeps
 *                                it in the flash history (device/crashlog.c) as the device's does
 *   crash_in_save [PC [VEC]]     the same, struck while the boot's save was under way (that boot keeps nothing)
 *   power                        a power-off and on: .noinit lost (the crash record), the flash kept, then the boot
 *                                (expect crashes: the history's records, crash_count: .noinit's count, crash_boot_rc:
 *                                the last boot's save, 1 kept, 0 nothing new, < 0 not kept; the history over "sysex
 *                                F0 7D 4C 48 F7", cleared with "sysex F0 7D 4C 48 43 4C 52 F7"); the debug stats' queue
 *                                lost too (device/dbgstats.c: "crash" and "reboot" keep it)
 *   stack MAIN IRQ C1            the stacks' marks used down to these bytes left in this run (device/stacks.c; -1 not
 *                                measured; every boot starts from -1): a "crash" keeps them in its record ("stk" line),
 *                                the stats' least ever goes down with them (expect stk_ever_main / _irq / _c1)
 *   stack_guard 1                the next "crash" ran a stack into its guard (EMU_MSG bit 3: "guard 1")
 *   stats_switch 1|0             the debug stats on / off (plat_stats_enable, as an app's setting does; expect stats:
 *                                the records in the flash, stats_pending: queued, stats_on, stats_rc: the last write's;
 *                                read over "sysex F0 7D 4C 53 F7", cleared with "sysex F0 7D 4C 53 43 4C 52 F7")
 *   ota_stage / ota_unstage      an update staged as ota.c leaves it (a word at 0xE0000: the stats write nothing), and
 *                                the staging erased whole (0xE0000..0xE4FFF: the stats and the crash history with it)
 *   data FILE OFFSET             a file into the simulated data area (plat_data) at OFFSET, in any of its windows
 *                                (plat.h), up to that window's end
 *   res 0|1                      (FM1B_RES) the resource region there (1, at start) or not (0: an FM-1 updated
 *                                without it; plat_res 0)
 *   data_erase OFFSET            plat_data_erase(OFFSET), as an app calls it (expect data_rc: what it returned)
 *   data_write OFFSET HEX        plat_data_write of HEX's bytes (up to 31, e.g. F00F) at OFFSET: as the flash, each
 *                                byte becomes the old AND the new (expect data_rc; -2: no bytes)
 *   flash_dump FILE              the whole flash into FILE, a 1 MiB image at the FM-1's offsets: the data windows and
 *                                the store's objects as device/storage.c keeps them (the rest erased)
 *   flash_load FILE              such an image in (a reference FM-1's flash read whole, say): the data windows and the
 *                                store's objects from it (then "reboot": a power-on with it). FM1B_FLASH=FILE in the
 *                                environment: the simulator starts with that flash
 *   fbdump FILE                  the screen's pixels raw (240 x 240 RGB565, high byte first) into FILE
 *   flash_fail 0|1               every store save fails (1) or works (0); expect store_tries counts the attempts
 *   clock N [MS]                 N MIDI clocks in (F8) over USB, MS apart (default 21: 119 bpm)
 *   usbh plug NAME [in|out|nomidi] | usbh unplug   (usb_host) a USB-MIDI device on the port while it is a host
 *                                (plat_usb_host): both ways, input or output only, or none; _ in NAME is a space (its
 *                                product string, plat_usb_host_name); connected 150 ms after it is plugged in
 *   usbh midi B0 B1 B2 | usbh clock N [MS]   its MIDI in (PORT_USB), its clocks. While the port is a host there is no
 *                                computer: "midi USB" and "clock" are dropped, nothing goes out to it; what goes to
 *                                the device: expect_out USBH ..., expect usbh_out, usbh_clocks (expect usbh: USBH_*)
 *   said_none                    nothing was said since the last "said" (or "listen"): else a failure
 *   mirror FILE|off|refresh|nokeep|unplug   (FM1B_MIRROR) the screen and panel mirrored over USB (device/mirror.c)
 *                                as a viewer asks for it, the device's SysEx into FILE at USB's pace; nokeep: the
 *                                viewer's keep-alive stops; unplug: the cable out
 *   mirror_rate N                (FM1B_MIRROR) that pace, bytes a millisecond (48: a 64-byte USB-MIDI packet a 1 ms frame)
 *   backup FILE                  (FM1B_BACKUP) a backup over USB written to FILE (device/backup.c's image, as
 *                                tools/fm1_backup.py writes it; stopped: the unsaved saved first)
 *   restore FILE                 (FM1B_BACKUP) a backup put back as the device does once SEL answers its question:
 *                                checked whole, every object replaced, everything reloaded; refused while playing
 *                                (STOP FIRST) or when damaged: nothing changes (expect restore_rc: 0, 10 playing)
 *   backup_damage FILE           (FM1B_BACKUP) a byte of a backup file changed (at its middle)
 *   sysex HEX..                  a SysEx frame from the computer (F0 .. F7, hex, spaces allowed), as the device takes
 *                                it: to the app (APP_SYSEX: app_sysex gets the bytes between F0 and F7), or dropped (no
 *                                app hook, the app's 0, a status byte inside, longer than the device's 640-byte buffer)
 *                                (8 KiB at most); with APP_SYSEX_BYTE each byte to app_sysex_byte first, any length
 *   sysex_out                    print the app's SysEx frames (plat_sysex_send) since the last "sysex_out" (and forget them)
 *   expect_sysex HEX..           check that this frame went out since the last "sysex_out" (F0 .. F7, hex)
 *   sysex_log FILE|off           every frame the app sends from now on, appended to FILE as it went out (raw)
 *   usb unplug|plug|stall|read   the computer: the cable out, in again (a new plat_usb_session), the computer stops
 *                                reading the device's SysEx, reads again
 * FILE "out:NAME" for backup, restore, backup_damage and mirror: NAME in OUTDIR (fbdump: always there).
 * NAME_host --serve [OUTDIR] (FM1B_MIRROR): driven over stdin by a tool, as an FM-1 over USB:
 *   sx HEX      one SysEx message from the host (F0 .. F7, hex): the mirror's requests (cmd 0x50, 0x54)
 *   run MS      MS milliseconds of the device; then what it sent meanwhile, a line "sx HEX" a message, and "ok NOW"
 *   shot FILE, fbdump FILE   the screen as a PNG, as raw pixels
 *   expect WHAT VALUE   as in a script: its line, "  ok ..." or "FAIL ..."
 *   quit
 *   listen                       a screen reader starts listening (HELLO, again every 2 s): each SAY is printed
 *                                ("said: ..."; "said!:" cuts off what was being said)
 *   where                        the reader asks for everything on the screen
 *   said TEXT                    some SAY since the last "said" (or "listen") has TEXT in it: else a failure
 *   unsaid TEXT                  no SAY since the last "said" has TEXT in it: else a failure
 *   speakdump                    every page, value and message as the reader gets them (the app's app_speak_dump)
 * The app may add its own checks for expect (app_expect), the speakdump (app_speak_dump), what a reboot forgets
 * (app_host_reboot) and commands (app_host_cmd: 1 = handled; the whole line in plat_host_line): app.h defines APP_HOST_EXPECT / APP_HOST_SPEAKDUMP /
 * APP_HOST_REBOOT / APP_HOST_CMD when it has them. With APP_KEY_SENSE, app_host_key(k, vel, press) (vel 0 = up)
 * makes the readings a key gives, from app_host_tick (every millisecond) while plat_sense is on. APP_HOST_FAILS:
 * app_host_fails() is added to the run's failures at its end (the app's own checks made along the way).
 * APP_HOST_MS: app_host_ms() every simulated millisecond, after the platform's (an app's own counters).
 * FM1B_SAID_LOG=FILE: a reader listens from boot, quietly, and every SAY is appended to FILE (the audit).
 */
#define PLAT_HOST 1
#ifndef FM1B_MIRROR
#define FM1B_MIRROR 0                           /* fm1.toml mirror = true (tools/build.py) */
#endif
#ifndef FM1B_RES
#define FM1B_RES 0                              /* fm1.toml resources = FILE */
#endif
#ifndef FM1B_BACKUP
#define FM1B_BACKUP 0                           /* fm1.toml backup = true */
#endif
#ifndef FM1B_CPU2
#define FM1B_CPU2 0                             /* fm1.toml cpu2 = true */
#endif
#ifdef __linux__
#define _GNU_SOURCE                             /* (open_memstream: --serve) */
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#define __attribute__(x)

/* ------------------------------------------------------------ screen --- */
#ifdef PLAT_FB
static uint16_t *const fb = PLAT_FB;    /* the embedder's own (host/rfm1.c: the panel's LCD) */
#else
static uint16_t fb[240 * 240];          /* RGB565, byte-swapped as the panel takes it (gfx.c) */
#endif
static uint32_t lcd_changes;            /* every fill and blit (host/rfm1.c: the panel's lcd_writes) */
static void lcd_sync(void) {}
#if FM1B_MIRROR
/* screen mirroring over USB (device/mirror.c), as lcd.c copies each blit and fill to it */
static uint8_t mirror_on;
static void mirror_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px);
static void mirror_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
#endif
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
#if FM1B_MIRROR
    if (mirror_on && w && h && x < 240u && y < 240u)
        mirror_fill(x, y, x + w > 240u ? 240u - x : w, y + h > 240u ? 240u - y : h, c);
#endif
    lcd_changes++;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static uint32_t blits;
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    lcd_changes++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
#if FM1B_MIRROR
    if (mirror_on)
        mirror_blit(x, y, w, h, px);
#endif
}
#include "plat.h"
#include "app.h"                       /* before gfx.c: the app may size the canvas (APP_CANVAS_PX), as on the device */
#ifdef APP_LOCAL_MONITOR
#include "uac_mix.h"
#endif
#include "gfx.c"


/* ---------------------------------------------------------- platform --- */
#ifdef APP_TOUCH
#include "touch.h"                     /* velocity and pressure from the keys (lib/touch.c) */
#endif
#include "fm1b_config.h"
static const uint8_t HOST_WHITE_K[16] = PLAT_WHITE_KEYS, HOST_BLACK_K[11] = PLAT_BLACK_KEYS;
/* "spin": encoder steps exactly as given; an app that accelerates its knobs reads this (#ifdef PLAT_HOST) */
static uint8_t plat_host_exact;
static const char *plat_host_dir = ".";        /* the run's output directory (an app's commands: "out:NAME") */
static const char *plat_host_line = "";        /* the script's line being run (an app's command with more words) */

uint32_t plat_ms(void);
uint32_t plat_buttons(void);
uint32_t plat_keys(void);
#if FM1B_MIRROR
#ifndef MRQ
#define MRQ 4096u                               /* (felucca-bsp usb.c's mirror ring) */
#endif
#include "mirror.c"
#else
static uint32_t mirror_in_buttons(void) { return 0; }
static uint32_t mirror_in_keys(void) { return 0; }
static int32_t mirror_in_enc(int role) { (void)role; return 0; }
static uint32_t mirror_in_master(uint32_t pot) { return pot; }
static void mirror_enc(int role, int32_t n) { (void)role, (void)n; }
static void mirror_leds(uint32_t buttons, uint32_t keys, uint32_t db, uint32_t dk) { (void)buttons, (void)keys, (void)db, (void)dk; }
static void mirror_mid(uint32_t buttons, uint32_t keys) { (void)buttons, (void)keys; }
#endif
static uint32_t now_ms, held_btn, held_keys, master = 2048;
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys, dim_btn, dim_keys;
static uint32_t lights_b, lights_db;    /* plat_lights' buttons as given (PLAY's green apart), and its glow */
static int lights_glow;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn | mirror_in_buttons(); }   /* (+ the host's: the mirror's remote) */
uint32_t plat_keys(void) { return held_keys | mirror_in_keys(); }
static uint32_t plat_remote_keys(void) { return 0; }   /* (the device's remote control: the script is this one's) */
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role] + mirror_in_enc(role);
    enc_acc[role] = 0;
    if (v)
        mirror_enc(role, v);
    return v;
}
uint32_t plat_master(void) { return mirror_in_master(master); }
#ifdef __EMSCRIPTEN__
int plat_battery(void) { return -1; }  /* the browser emulator has no battery: not read, so none is shown */
#ifdef APP_BATTERY_RAW
int32_t plat_battery_raw(void) { return 0; }     /* (not read, as a device's before its first reading) */
#endif
#else
int plat_battery(void) { return 3; }   /* (the host simulator: full) */
#ifdef APP_BATTERY_RAW
int32_t plat_battery_raw(void) { return 620; }   /* (full: over the third bar's 591, as plat_battery's 3) */
#endif
#endif
#ifdef APP_DIM_LEVELS
static uint32_t dim_ns_app, dim_glow;   /* plat_dim_ns's pulse (0: the glow's); the glow plat_lights last got */
void plat_dim_ns(uint32_t ns)           /* clamped as the FM-1's scan takes it: 500 .. 4,000 */
{
    dim_ns_app = !ns ? 0u : ns < 500u ? 500u : ns > 4000u ? 4000u : ns;
}
/* the pulse the dim ones have now (ns): the app's, else the glow's (1,600 or 3,200), 0 with no glow */
static uint32_t dim_pulse(void) { return dim_ns_app ? dim_ns_app : dim_glow == 1 ? 1600u : dim_glow ? 3200u : 0u; }
#endif
static int host_hp;                                /* the headphone jack: a script's "jack in" / "jack out" (0: none) */
int plat_headphones(void) { return host_hp; }   /* (debounced on the device: here the script's state at once) */
/* the lights ("leds"): bright, and dim (glow 0: none); PLAY's green counts as PLAY */
void plat_lights(uint32_t b, uint32_t k, uint32_t db, uint32_t dk, int glow)
{
    lights_b = b, lights_db = db, lights_glow = glow;
#ifdef APP_DIM_LEVELS
    dim_glow = glow > 0 ? (uint32_t)glow : 0u;
#endif
    lit_btn = (b & ((1u << NB) - 1u)) | (b & LED_PLAY_GREEN ? 1u << B_PLAY : 0u);
    lit_keys = k;
    db = (db & ((1u << NB) - 1u)) | (db & LED_PLAY_GREEN ? 1u << B_PLAY : 0u);
    dim_btn = glow ? db & ~lit_btn : 0u;
    dim_keys = glow ? dk & ~k : 0u;
    {   /* the mirror's view: PLAY lit (either LED), and bit 15 PLAY lit red */
        uint32_t play = 1u << B_PLAY;
        mirror_leds((b & ((1u << NB) - 1u) & ~play) | (b & (play | LED_PLAY_GREEN) ? play : 0u) |
                    (b & play ? 1u << 15 : 0u), k, dim_btn, dim_keys);
    }
}
static uint32_t mid_keys;               /* plat_lights_mid's keys: "leds" (and the browser's web_dim_keys) shows them as
                                         * the glow */
void plat_lights_mid(uint32_t b, uint32_t k)
{
    mid_keys = k & ~lit_keys;
    mirror_mid(b & ((1u << NB) - 1u), k);
}
static int glow_on = 1;                 /* the device's button glow (plat_glow) */
void plat_glow(int on) { glow_on = on; }
void plat_play_red(int red) { (void)red; }      /* the device's PLAY colour: green, red while recording */
void plat_leds(uint32_t b, uint32_t k) { plat_lights(b, k, glow_on ? ~b : 0u, 0, glow_on ? 2 : 0); }
uint32_t plat_lock(void) { return 0; }  /* one thread: the tick runs between the main loop's steps (run_ms) */
void plat_unlock(uint32_t f) { (void)f; }
static void hsx_ms(void);
void plat_yield(void)
{
    hsx_ms();                                   /* (a wait inside the app: the computer reads meanwhile) */
#ifdef APP_YIELD
    app_yield();
#endif
}
static int sense_on;                    /* the keys' readings: the simulator has none to give (sense_on only) */
void plat_sense(int on) { sense_on = on; }
uint32_t plat_analog_mask(void) { return 0xFFFFFu; }    /* the keys on rows PA7 / PA8: keys 0..19 (the top row: 20..26) */

#define MQ 256
static uint32_t min_q[MQ], mi_w, mi_r;
static FILE *midi_log;
static uint32_t midi_out_count, clock_out_count;
static uint32_t min_port[MQ];
static uint32_t out_pkt[4096], out_to[4096], n_out;   /* what went out since the script's last "out" */
static int ble_state, ble_conn, bdev_state;   /* bdev_state: the Bluetooth MIDI device (BLE_DEV_*) */
static uint32_t midi_n[NPORTS][2], midi_t[NPORTS][2];   /* plat_midi_count */
static void midi_counted(int port, int out, uint32_t pkt)
{
    if ((pkt >> 8 & 0xFFu) >= 0xF8u)             /* clock, start, stop, active sensing: not counted, no light */
        return;
    midi_n[port][out]++;
    midi_t[port][out] = plat_ms() | 1u;
}
uint32_t plat_midi_count(int port, int out) { return port >= 0 && port < NPORTS ? midi_n[port][!!out] : 0; }
static uint32_t ble_listening(void)            /* the Bluetooth links listening, as outs */
{
    return (ble_conn ? TO_BLE_HOST : 0u) | (bdev_state == BLE_DEV_READY ? TO_BLE_DEV : 0u);
}
static uint32_t usbh_to_usb(void);             /* TO_USB while someone listens there: a computer, or the host's device */
static int usbh_port(void);
static void usbh_sent(uint32_t pkt);
/* the host's MIDI out is logged at once and never fills: the device's whole ring free, for whoever listens */
uint32_t plat_midi_out_room(uint32_t to)
{
    return to & (usbh_to_usb() | ble_listening()) ? 64u : 0u;
}
static uint32_t usb_links;                      /* plat_midi_out_link: one USB connection, all run long */
uint32_t plat_midi_out_link(void) { return usb_links; }
/* the USB MIDI host (plat_usb_host): a device on the port, as the script plugs it ("usbh plug NAME [in|out|nomidi]",
 * "usbh unplug"), its MIDI in ("usbh midi B0 B1 B2"), what the FM-1 sends it (expect_out USBH; expect usbh_out,
 * usbh_clocks). As felucca-bsp's host: boot is a device; the mode changes at the platform's next look; a device
 * plugged in connects USBH_CONNECT_MS later (its enumeration; one without MIDI never: NO MIDI said once); the port's
 * MIDI is PORT_USB / TO_USB both ways and there is no computer meanwhile (the script's "midi USB" is dropped, nothing
 * goes to a computer); each session's end (the mode changed, the device lost) gives plat_usb_lost once, each connection
 * a new plat_midi_out_link. expect usbh: USBH_*; usbh_in. Without FM1B_USB_HOST the port stays a device (USBH_OFF) */
#ifndef FM1B_USB_HOST
#define FM1B_USB_HOST 0
#endif
#define USBH_CONNECT_MS 150u
#define TO_USBH_SIM (1u << 5)                  /* (the out log's tag: sent to the host's device, not to a computer) */
static struct {
    uint8_t want, on, plugged, midi_in, midi_out, conn, msg, lost, said;
    uint32_t t_plug, in, out, clocks;
    char name[32];
} uhs;
static void usbh_step(void)                     /* the mode and the connection, as the host's task moves them */
{
    if (uhs.want != uhs.on) {
        uhs.on = uhs.want;
        uhs.conn = 0;
        uhs.lost = 1;                           /* (the session before: the computer's, or the device's) */
        uhs.msg = uhs.on ? USBH_MSG_ON : USBH_MSG_OFF;
        uhs.t_plug = now_ms;
        uhs.said = 0;
        if (!uhs.on)
            usb_links++;                        /* (a computer again) */
    }
    if (uhs.on && uhs.plugged && !uhs.conn && !uhs.said && now_ms - uhs.t_plug >= USBH_CONNECT_MS) {
        uhs.said = 1;
        if (uhs.midi_in || uhs.midi_out) {
            uhs.conn = 1;
            uhs.msg = USBH_MSG_MIDI;
            usb_links++;
        } else {
            uhs.msg = USBH_MSG_NOMIDI;
        }
    }
}
static void usbh_reboot(void)                   /* the script's reboot: the port a device again, what is plugged in stays */
{
    uhs.want = uhs.on = uhs.conn = uhs.lost = uhs.said = 0;
    uhs.msg = USBH_MSG_NONE;
    uhs.t_plug = now_ms;
}
static int usbh_port(void)                      /* the port is a host */
{
    usbh_step();
    return uhs.on;
}
static uint32_t usbh_to_usb(void) { return !usbh_port() || (uhs.conn && uhs.midi_out) ? TO_USB : 0u; }
void plat_usb_host(int on) { uhs.want = (uint8_t)(FM1B_USB_HOST && on); }
int plat_usb_host_state(void)
{
    usbh_step();
    return !uhs.on ? USBH_OFF : uhs.conn ? USBH_CONNECTED : USBH_LOOKING;
}
int plat_usb_host_msg(void)
{
    int m;
    usbh_step();
    m = uhs.msg;
    uhs.msg = USBH_MSG_NONE;
    return m;
}
int plat_usb_lost(void)
{
    usbh_step();
    if (!uhs.lost)
        return 0;
    uhs.lost = 0;
    return 1;
}
const char *plat_usb_host_name(void)
{
    usbh_step();
    return uhs.conn ? uhs.name : "";
}
static uint32_t mk_pkt(const char *a, const char *b, const char *c);
/* the script's usbh: plug NAME [in|out|nomidi] (both ways by default; _ in NAME a space), unplug, midi B0 B1 B2 (from the device) */
static int usbh_cmd(const char *a, const char *b, const char *c, const char *d)
{
    usbh_step();
    if (!strcmp(a, "plug") && b[0]) {
        snprintf(uhs.name, sizeof uhs.name, "%s", b);
        for (char *p = uhs.name; *p; p++)
            if (*p == '_')
                *p = ' ';                       /* (the script's words: "Launchkey_Mini" is "Launchkey Mini") */
        uhs.plugged = 1;
        uhs.midi_in = (uint8_t)(strcmp(c, "out") && strcmp(c, "nomidi"));
        uhs.midi_out = (uint8_t)(strcmp(c, "in") && strcmp(c, "nomidi"));
        uhs.t_plug = now_ms;
        uhs.said = 0;
        return 0;
    }
    if (!strcmp(a, "unplug")) {
        if (uhs.conn) {
            uhs.lost = 1;
            uhs.msg = USBH_MSG_LOST;
        }
        uhs.plugged = uhs.conn = 0;
        return 0;
    }
    if (!strcmp(a, "midi")) {
        if (!uhs.conn || !uhs.midi_in) {
            printf("  usbh midi: no device connected that sends: dropped\n");
            return 0;
        }
        min_q[mi_w % MQ] = mk_pkt(b, c, d) & ~0xF0u;     /* (cable 0: the host drops the cable number) */
        min_port[mi_w++ % MQ] = PORT_USB;
        uhs.in++;
        return 0;
    }
    printf("FAIL usbh %s: plug NAME [in|out|nomidi], unplug, midi B0 B1 B2\n", a);
    return 1;
}
static void usbh_sent(uint32_t pkt)             /* plat_midi_send's TO_USB while the port is a host */
{
    if (((pkt >> 8) & 0xFFu) == 0xF8u)
        uhs.clocks++;
    else
        uhs.out++;
}
static int usbh_expect(const char *what, int *got)    /* the model's state for expect: 1 = one of its */
{
    usbh_step();
    if (!strcmp(what, "usbh"))
        *got = plat_usb_host_state();
    else if (!strcmp(what, "usbh_in"))         /* messages in from the device (real-time too) */
        *got = (int)uhs.in;
    else if (!strcmp(what, "usbh_out"))        /* messages out to it, clocks left out */
        *got = (int)uhs.out;
    else if (!strcmp(what, "usbh_clocks"))     /* F8s out to it */
        *got = (int)uhs.clocks;
    else
        return 0;
    return 1;
}
uint32_t plat_midi_ago(int port, int out)
{
    return port < 0 || port >= NPORTS || !midi_t[port][!!out] ? 0xFFFFFFFFu : plat_ms() - midi_t[port][!!out];
}
static uint32_t midi_in_from;
uint32_t plat_midi_in_from(void) { return midi_in_from; }
uint32_t plat_midi_in_t24(void) { return plat_ticks24(); }   /* (the script's messages come in when they are read) */
int plat_midi_in(uint32_t *pkt, int *port)       /* the script's "midi" (BLE-DEV's on cable 1, as radio.c has them) */
{
    int p;
    uint32_t q;
    if (mi_r == mi_w)
        return 0;
    p = (int)min_port[mi_r % MQ];
    if (port)
        *port = p;
    q = min_q[mi_r++ % MQ];
    midi_in_from = p == PORT_USB ? TO_USB : p != PORT_BLE ? 0u : (q >> 4 & 0xFu) == 1u ? TO_BLE_DEV : TO_BLE_HOST;
    *pkt = q & ~0xF0u;
    if (p >= 0 && p < NPORTS)
        midi_counted(p, 0, *pkt);
    return 1;
}
void plat_midi_send(uint32_t pkt, uint32_t to)
{
    to &= usbh_to_usb() | ble_listening();        /* as the device: nothing queues for a link not there */
    if (!to)
        return;
    if ((to & TO_USB) && usbh_port()) {         /* (the host's device, no computer: tagged in the out log) */
        usbh_sent(pkt);
        to = (to & ~TO_USB) | TO_USBH_SIM;
        midi_counted(PORT_USB, 1, pkt);
    }
    if (to & TO_USB)
        midi_counted(PORT_USB, 1, pkt);
    if (to & TO_BLE)
        midi_counted(PORT_BLE, 1, pkt);
    midi_out_count++;
    if (n_out < 4096) {
        out_pkt[n_out] = pkt;
        out_to[n_out++] = to;
    }
    if (((pkt >> 8) & 0xFF) == 0xF8) {
        clock_out_count++;
        return;
    }
    if (midi_log)
        fprintf(midi_log, "%u ms: %02X %02X %02X ->%s%s%s\n", now_ms, (pkt >> 8) & 0xFF, (pkt >> 16) & 0xFF,
                (pkt >> 24) & 0xFF, to & TO_USB ? " USB" : to & TO_USBH_SIM ? " USBH" : "", to & TO_BLE_HOST ? " BLE" : "",
                to & TO_BLE_DEV ? " BLE-DEV" : "");
}

/* Bluetooth (FM1B_BLE): a stand-in for the stack, ready a second after the start, and its log */
static uint32_t ble_t0;
static char ble_log[8][40];
static uint32_t ble_log_n;
static void ble_say(const char *s) { snprintf(ble_log[ble_log_n++ % 8], 40, "%s", s); }
int plat_ble_state(void) { return ble_state; }
static int ble_link;                            /* "ble link": a computer linked, not listening yet */
int plat_ble_connected(void) { return ble_conn; }
int plat_ble_linked(void) { return ble_conn || ble_link; }
int plat_ble_stream(const struct plat_ble_stream *s) { (void)s; return 0; }   /* (send by plat_midi_send) */
void plat_ble_link_info(uint32_t v[7])
{
    int i;
    for (i = 0; i < 7; i++)
        v[i] = 0;
}
void plat_ble_start(void)
{
    if (ble_state != BLE_OFF || !FM1B_BLE)
        return;
    ble_state = BLE_STARTING;
    ble_t0 = now_ms;
    ble_log_n = 0;
    ble_say("BT: starting");
}
/* Bluetooth MIDI devices (FM1B_BLE_CENTRAL): one stand-in, "BLE MIDI", found half a second into a scan;
 * connecting takes 0.3 s; "ble device off" takes it away */
static uint32_t bdev_t;
static void bdev_report(void);
static void ble_tick(void)
{
    if (ble_state == BLE_STARTING && now_ms - ble_t0 >= 1000u) {
        ble_state = BLE_READY;
        ble_say("BT init ok: starting BLE");
    }
    if (bdev_state == BLE_DEV_CONNECTING && now_ms - bdev_t >= 300u) {
        bdev_state = BLE_DEV_READY;
        ble_say("central: MIDI ready");
    }
    bdev_report();
}
void plat_ble_scan(int on)
{
    if (!FM1B_BLE_CENTRAL || ble_state != BLE_READY)
        return;
    if (on && bdev_state == BLE_DEV_OFF) {
        bdev_state = BLE_DEV_SCANNING;
        bdev_t = now_ms;
    } else if (!on && bdev_state == BLE_DEV_SCANNING) {
        bdev_state = BLE_DEV_OFF;
    }
}
int plat_ble_found(int i, char *name, int *rssi)
{
    if (bdev_state != BLE_DEV_SCANNING || i != 0 || now_ms - bdev_t < 500u)
        return 0;
    if (name)
        snprintf(name, 21, "BLE MIDI");
    if (rssi)
        *rssi = -50;
    return 1;
}
int plat_ble_connect(int i)
{
    if (!plat_ble_found(i, 0, 0))
        return -1;
    bdev_state = BLE_DEV_CONNECTING;
    bdev_t = now_ms;
    return 0;
}
void plat_ble_disconnect(void)
{
    if (bdev_state == BLE_DEV_READY)
        ble_say("central: disconnected");
    bdev_state = BLE_DEV_OFF;
}
int plat_ble_device(void) { return bdev_state; }
int plat_ble_passkey(void) { return -1; }
/* the devices one by one (plat.h plat_ble_connect_peer): the stand-in's advertising report (ADV_IND, a random static
 * address, the BLE-MIDI service, its name) to the app every 100 ms of a scan once it is found; connecting to its
 * address as plat_ble_connect does; no pairing, so no keys */
#ifdef APP_BLE_PEERS
static const uint8_t BDEV_ADDR[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0xC6};
static uint32_t bdev_said;
static void bdev_report(void)
{
    static const uint8_t ad[] = {2, 0x01, 0x06,
                                 17, 0x07, 0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E,
                                 0xB8, 0x03,
                                 9, 0x09, 'B', 'L', 'E', ' ', 'M', 'I', 'D', 'I'};
    uint8_t pdu[2 + 6 + sizeof ad];
    if (bdev_state != BLE_DEV_SCANNING || now_ms - bdev_t < 500u || now_ms - bdev_said < 100u)
        return;
    bdev_said = now_ms;
    pdu[0] = 0x40;                              /* (ADV_IND, TxAdd: random) */
    pdu[1] = (uint8_t)(6u + sizeof ad);
    memcpy(pdu + 2, BDEV_ADDR, 6);
    memcpy(pdu + 8, ad, sizeof ad);
    app_ble_scan_report(pdu, sizeof pdu, 60u);
}
int plat_ble_connect_peer(const struct plat_ble_peer *p)
{
    if (!FM1B_BLE_CENTRAL || ble_state != BLE_READY || bdev_state >= BLE_DEV_CONNECTING || memcmp(p->addr, BDEV_ADDR, 6))
        return -1;
    bdev_state = BLE_DEV_CONNECTING;
    bdev_t = now_ms;
    return 0;
}
int plat_ble_device_fail(uint32_t *code)
{
    if (code)
        *code = 0;
    return PLAT_BLE_CF_NONE;
}
uint32_t plat_ble_device_flags(void) { return 0; }
int plat_ble_rpa_resolve(const uint8_t irk[16], const uint8_t addr[6]) { (void)irk, (void)addr; return 0; }
#else
static void bdev_report(void) {}
#endif
void plat_ble_enable(int on)
{
    if (!on) {
        bdev_state = BLE_DEV_OFF;
        ble_conn = ble_link = 0;
        ble_state = BLE_OFF;
        ble_say("switched off");
    } else {
        plat_ble_start();
    }
}
#if FM1B_BT_AUDIO
/* Bluetooth audio (FM1B_BT_AUDIO): a stand-in for the A2DP source. The devices around are the script's ("bta add NAME",
 * "bta drop NAME": it leaves, a link to it is lost); a scan lists them after 1 s, a connect streams 0.3 s later (one
 * the script has not added answers nothing: FAILED after 1 s); four are remembered, the last one first. The sound
 * stays the simulator's own output (the stream is not simulated) */
#define BTH_N 8
static char bth_near[BTH_N][17], bth_found[BTH_N][17], bth_mem[4][17];
static uint32_t bth_nnear, bth_nfound, bth_nmem, bth_t;
static int bth_st = PLAT_BTA_OFF, bth_out = 1, bth_lost;
static int bth_tried;
static int bth_is_near(const char *nm)
{
    uint32_t i;
    for (i = 0; i < bth_nnear; i++)
        if (!strcmp(bth_near[i], nm))
            return 1;
    return 0;
}
static int bth_rest(void)                         /* the state with no link and nothing under way (bt_audio.c's MAN) */
{
    return bth_tried ? PLAT_BTA_FAILED : bth_nfound ? PLAT_BTA_FOUND : PLAT_BTA_NONE;
}
static void bth_front(uint32_t i)                 /* remembered device i becomes the last one */
{
    char t[17];
    memcpy(t, bth_mem[i], 17);
    memmove(bth_mem[1], bth_mem[0], 17u * i);
    memcpy(bth_mem[0], t, 17);
}
#ifdef APP_BLE_STORE
/* APP_BLE_STORE: the remembered devices from the app's record at the first plat_bta_on after a boot, and back to it
 * whenever the list changes (as the device's stack keeps them: their names here; an address of its own, k + 1) */
static uint8_t bth_loaded;
static void bth_load(void)
{
    struct plat_ble_rec r;
    uint32_t k;
    memset(&r, 0, sizeof r);
    bth_loaded = 1;
    if (!app_ble_load(&r))
        return;
    for (k = 0; k < r.bta_n && k < 4u; k++)
        memcpy(bth_mem[k], r.bta[k].name, 17);
    bth_nmem = k;
}
static void bth_keep(void)
{
    static const uint8_t ADDR[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0xC6};   /* (a static random one: the top bits 11) */
    struct plat_ble_rec r;
    uint32_t k;
    memset(&r, 0, sizeof r);
    if (!app_ble_load(&r))
        memcpy(r.addr, ADDR, 6);                  /* (none kept yet: the stack makes its address, as on the device) */
    memset(r.bta, 0, sizeof r.bta);
    r.bta_n = (uint8_t)bth_nmem;
    for (k = 0; k < bth_nmem; k++) {
        r.bta[k].addr[0] = (uint8_t)(k + 1u);
        r.bta[k].bonded = 1;
        memcpy(r.bta[k].name, bth_mem[k], 17);
    }
    (void)app_ble_save(&r);
}
#else
static uint8_t bth_loaded = 1;
static void bth_load(void) {}
static void bth_keep(void) {}
#endif
static void bta_boot(void)                        /* a boot (the simulator's reboot too): nothing up, the list to load */
{
    bth_st = PLAT_BTA_OFF;
    bth_nfound = 0;
#ifdef APP_BLE_STORE
    bth_nmem = 0;
    bth_loaded = 0;
#endif
}
/* fm1.toml bt_audio_mem = "app" (FM1B_BTA_MEM): the block as the device's stack takes it (the same size: bt_page.c
 * BP_MEM_BYTES, 40 x 332 + 40 x 2 queued packets and 12 receptions of 364 B), written while on (as the device's queue
 * is: a block taken back too early, or one too short, fails under ASan) */
#define BTH_MEM_BYTES (13360u + 12u * 364u)
static uint8_t *bth_mem_p;
uint32_t plat_bta_mem_bytes(void) { return FM1B_BTA_MEM ? BTH_MEM_BYTES : 0u; }
int plat_bta_mem(void *p)
{
    if (!FM1B_BTA_MEM)
        return p ? -1 : 0;
    if (!p && bth_st != PLAT_BTA_OFF)
        return -1;
    if (p)
        memset(p, 0x5A, BTH_MEM_BYTES);
    bth_mem_p = (uint8_t *)p;
    return 0;
}
static void bta_tick(void)
{
    if (bth_mem_p && bth_st != PLAT_BTA_OFF)
        bth_mem_p[(now_ms * 7u) % BTH_MEM_BYTES]++;   /* (the radio's queue at work: a block gone is written to) */
    if (bth_st == PLAT_BTA_SCANNING && now_ms - bth_t >= 1000u) {
        uint32_t i;
        for (i = 0, bth_nfound = 0; i < bth_nnear; i++)
            memcpy(bth_found[bth_nfound++], bth_near[i], 17);
        bth_st = bth_rest();
    }
    if (bth_st == PLAT_BTA_PAGING && now_ms - bth_t >= (bth_is_near(bth_mem[0]) ? 300u : 1000u)) {
        bth_tried = 1;
        bth_st = bth_is_near(bth_mem[0]) ? PLAT_BTA_STREAMING : PLAT_BTA_FAILED;
        bth_out = 1;
    }
}
void plat_bta_on(int on)
{
    if (FM1B_BTA_MEM && on && !bth_mem_p)
        return;
    if (on && bth_st == PLAT_BTA_OFF) {
        if (ble_state == BLE_OFF)
            plat_ble_start();
        if (!bth_loaded)
            bth_load();
        bth_tried = 0;
        bth_st = bth_rest();
    } else if (!on)
        bth_st = PLAT_BTA_OFF;
}
void plat_bta_scan(int on)
{
    if (bth_st == PLAT_BTA_OFF)
        return;
    if (on == 2 && bth_st == PLAT_BTA_SCANNING)
        return;                                   /* (again, the list kept: only when idle) */
    if (on && bth_st != PLAT_BTA_STREAMING && bth_st != PLAT_BTA_PAGING) {
        if (on != 2)
            bth_nfound = 0;                       /* (a fresh list) */
        bth_st = PLAT_BTA_SCANNING;
        bth_t = now_ms;
    } else if (!on && bth_st == PLAT_BTA_SCANNING)
        bth_st = bth_rest();
}
int plat_bta_found(int i, char *name)
{
    if (i < 0 || (uint32_t)i >= bth_nfound)
        return 0;
    if (name)
        memcpy(name, bth_found[i], 17);
    return 1;
}
int plat_bta_known(int i, char *name)
{
    if (i < 0 || (uint32_t)i >= bth_nmem)
        return 0;
    if (name)
        memcpy(name, bth_mem[i], 17);
    return 1;
}
static void bth_page(void)
{
    bth_st = PLAT_BTA_PAGING;
    bth_t = now_ms;
}
void plat_bta_pick(int i)
{
    uint32_t k;
    if (bth_st == PLAT_BTA_OFF || i < 0 || (uint32_t)i >= bth_nfound)
        return;
    for (k = 0; k < bth_nmem && strcmp(bth_mem[k], bth_found[i]); k++)
        ;
    if (k == bth_nmem) {                          /* (new: remembered, the oldest one forgotten when four are) */
        if (bth_nmem < 4u)
            bth_nmem++;
        k = bth_nmem - 1u;
        memcpy(bth_mem[k], bth_found[i], 17);
    }
    bth_front(k);
    bth_keep();
    bth_page();
}
int plat_bta_reconnect(int i)
{
    if (bth_st == PLAT_BTA_OFF || i < 0 || (uint32_t)i >= bth_nmem)
        return -1;
    if (i == 0 && (bth_st == PLAT_BTA_STREAMING || bth_st == PLAT_BTA_PAGING))
        return 0;                                 /* (that device already) */
    bth_front((uint32_t)i);
    bth_keep();
    bth_page();
    return 0;
}
void plat_bta_forget(int i)
{
    if (i < 0 || (uint32_t)i >= bth_nmem)
        return;
    memmove(bth_mem[i], bth_mem[i + 1], 17u * (bth_nmem - 1u - (uint32_t)i));
    bth_nmem--;
    bth_keep();
}
void plat_bta_hangup(void)
{
    if (bth_st == PLAT_BTA_STREAMING || bth_st == PLAT_BTA_PAGING)
        bth_st = bth_rest();
}
int plat_bta_state(void) { return bth_st; }
void plat_bta_output(int to_device)
{
    if (bth_st == PLAT_BTA_STREAMING)
        bth_out = to_device != 0;
}
int plat_bta_out_device(void) { return bth_out; }
int plat_bta_lost(void)
{
    int w = bth_lost;
    bth_lost = PLAT_BTA_LOST_NONE;
    return w;
}
int plat_bta_timed_out(void) { return 0; }
int32_t plat_bta_latency_us(void) { return bth_st == PLAT_BTA_STREAMING ? 20000 : -1; }   /* (a queue's worth) */
static int bta_script(const char *what, const char *name, int lineno)   /* "bta add NAME", "bta drop NAME" -> 1 bad */
{
    uint32_t i;
    if (!strcmp(what, "add") && bth_nnear < BTH_N && !bth_is_near(name)) {
        snprintf(bth_near[bth_nnear++], 17, "%s", name);
        return 0;
    }
    if (!strcmp(what, "drop")) {
        for (i = 0; i < bth_nnear && strcmp(bth_near[i], name); i++)
            ;
        if (i < bth_nnear) {
            memmove(bth_near[i], bth_near[i + 1], 17u * (bth_nnear - 1u - i));
            bth_nnear--;
        }
        if (bth_st == PLAT_BTA_STREAMING && !strcmp(bth_mem[0], name)) {
            bth_st = PLAT_BTA_FAILED;             /* (its link lost: said once, plat_bta_lost) */
            bth_lost = PLAT_BTA_LOST_STREAMING;
        }
        return 0;
    }
    printf("line %d: bta %s %s: add or drop NAME\n", lineno, what, name);
    return 1;
}
#endif
#if FM1B_WIFI
/* WiFi (FM1B_WIFI): a stand-in, two networks around ("Home", kept, and "Cafe", open), on 0.5 s after asked, joined
 * 1.5 s after (an address 0.5 s later); the network code (fm1.toml net) is the device's alone: plat_net_task runs
 * nothing here */
static uint8_t wf_st;
static uint32_t wf_t;
static char wf_ssid[33], wf_kept[6][33] = {"Home"};
static void wifi_tick(void)
{
    if (wf_st == WIFI_STARTING && now_ms - wf_t >= 500u) {
        wf_st = wf_kept[0][0] ? WIFI_JOINING : WIFI_ON;
        snprintf(wf_ssid, sizeof wf_ssid, "%s", wf_kept[0]);
        wf_t = now_ms;
    }
    if (wf_st == WIFI_JOINING && now_ms - wf_t >= 1000u) {
        wf_st = WIFI_ADDRESS;
        wf_t = now_ms;
    }
    if (wf_st == WIFI_ADDRESS && now_ms - wf_t >= 500u)
        wf_st = WIFI_CONNECTED;
}
void plat_wifi_on(int on)
{
    if (on && wf_st == WIFI_OFF) {
        wf_st = WIFI_STARTING;
        wf_t = now_ms;
        wf_ssid[0] = 0;
    } else if (!on) {
        wf_st = WIFI_OFF;
    }
}
int plat_wifi_state(void) { return wf_st; }
const char *plat_wifi_why(void) { return ""; }
const char *plat_wifi_ssid(void) { return wf_ssid; }
uint32_t plat_wifi_ip(void) { return wf_st == WIFI_CONNECTED ? 0x2A01A8C0u : 0; }   /* 192.168.1.42 */
int plat_wifi_rssi(void) { return wf_st == WIFI_CONNECTED ? -48 : 0; }
int plat_wifi_scan(void) { return wf_st >= WIFI_ON ? 0 : -1; }
int plat_wifi_found_channel(int i) { return wf_st >= WIFI_ON && i >= 0 && i <= 1 ? (i ? 11 : 6) : 0; }
int plat_wifi_found(int i, char *ssid, int *rssi, int *secured)
{
    static const char *const N[2] = {"Home", "Cafe"};
    if (wf_st < WIFI_ON || i < 0 || i > 1)
        return 0;
    if (ssid)
        snprintf(ssid, 33, "%s", N[i]);
    if (rssi)
        *rssi = i ? -71 : -48;
    if (secured)
        *secured = !i;
    return 1;
}
int plat_wifi_join(const char *ssid, const char *pass)
{
    (void)pass;
    if (wf_st < WIFI_ON || !ssid || !ssid[0])
        return -1;
    snprintf(wf_ssid, sizeof wf_ssid, "%s", ssid);
    wf_st = WIFI_JOINING;
    wf_t = now_ms;
    return 0;
}
int plat_wifi_saved(int i, char *ssid)
{
    if (i < 0 || i > 5 || !wf_kept[i][0])
        return 0;
    if (ssid)
        snprintf(ssid, 33, "%s", wf_kept[i]);
    return 1;
}
void plat_wifi_forget(int i)
{
    if (i >= 0 && i < 6)
        memmove(wf_kept[i], wf_kept[i + 1], (size_t)(5 - i) * sizeof wf_kept[0]), memset(wf_kept[5], 0, 33);
}
int plat_wifi_kept(const char *ssid)
{
    int i;
    for (i = 0; ssid && i < 6; i++)
        if (wf_kept[i][0] && !strcmp(wf_kept[i], ssid))
            return 1;
    return 0;
}
uint32_t plat_wifi_scans(void) { return wf_st >= WIFI_ON ? 1u : 0u; }
void plat_wifi_rx(uint32_t *frames, int *rssi)
{
    if (frames)
        *frames = wf_st >= WIFI_ON ? 40u : 0u;
    if (rssi)
        *rssi = wf_st >= WIFI_ON ? -55 : 0;
}
int plat_net_task(void (*fn)(void *), void *arg, const char *name) { (void)fn, (void)arg, (void)name; return -1; }
#endif
uint32_t plat_ble_log_total(void) { return ble_log_n; }
const char *plat_ble_log_line(int back)
{
    if (back < 0 || (uint32_t)back >= ble_log_n || back >= 8)
        return "";
    return ble_log[(ble_log_n - 1u - (uint32_t)back) % 8];
}
void plat_midi_out(uint32_t pkt) { plat_midi_send(pkt, TO_USB); }
void plat_log(const char *line) { ble_say(line); }
/* the data area and its two more windows (plat.h): a simulated flash from the first window to the last (the gap
 * between, the update's staging, refused as on the device), erased at the start, kept across a simulated reboot.
 * FM1B_XDATA2: the fourth window too, just above the third, the store's objects there refused as on the device */
#define DATA_BASE PLAT_XDATA0_LO
#if FM1B_XDATA2
#include "plat_xdata2.h"
#define DATA_TOP PLAT_XDATA2_HI
#define DATA_STORE_HIT(off, n) plat_xdata2_store_hit((off), (n))
#else
#define DATA_TOP PLAT_XDATA1_HI
#define DATA_STORE_HIT(off, n) 0
#endif
#ifdef PLAT_DATA_AREA
static uint8_t *const data_area = PLAT_DATA_AREA;   /* the embedder's own (host/rfm1.c: in its flash image) */
#else
static uint8_t data_area[DATA_TOP - DATA_BASE];
#endif
static uint8_t data_ready;
static uint32_t data_writes;            /* every erase and write */
static int data_cmd_rc;                   /* the last script data_erase / data_write: what it returned (expect data_rc) */
static void data_init(void)
{
    if (!data_ready) {
        memset(data_area, 0xFF, DATA_TOP - DATA_BASE);
        data_ready = 1;
    }
}
static uint32_t data_window_hi(uint32_t off)   /* the end of the window that holds off, 0 = none */
{
    static const uint32_t W[][2] = {{PLAT_DATA_LO, PLAT_DATA_HI}, {PLAT_XDATA0_LO, PLAT_XDATA0_HI},
                                    {PLAT_XDATA1_LO, PLAT_XDATA1_HI},
#if FM1B_XDATA2
                                    {PLAT_XDATA2_LO, PLAT_XDATA2_HI},
#endif
    };
    uint32_t i;
    for (i = 0; i < sizeof W / sizeof W[0]; i++)
        if (off >= W[i][0] && off < W[i][1])
            return W[i][1];
    return 0;
}
const uint8_t *plat_data(uint32_t off)
{
    data_init();
    return PLAT_DATA_OK(off, 1u) ? &data_area[off - DATA_BASE] : 0;
}
int plat_data_erase(uint32_t off)
{
    data_init();
    if ((off & 0xFFFu) || !PLAT_DATA_OK(off, 0x1000u) || DATA_STORE_HIT(off, 0x1000u))
        return -1;
    memset(&data_area[off - DATA_BASE], 0xFF, 0x1000u);
    data_writes++;
    return 0;
}
int plat_data_write(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    data_init();
    if (!PLAT_DATA_OK(off, n) || DATA_STORE_HIT(off, n))
        return -1;
    for (i = 0; i < n; i++)                       /* as flash: bits only go from 1 to 0 */
        data_area[off - DATA_BASE + i] &= ((const uint8_t *)src)[i];
    data_writes++;
    return 0;
}
/* the app's SysEx to the computer (plat_sysex_send): the computer reads it at once; each frame is kept for the script
 * ("sysex_out", "expect_sysex") and written to the "sysex_log" file */
#define HSX_KEEP 65536u
static uint8_t hsx_out[HSX_KEEP];               /* the frames sent since the last "sysex_out", one after another */
static uint32_t hsx_n, hsx_lost;
static FILE *hsx_log;
static void hsx_keep(const uint8_t *p, uint32_t n)
{
    if (hsx_log)
        fwrite(p, 1, n, hsx_log);
    if (n <= HSX_KEEP - hsx_n) {
        memcpy(hsx_out + hsx_n, p, n);
        hsx_n += n;
    } else {
        hsx_lost += n;
    }
}
/* the device's SysEx ring as the computer empties it (plat_sysex_room, plat_sysex_queued): HSX_RING packets of 3 bytes,
 * HSX_READ a millisecond read while the computer reads (a 64-byte USB-MIDI transfer a 1 ms frame); a send that finds
 * no room waits as the device's does, the computer reading meanwhile (or, stalled: -1 after its 200 ms). The script's
 * "usb unplug | plug | stall | read" (plat_usb_session: a new number at each plug) */
#define HSX_RING 128u                           /* (device/unity.c SXQ) */
#define HSX_READ 16u
static uint32_t hsx_q, hsx_session = 1, hsx_plugs = 1;
static uint8_t hsx_stalled;
static uint8_t hsx_streaming, hsx_scut;          /* a stream begun (plat_sysex_begin); cut short (a put gave up) */
static uint32_t hsx_sk;                          /* the stream's bytes not yet in a packet (0..3) */
uint32_t plat_sysex_room(void) { return hsx_session && !hsx_streaming ? (HSX_RING - hsx_q) * 3u : 0u; }
uint32_t plat_sysex_queued(void) { return hsx_q * 3u; }
uint32_t plat_usb_session(void) { return hsx_session; }
static void hsx_ms(void)                        /* one millisecond of USB: the computer reads */
{
    if (!hsx_stalled)
        hsx_q -= hsx_q < HSX_READ ? hsx_q : HSX_READ;
}
int plat_sysex_send(const uint8_t *frame, uint32_t n)
{
    uint32_t pk = (n + 2u) / 3u;
    if (!hsx_session || (hsx_stalled && pk > HSX_RING - hsx_q))
        return -1;                              /* (no computer; or the ring full and nobody reading) */
    if (hsx_streaming)
        return -1;                              /* (a stream under way holds the ring) */
    hsx_q = hsx_q + pk > HSX_RING ? HSX_RING : hsx_q + pk;   /* (a wait: the computer read the rest meanwhile) */
    hsx_keep(frame, n);
    return 0;
}
#if FM1B_RES
#include "fm1_res_data.h"                             /* fm1b_res_data: the build's resources (build.py, from the file) */
static int res_there = 1;                             /* (script "res 0": an FM-1 without them) */
const uint8_t *plat_res(uint32_t *len)
{
    if (len)
        *len = res_there ? (uint32_t)sizeof fm1b_res_data : 0u;
    return res_there ? fm1b_res_data : 0;
}
#endif
/* a stream (plat_sysex_begin .. end): its bytes kept as they are put, its packets into the ring as the device's */
int plat_sysex_begin(void)
{
    if (!hsx_session || hsx_streaming)
        return -1;
    hsx_streaming = 1, hsx_scut = 0, hsx_sk = 0;
    return 0;
}
int plat_sysex_put(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (!hsx_streaming || hsx_scut || !hsx_session)
        return -1;
    for (i = 0; i < n; i++) {
        if (hsx_sk == 3u) {                     /* (a full packet goes when the next byte comes) */
            if (hsx_stalled && hsx_q >= HSX_RING) {
                hsx_scut = 1;                   /* (nobody reading for the stall: the frame cut here) */
                hsx_keep(p, i);
                return -1;
            }
            hsx_q += hsx_q < HSX_RING;
            hsx_sk = 0;
        }
        hsx_sk++;
    }
    hsx_keep(p, n);
    return 0;
}
int plat_sysex_end(void)
{
    int rc = hsx_streaming && !hsx_scut && hsx_session && hsx_sk ? 0 : -1;
    if (!rc && hsx_stalled && hsx_q >= HSX_RING)
        rc = -1;                                /* (its last packet: no room) */
    else if (!rc)
        hsx_q++;
    hsx_streaming = 0, hsx_sk = 0;
    return rc;
}
/* no USB serial console on the host (plat.h: never open) */
int plat_console_open(void) { return 0; }
int plat_console_read(void) { return -1; }
uint32_t plat_console_room(void) { return 0; }
uint32_t plat_console_write(const void *p, uint32_t n) { (void)p, (void)n; return 0; }
void plat_console_present(int on) { (void)on; }
/* the crash history (device/crashlog.c) over the simulated flash's staging sectors (data_area holds the gap between
 * the windows: plat_data refuses it, as on the device). The device's .noinit crash record simulated: "crash" fills it
 * as felucca-bsp's fm1_fault_c does and restarts (.noinit kept), "power" a power-off and on (.noinit lost, the flash
 * kept); each boot hands it to cl_boot as main_fm1.c does. "sysex F0 7D 4C 48 F7" asks for the history (the platform's
 * frame, before the app's), expect crashes / crash_count / crash_boot_rc read it */
#ifndef FM1B_BUILD
#define FM1B_BUILD 0u                               /* (tools/build.py's build_id) */
#endif
#include "crashlog.c"
static int cl_in(uint32_t off, uint32_t n) { return off >= CL_LO && n <= CL_SECTORS * 0x1000u - (off - CL_LO); }
static int cl_read(uint32_t off, void *dst, uint32_t n)
{
    data_init();
    if (!cl_in(off, n))
        return -1;
    memcpy(dst, &data_area[off - DATA_BASE], n);
    return 0;
}
static int cl_erase(uint32_t off)
{
    data_init();
    if (!cl_in(off, 0x1000u) || (off & 0xFFFu))
        return -1;
    memset(&data_area[off - DATA_BASE], 0xFF, 0x1000u);
    return 0;
}
static int cl_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    data_init();
    if (!cl_in(off, n) || (off & 0xFFu) + n > 256u)
        return -1;                                  /* (as the device's driver: within a page) */
    for (i = 0; i < n; i++)
        data_area[off - DATA_BASE + i] &= ((const uint8_t *)src)[i];
    return 0;
}
static int cl_send(const uint8_t *f, uint32_t n) { plat_sysex_send(f, n); return 0; }   /* (its answer: the device ignores it too) */
#define HCR_MAGIC 0x43525348u                       /* felucca-bsp's FM1_CRASH_MAGIC, "CRSH" */
static struct {                                     /* the device's .noinit: fm1_crash, fm1_crash_ctx, fm1b_dbg's two */
    uint32_t magic, count, vec, pc, rets, sp, uptime_ms;
    uint32_t r[16], usp;
    uint32_t kept, busy;
    uint32_t emu;                                   /* (EMU_MSG: bit 3 with "stack_guard 1", a stack in its guard) */
    int32_t left[3];                                /* the stacks' room at the crash (stacks.c pst_crash) */
} hcr;
/* the stacks' room in this run (stacks.c plat_stack_left), as the script says the painted marks were used down to
 * ("stack MAIN IRQ C1", bytes left, -1 not measured; each boot starts from -1: a new paint) */
static int32_t hst_left[3] = {-1, -1, -1};
static uint32_t hst_guard;                          /* the next crash ran into a guard ("stack_guard 1") */
int32_t plat_stack_left(int k) { return k >= 0 && k < 3 ? hst_left[k] : -1; }
static uint32_t hcr_boot_ms;                        /* (now_ms at the last boot: the crash's uptime) */
static int hcr_rc;                                  /* the last boot's cl_boot: 1 kept, 0 nothing new, < 0 not kept */
static void hcr_crash(uint32_t pc, uint32_t vec, int in_save)   /* a fault, as fm1_fault_c records it */
{
    uint32_t i;
    hcr.count = hcr.magic == HCR_MAGIC ? hcr.count + 1u : 1u;
    hcr.magic = HCR_MAGIC;
    hcr.vec = vec, hcr.pc = pc, hcr.rets = pc - 0x40u, hcr.sp = 0x01C7A000u - 4u * hcr.count;
    hcr.uptime_ms = now_ms - hcr_boot_ms;
    for (i = 0; i < 16u; i++)
        hcr.r[i] = 0x11111111u * i ^ hcr.count;
    hcr.usp = hcr.sp + 0x100u;
    hcr.emu = hst_guard ? 1u << 3 : 0u;             /* (EMU_MSG bit 3: sp out of the stacks' window) */
    hst_guard = 0;
    for (i = 0; i < 3u; i++)
        hcr.left[i] = hst_left[i];                  /* (main_fm1.c fm1_fault: stacks.c pst_at_crash) */
    if (in_save)
        hcr.busy = 1;                               /* (crash_in_save: the boot's save under way when it struck) */
}
static void hcr_boot(void)                          /* main_fm1.c fm1_main's: a crash since the last boot, kept */
{
    cl_rec_t r;
    hcr_boot_ms = now_ms;
    hcr_rc = 0;
    if (hcr.magic != HCR_MAGIC)
        return;
    memset(&r, 0, sizeof r);
    r.count = hcr.count, r.vec = hcr.vec, r.pc = hcr.pc, r.rets = hcr.rets, r.sp = hcr.sp, r.uptime_ms = hcr.uptime_ms;
    r.build = FM1B_BUILD, r.flags = CL_CTX, r.usp = hcr.usp, r.ssp = hcr.usp - 0x800u;
    if (hcr.left[0] >= 0 || hcr.left[1] >= 0 || hcr.left[2] >= 0)
        r.flags |= cl_stk_pack(hcr.left);           /* (the stacks' room, when the script said: "stack") */
    r.emu = hcr.emu;
    memcpy(r.r, hcr.r, sizeof r.r);
    hcr_rc = cl_boot(&r, &hcr.kept, &hcr.busy);
}
/* the debug stats (device/dbgstats.c) over the simulated flash's last staging sector, as main_fm1.c keeps them: its
 * queue (.noinit on the device) kept by "crash" and "reboot", lost by "power"; a snapshot every 5 minutes of the
 * simulated uptime while the app has them on; the crash's at the boot. "ota_stage" / "ota_unstage" stage an update as
 * ota.c leaves the area (a loader's first word at 0xE0000) and erase it whole; "sysex F0 7D 4C 53 F7" asks for them,
 * expect stats / stats_pending / stats_on / stats_rc read them */
#define DS_NOINIT
#ifdef APP_STATS_LAYOUT
#define DS_LAYOUT APP_STATS_LAYOUT
#else
#define DS_LAYOUT 0
#endif
#include "dbgstats.c"
#define HDS_OTA_AREA 0xE0000u                       /* ota.c's OTA_AREA and OTA_RES (0xE4F00) */
#define HDS_OTA_RES 0xE4F00u
static int ds_in(uint32_t off, uint32_t n) { return off >= DS_LO && n <= DS_SLOTS * DS_REC - (off - DS_LO); }
static int ds_read(uint32_t off, void *dst, uint32_t n)
{
    data_init();
    if (!ds_in(off, n))
        return -1;
    memcpy(dst, &data_area[off - DATA_BASE], n);
    return 0;
}
static int ds_erase(uint32_t off)
{
    data_init();
    if (off != DS_LO)
        return -1;
    memset(&data_area[off - DATA_BASE], 0xFF, 0x1000u);
    return 0;
}
static int ds_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    data_init();
    if (!ds_in(off, n) || (off & 0xFFu) + n > 256u)
        return -1;                                  /* (as the device's driver: within a page) */
    for (i = 0; i < n; i++)
        data_area[off - DATA_BASE + i] &= ((const uint8_t *)src)[i];
    return 0;
}
static int ds_send(const uint8_t *f, uint32_t n) { plat_sysex_send(f, n); return 0; }   /* (sent; as cl_send) */
static int ds_staged(void)
{
    static const uint8_t E[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    data_init();
    return memcmp(&data_area[HDS_OTA_AREA - DATA_BASE], E, 4) || memcmp(&data_area[HDS_OTA_RES - DATA_BASE], E, 4);
}
static int ds_quiet(void)
{
#ifdef APP_STATS_QUIET
    return app_stats_quiet();
#else
    return 1;
#endif
}
static int32_t ds_stack(int k) { return plat_stack_left(k); }
void plat_stats_enable(int on) { ds_enable(on); }
int plat_stats_on(void) { return ds_on; }
int32_t plat_stack_least_ever(int k) { return ds_least_ever(k); }
static uint32_t hds_boots, hds_halves;              /* starts since the power-on (fm1b_dbg.boots), halves since the boot */
static void hds_boot(void)                          /* main_fm1.c stats_keep: the queue, a crash's snapshot, the least
                                                     * ever; then a new paint (stacks.c pst_boot) */
{
    int on = hcr.magic == HCR_MAGIC;
    hds_boots++;
    hds_halves = 0;
    ds_init();
    (void)ds_boot(on ? hcr.count : 0u, on ? hcr.left : 0);
    hst_left[0] = hst_left[1] = hst_left[2] = -1;
}
/* no host to pull from (an app's transfer is tested on its own, or by writing the flash: "data") */
int plat_host_read(uint32_t addr, uint8_t *dst, uint32_t n) { (void)addr, (void)dst, (void)n; return -1; }
void plat_host_patience(uint32_t try_ms, uint32_t total_ms) { (void)try_ms, (void)total_ms; }
static int uac_full;                            /* plat_usb_audio_full: the render at full level, MASTER after it */
void plat_usb_audio_full(int on) { uac_full = on != 0; }
static uint32_t usb_play_q12 = 4096u;            /* plat_usb_play_gain: no USB audio on the host; kept for a test */
void plat_usb_play_gain(uint32_t q12) { usb_play_q12 = q12 > 4096u ? 4096u : q12; }
#if FM1B_UAC_CH > 2
static int16_t usb_ch[APP_BLOCK][FM1B_UAC_CH];      /* plat_usb_ch: the block's frames (1 and 2 the master, after it) */
int16_t *plat_usb_ch(uint32_t off, uint32_t *run)
{
    if (off >= APP_BLOCK) {
        *run = 0;
        return 0;
    }
    *run = APP_BLOCK - off;
    return &usb_ch[off][2];
}
static void usb_ch_master(const int32_t *blk)       /* the master into 1 and 2, as the device's tap takes it */
{
    for (uint32_t i = 0; i < APP_BLOCK; i++)
        for (uint32_t c = 0; c < 2u; c++) {
            int32_t v = (blk[2u * i + c] + 128) >> 8;
            usb_ch[i][c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
}
#else
int16_t *plat_usb_ch(uint32_t off, uint32_t *run) { (void)off; *run = 0; return 0; }
#endif

/* flash: one buffer per object, kept across a simulated reboot */
static uint8_t store[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_len[OBJ_NOBJ];           /* -1 = nothing saved (store_ready) */
static uint32_t store_writes, store_tries;
static int store_fail;                    /* flash_fail 1: every save fails (a flash error) */
static void store_ready(void)             /* the flash starts empty: once, on first use (a "reboot" keeps it) */
{
    static int done;
    uint32_t i;
    if (done)
        return;
    done = 1;
    for (i = 0; i < OBJ_NOBJ; i++)
        store_len[i] = -1;
}
const uint8_t *plat_chip_uid(void)
{
    static const uint8_t U[16] = {0x46, 0x4D, 0x31, 0x2D, 0x68, 0x6F, 0x73, 0x74};   /* "FM1-host" */
    return U;
}
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    int n;
    store_ready();
    if (obj >= OBJ_NOBJ || (n = store_len[obj]) <= 0)    /* (empty: none, as storage.c) */
        return -1;
    if ((uint32_t)n > max)
        n = (int)max;
    memcpy(dst, store[obj], (size_t)n);
    return n;
}
int plat_store_head(uint32_t obj, void *dst, uint32_t n)
{
    store_ready();
    if (obj >= OBJ_NOBJ || store_len[obj] <= 0 || (uint32_t)store_len[obj] < n)
        return -1;
    memcpy(dst, store[obj], (size_t)n);
    return (int)n;
}
int plat_store_size(uint32_t obj)
{
    store_ready();
    return obj >= OBJ_NOBJ || store_len[obj] <= 0 ? -1 : store_len[obj];
}
int plat_store_room(void) { return -1; }   /* (the simulator's store has no pool) */
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    store_ready();
    store_tries++;
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX || store_fail)
        return -1;
    memcpy(store[obj], src, len);
    store_len[obj] = (int)len;
    store_writes++;
    return 0;
}

#ifndef PLAT_WEB
/* the whole flash as a file (flash_dump / flash_load, FM1B_FLASH): a 1 MiB image at the FM-1's offsets, a reference
 * FM-1's flash read whole, say. What the simulator has of it: plat.h's data windows, byte for byte, and the store's
 * objects as device/storage.c keeps them (each at its APP_STORE_MAP offset, A then B: a 32-byte header at 0, the
 * payload at 256; the current copy is the valid one with the highest seq), the crash history's two sectors in the
 * update's staging (device/crashlog.c CL_LO) and the debug stats' (device/dbgstats.c DS_LO). The rest (the head, the
 * app area, the rest of the staging, the SDK's
 * sectors) is not the simulator's: erased in a dump, left out of a load */
#define HIMG_SIZE 0x100000u
#define HIMG_MAGIC 0x30583058u                  /* storage.c's ST_MAGIC, "X0X0" */
#define HIMG_PAYLOAD 256u                       /* storage.c's ST_PAYLOAD_OFF */
static uint8_t himg[HIMG_SIZE];
static uint32_t himg_crc32(const uint8_t *p, uint32_t n)   /* zlib's CRC-32, as storage.c's st_crc32 */
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--)
        for (c ^= *p++, k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    return ~c;
}
static uint32_t himg_rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void himg_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}
static uint32_t himg_sector(uint32_t obj, uint32_t copy)   /* an object's copy in the image, 0 = outside it */
{
    static const uint32_t map[OBJ_NOBJ] = APP_STORE_MAP;
    return map[obj] + 0x2000u <= HIMG_SIZE && !(map[obj] & 0xFFFu) ? map[obj] + copy * 0x1000u : 0u;
}
static int himg_copy(uint32_t obj, uint32_t copy, uint32_t *seq)   /* a whole valid copy: its length, else -1 */
{
    uint32_t s = himg_sector(obj, copy), len;
    const uint8_t *h = &himg[s];
    if (!s || himg_rd32(h) != HIMG_MAGIC || (uint32_t)(h[4] | h[5] << 8) != obj ||
        himg_rd32(h + 28) != himg_crc32(h, 28) || (len = himg_rd32(h + 12)) > PLAT_STORE_MAX ||
        himg_rd32(h + 16) != himg_crc32(h + HIMG_PAYLOAD, len))
        return -1;
    *seq = himg_rd32(h + 8);
    return (int)len;
}
static int himg_dump(const char *path)
{
    uint32_t off, obj;
    FILE *f;
    size_t n = 0;
    data_init();
    store_ready();
    memset(himg, 0xFF, sizeof himg);
    for (off = 0; off < HIMG_SIZE; off += 0x1000u)
        if (PLAT_DATA_OK(off, 0x1000u))
            memcpy(&himg[off], plat_data(off), 0x1000u);
    memcpy(&himg[CL_LO], &data_area[CL_LO - DATA_BASE], CL_SECTORS * 0x1000u);   /* (the crash history) */
    memcpy(&himg[DS_LO], &data_area[DS_LO - DATA_BASE], 0x1000u);   /* (the debug stats) */
    for (obj = 0; obj < OBJ_NOBJ; obj++) {      /* copy A, seq 1 (B erased): what a first save leaves */
        uint32_t s = himg_sector(obj, 0);
        uint8_t *h = &himg[s];
        if (!s || store_len[obj] <= 0)
            continue;
        memset(h, 0xFF, 0x2000u);
        himg_wr32(h, HIMG_MAGIC);
        h[4] = (uint8_t)obj, h[5] = (uint8_t)(obj >> 8), h[6] = h[7] = 0;   /* type, slot 0 */
        himg_wr32(h + 8, 1u);
        himg_wr32(h + 12, (uint32_t)store_len[obj]);
        memcpy(h + HIMG_PAYLOAD, store[obj], (size_t)store_len[obj]);
        himg_wr32(h + 16, himg_crc32(h + HIMG_PAYLOAD, (uint32_t)store_len[obj]));
        himg_wr32(h + 28, himg_crc32(h, 28));    /* (rsv[2] stays 0xFF: storage.c writes 0xFFFFFFFF there) */
    }
    if ((f = fopen(path, "wb")) != 0) {
        n = fwrite(himg, 1, sizeof himg, f);
        fclose(f);
    }
    return n == sizeof himg ? 0 : -1;
}
static int himg_load(const char *path, uint32_t *objs)   /* 0 = loaded (*objs: the store's objects found in it) */
{
    uint32_t off, obj;
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(himg, 1, sizeof himg, f) : 0;
    if (f && n == sizeof himg && fgetc(f) != EOF)
        n = 0;                                    /* (bigger than the 1 MiB part: not an FM-1's) */
    if (f)
        fclose(f);
    if (n != sizeof himg)
        return -1;
    data_init();
    store_ready();
    for (off = 0; off < HIMG_SIZE; off += 0x1000u)
        if (PLAT_DATA_OK(off, 0x1000u))
            memcpy(&data_area[off - DATA_BASE], &himg[off], 0x1000u);
    memcpy(&data_area[CL_LO - DATA_BASE], &himg[CL_LO], CL_SECTORS * 0x1000u);   /* (the crash history) */
    memcpy(&data_area[DS_LO - DATA_BASE], &himg[DS_LO], 0x1000u);   /* (the debug stats) */
    *objs = 0;
    for (obj = 0; obj < OBJ_NOBJ; obj++) {
        uint32_t sa = 0, sb = 0;
        int a = himg_copy(obj, 0, &sa), b = himg_copy(obj, 1, &sb);
        uint32_t use_b = b >= 0 && (a < 0 || sb > sa);   /* (storage.c st_current: A on a tie) */
        int len = use_b ? b : a;
        uint32_t s = himg_sector(obj, use_b);
        store_len[obj] = len > 0 ? len : -1;      /* (an empty object: none, as storage.c's st_load) */
        if (len > 0) {
            memcpy(store[obj], &himg[s + HIMG_PAYLOAD], (size_t)len);
            ++*objs;
        }
    }
    return 0;
}
#endif

static uint32_t cpu_pct;
uint32_t plat_cpu_pct(void) { return cpu_pct; }
#ifdef APP_CPU_KHZ
uint32_t plat_cpu_khz(void) { return 0; }        /* (no clock to speak of: the host's) */
#endif
static void ds_fill(ds_rec_t *r)                    /* what the simulator has of main_fm1.c's counters (the rest 0) */
{
    r->up_s = (now_ms - hcr_boot_ms) / 1000u, r->halves = hds_halves;
    r->boots = (uint8_t)(hds_boots < 255u ? hds_boots : 255u);
    r->crashes = hcr.magic == HCR_MAGIC ? (uint8_t)(hcr.count < 255u ? hcr.count : 255u) : 0u;
#ifdef APP_STATS
    app_stats(r->app);
#endif
}
uint32_t plat_xruns(void) { return 0; }
void plat_code_window(const void *end) { (void)end; }
/* disk mode: no computer in the simulator; the app's screen and flush still run */
static const plat_disk_t *host_disk;
void plat_disk_mode(const plat_disk_t *d)
{
    if (!d && host_disk && host_disk->flush)
        host_disk->flush();
    host_disk = d;
}
int plat_disk_poll(void) { return host_disk ? PLAT_DISK_WAITING : PLAT_DISK_OFF; }
uint32_t plat_disk_idle_ms(void) { return 0; }
uint32_t plat_ticks24(void)                     /* the host's monotonic clock, at the device's rate */
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)t.tv_sec * (PLAT_TICKS_PER_US * 1000000u) + (uint32_t)t.tv_nsec / 1000u * PLAT_TICKS_PER_US +
           (uint32_t)t.tv_nsec % 1000u * PLAT_TICKS_PER_US / 1000u;
}

#if FM1B_CPU2 && !defined(APP_HOST_CPU2)
/* the second core, simulated: a job runs at once, in order (the same sums as the device's split); plat_core is 1 in it */
static int cpu2_on, cpu2_in_job;
int plat_cpu2_set(int on) { cpu2_on = on; return 0; }
int plat_cpu2_on(void) { return cpu2_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!cpu2_on)
        return 0;
    cpu2_in_job = 1;
    fn(arg);
    cpu2_in_job = 0;
    return 1;
}
uint32_t plat_core(void) { return (uint32_t)cpu2_in_job; }
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
uint32_t plat_cpu2_stack(void) { return 0; }
#elif !defined(APP_HOST_CPU2)
uint32_t plat_core(void) { return 0; }   /* (one core) */
#endif

/* the cache RAM (plat.h, fm1.toml cache_ram_ways): a buffer of the ways' size stands in, ready from the start, its masks
 * as the device's would read; FM1B_CACHE_RAM_FAIL=1 in the environment: its test failed (0 bytes, the masks put back) */
#ifndef FM1B_CACHE_RAM
#define FM1B_CACHE_RAM 0
#endif
const struct plat_cache_ram *plat_cache_ram(void)
{
    static struct plat_cache_ram r;
#if FM1B_CACHE_RAM
    static uint8_t ways[FM1B_CACHE_RAM * 4096u] __attribute__((aligned(8)));
    static int set;
    if (!set) {
        const char *f = getenv("FM1B_CACHE_RAM_FAIL");
        uint32_t keep = (0xFFu << FM1B_CACHE_RAM) & 0xFFu;
        int ok = !(f && f[0] == '1');
        set = 1;
        r.ways = FM1B_CACHE_RAM;
        r.status = ok ? PLAT_CR_OK : PLAT_CR_BAD;
        r.dway = ok ? keep | keep << 8 | keep << 16 : 0x00FFFFFFu;
        r.iway = 0xFFFFu;
        r.bad_at = ok ? 0u : 0x01F28000u;
        r.bytes = ok ? (uint32_t)sizeof ways : 0u;
        r.base = ok ? ways : 0;
    }
#endif
    return &r;
}

/* -------------------------------------------------------------- app --- */
#define ANN_NAME APP_NAME " " APP_VERSION
#include "ann_words.h"                                  /* the words file's link (tools/gen_words.py) */
#include "announce.c"
/* what the reader is told: every SAY to stdout with "said:" (listen), and kept for the "said" check */
static char said_last[ANN_MAX + 1], said_log[16384];   /* said_log: every SAY since the last "said" check */
static uint32_t said_len;
static int said_print = 1, sim_listen;
static FILE *said_log_f;                                /* FM1B_SAID_LOG=FILE: every SAY, for fm1-reader's audit */
#ifdef PLAT_WEB
static void web_said(const uint8_t *p, uint32_t n);
#endif
static int ann_wire_send(const uint8_t *p, uint32_t n)
{
#ifdef PLAT_WEB
    web_said(p, n);
#endif
    if (n >= 7u && p[4] == ANN_SAY) {
        uint32_t i, k = 0;
        for (i = 6; i + 1u < n && k < ANN_MAX; i++)
            said_last[k++] = (char)p[i];
        said_last[k] = 0;
        if (said_len + k + 2u < sizeof said_log) {
            memcpy(said_log + said_len, said_last, k);
            said_len += k;
            said_log[said_len++] = '\n';
            said_log[said_len] = 0;
        }
#ifndef PLAT_WEB
        if (said_print)
            printf("said%s: %s\n", (p[5] & ANN_NOW) ? "!" : "", said_last);
        if (said_log_f)
            fprintf(said_log_f, "%s\t%s\n", (p[5] & ANN_KEEP) ? "WARN" : "MSG", said_last);
#endif
    }
    return 0;
}
#ifdef APP_TOUCH
#include "touch.c"
#endif
#ifdef APP_BTDEV
#include "btdev.c"
#endif
#include "app.c"

#if FM1B_MIRROR
/* ------------------------------------------------- mirroring over USB --- */
/* the device's mirror ring, drained at USB's pace (mr_rate bytes a millisecond; 48: one 64-byte packet of USB-MIDI
 * a 1 ms frame) into a file of SysEx (`mirror FILE`); the SysEx ring's frames (the input reports, the hellos) go
 * first, between frames, as usb.c sends them. The viewer's keep-alive every 2 s. */
static uint8_t hmr_q[MRQ], hctl[1024];
static uint32_t hmr_w, hmr_r, hctl_n, mr_rate = 48, mr_mid, mr_keep = 1, mr_keep_t, mr_link_n = 1;
static FILE *mr_out;
static uint32_t mr_free(void) { return MRQ - (hmr_w - hmr_r); }
static void mr_put(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        hmr_q[(hmr_w + i) % MRQ] = p[i];
    hmr_w += n;
}
static int mr_link(void) { return mr_out ? (int)mr_link_n : -1; }
static int mr_busy(void) { return !mr_out; }
static int mr_ctl_send(const uint8_t *p, uint32_t n)
{
    if (!mr_out || hctl_n + n > sizeof hctl)
        return -1;
    memcpy(hctl + hctl_n, p, n);
    hctl_n += n;
    return 0;
}
static void mr_redraw(void)
{
#ifdef APP_REDRAW
    app_redraw();
#endif
}
static int mr_test(int on)
{
#ifdef APP_MIRROR_TEST
    return app_mirror_test(on);
#else
    return !on;
#endif
}
static void host_mirror_ms(void)                 /* one millisecond of USB */
{
    uint32_t budget = mr_rate;
    if (!mr_out)
        return;
    if (mirror_on && mr_keep && now_ms - mr_keep_t >= 2000u) {
        mr_keep_t = now_ms;
        mirror_cmd(1);
    }
    for (;;) {
        if (!mr_mid && hctl_n) {
            fwrite(hctl, 1, hctl_n, mr_out);
            budget = budget > hctl_n ? budget - hctl_n : 0;
            hctl_n = 0;
        }
        if (hmr_r == hmr_w || !budget)
            break;
        {
            uint8_t b = hmr_q[hmr_r++ % MRQ];
            fputc(b, mr_out);
            mr_mid = b != 0xF7u;
            budget--;
        }
    }
}
#endif

#if FM1B_BACKUP
/* ------------------------------------------- backup / restore over USB --- */
/* device/backup.c's image, made and taken from the simulated store (the device's side and its safety are
 * device/backup.c's, on storage.c) */
static const uint16_t sim_objs[] = APP_BACKUP_OBJS;
#define SIM_NOBJ ((int)(sizeof sim_objs / sizeof sim_objs[0]))
static int restore_rc = -1;
static uint32_t sim_crc32(uint32_t c, const uint8_t *p, size_t n)
{
    c = ~c;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}
static void sim_wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t sim_rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint8_t sim_img[16 + SIM_NOBJ * (8 + PLAT_STORE_MAX) + 4];
static size_t sim_backup_image(void)              /* the image into sim_img: its length */
{
    size_t n = 16;
    int playing = app_backup_playing() != 0;
    if (!playing)
        app_backup_flush();
    sim_wr32(sim_img, APP_BACKUP_MAGIC);
    sim_img[4] = (uint8_t)APP_BACKUP_VERSION, sim_img[5] = (uint8_t)(APP_BACKUP_VERSION >> 8);
    sim_img[6] = (uint8_t)SIM_NOBJ, sim_img[7] = 0;
    sim_wr32(sim_img + 8, (uint32_t)playing);
    sim_wr32(sim_img + 12, sim_crc32(0, sim_img, 12));
    for (int i = 0; i < SIM_NOBJ; i++) {
        uint32_t o = sim_objs[i], len = store_len[o] > 0 ? (uint32_t)store_len[o] : 0;
        uint8_t *r = sim_img + n;
        r[0] = (uint8_t)o, r[1] = (uint8_t)(o >> 8), r[2] = (uint8_t)len, r[3] = (uint8_t)(len >> 8);
        memcpy(r + 8, store[o], len);
        sim_wr32(r + 4, sim_crc32(0, r + 8, len));
        n += 8 + len;
    }
    sim_wr32(sim_img + n, sim_crc32(0, sim_img, n));
    return n + 4;
}
static int sim_backup(const char *path)
{
    size_t n = sim_backup_image();
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite(sim_img, 1, n, f);
    fclose(f);
    return 0;
}
static int sim_restore_image(size_t n)            /* sim_img's n bytes: 0, or backup.c's RS_E_*: 10 playing, 3 the head, 4..6 damaged */
{
    size_t pos = 16;
    if (app_backup_playing())
        return 10;                                /* STOP FIRST */
    if (n < 16u + (size_t)SIM_NOBJ * 8u + 4u || sim_rd32(sim_img) != APP_BACKUP_MAGIC ||
        (uint32_t)(sim_img[4] | sim_img[5] << 8) != APP_BACKUP_VERSION || sim_img[6] != SIM_NOBJ ||
        sim_rd32(sim_img + 12) != sim_crc32(0, sim_img, 12))
        return 3;
    for (int i = 0; i < SIM_NOBJ; i++) {          /* all checked before anything changes */
        const uint8_t *r = sim_img + pos;
        uint32_t len = r[2] | r[3] << 8;
        if (pos + 8 + len + 4 > n || (uint32_t)(r[0] | r[1] << 8) != sim_objs[i] || len > PLAT_STORE_MAX)
            return 4;
        if (sim_rd32(r + 4) != sim_crc32(0, r + 8, len))
            return 5;
        pos += 8 + len;
    }
    if (pos + 4 != n || sim_rd32(sim_img + pos) != sim_crc32(0, sim_img, pos))
        return 6;
    pos = 16;
    for (int i = 0; i < SIM_NOBJ; i++) {
        const uint8_t *r = sim_img + pos;
        uint32_t o = sim_objs[i], len = r[2] | r[3] << 8;
        memcpy(store[o], r + 8, len);
        store_len[o] = len ? (int)len : -1;       /* (an empty object: none, as storage.c's st_load) */
        pos += 8 + len;
    }
    app_panic();
    app_backup_restored();
    return 0;
}
static int sim_restore(const char *path)          /* 0, or backup.c's RS_E_*; 1: no such file */
{
    size_t n;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 1;
    n = fread(sim_img, 1, sizeof sim_img, f);
    fclose(f);
    return sim_restore_image(n);
}
#endif

/* ---------------------------------------------------------- outputs --- */
static void png_write(const char *path)
{
    /* RGB PNG with stored (uncompressed) deflate blocks: no zlib needed */
    static uint8_t raw[240 * (1 + 240 * 3)];
    FILE *f = fopen(path, "wb");
    uint32_t crc_t[256], i, j, k, n = 0, a = 1, b = 0, pos, len = sizeof raw;
    uint8_t hdr[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_t[i] = c;
    }
    for (j = 0; j < 240; j++) {
        raw[n++] = 0;
        for (i = 0; i < 240; i++) {
            uint16_t p = fb[j * 240 + i];
            p = (uint16_t)((p >> 8) | (p << 8));
            raw[n++] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            raw[n++] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            raw[n++] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    for (i = 0; i < len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
#define PUT32(v) do { uint8_t q_[4] = {(uint8_t)((v) >> 24), (uint8_t)((v) >> 16), (uint8_t)((v) >> 8), (uint8_t)(v)}; fwrite(q_, 1, 4, f); } while (0)
    fwrite(hdr, 1, 8, f);
    {
        uint8_t ih[17] = {'I', 'H', 'D', 'R', 0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(13u);
        fwrite(ih, 1, 17, f);
        for (i = 0; i < 17; i++)
            c = crc_t[(c ^ ih[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    {
        uint32_t nblk = (len + 65534) / 65535, zlen = 2 + len + nblk * 5 + 4, c = 0xFFFFFFFFu;
        uint8_t *z = malloc(zlen + 4), *q = z;
        memcpy(q, "IDAT", 4);
        q += 4;
        *q++ = 0x78;
        *q++ = 0x01;
        for (pos = 0; pos < len;) {
            uint32_t m = len - pos > 65535 ? 65535 : len - pos;
            *q++ = pos + m >= len ? 1 : 0;
            *q++ = (uint8_t)m;
            *q++ = (uint8_t)(m >> 8);
            *q++ = (uint8_t)~m;
            *q++ = (uint8_t)(~m >> 8);
            memcpy(q, raw + pos, m);
            q += m;
            pos += m;
        }
        *q++ = (uint8_t)(b >> 8);
        *q++ = (uint8_t)b;
        *q++ = (uint8_t)(a >> 8);
        *q++ = (uint8_t)a;
        PUT32(zlen);
        fwrite(z, 1, zlen + 4, f);
        for (i = 0; i < zlen + 4; i++)
            c = crc_t[(c ^ z[i]) & 255] ^ (c >> 8);
        PUT32(~c);
        free(z);
    }
    {
        uint8_t ie[4] = {'I', 'E', 'N', 'D'};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(0u);
        fwrite(ie, 1, 4, f);
        for (i = 0; i < 4; i++)
            c = crc_t[(c ^ ie[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    fclose(f);
}

static FILE *wav;
static uint32_t wav_frames;
static float peak_out;
static FILE *uwav;                                  /* usbwav: the USB audio input's channels */
static uint32_t uwav_frames;
static FILE *wav_start(const char *path)
{
    static const uint8_t h[44] = {0};
    FILE *f = fopen(path, "wb");
    if (f)
        fwrite(h, 1, 44, f);
    return f;
}
static void wav_open(const char *path)
{
    wav = wav_start(path);
    wav_frames = 0;
}
static void wav_end(FILE *f, uint32_t frames, uint16_t ch)   /* the header, now the length is known */
{
    uint32_t bytes = frames * 2u * ch, v;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    v = 36 + bytes;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    {
        uint16_t fmt[2] = {1, ch};
        uint32_t r[2] = {44100, 44100u * 2u * ch};
        uint16_t al[2] = {(uint16_t)(2u * ch), 16};
        fwrite(fmt, 2, 2, f);
        fwrite(r, 4, 2, f);
        fwrite(al, 2, 2, f);
    }
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
    fclose(f);
}
static void wav_close(void)
{
    uint32_t bytes = wav_frames * 4, v;
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    v = 36 + bytes;
    fwrite(&v, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    v = 16;
    fwrite(&v, 4, 1, wav);
    {
        uint16_t fmt[2] = {1, 2};
        uint32_t r[2] = {44100, 44100 * 4};
        uint16_t al[2] = {4, 16};
        fwrite(fmt, 2, 2, wav);
        fwrite(r, 4, 2, wav);
        fwrite(al, 2, 2, wav);
    }
    fwrite("data", 1, 4, wav);
    fwrite(&bytes, 4, 1, wav);
    fclose(wav);
    wav = 0;
}

/* --------------------------------------------------------- the device --- */
static double audio_due_ms, render_ns_total, render_budget_ns_total;
static uint32_t ui_due;
static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static void boot(void)
{
    hcr_boot();                                     /* a crash before this boot into the flash history (main_fm1.c) */
    hds_boot();                                     /* the debug stats' queue, a crash's snapshot (main_fm1.c) */
    palette_set(APP_PALETTE);                       /* as the device does at boot */
    app_init();
    lcd_fill(0, 0, 240, 240, 0);
    ble_state = BLE_OFF;                            /* Bluetooth starts at every boot, as on the device */
    ble_conn = ble_link = 0;
    bdev_state = BLE_DEV_OFF;
#if FM1B_BT_AUDIO
    bta_boot();
#endif
    if (APP_BLE_AT_BOOT)
        plat_ble_start();
#ifdef APP_BOOT
    app_boot();
#endif
    app_ui_init();
}

static uint8_t restart_req;                         /* plat_restart: at the end of this millisecond */
static uint32_t restarts;
void plat_restart(void) { restart_req = 1; }
#if FM1B_EMU_REBOOT
static uint8_t web_down;                            /* the browser emulator's restart or update mode: the app is not run,
                                                     * the sound is silence (the page's part "reboot", web/emu/parts) */
#endif

/* one millisecond of the device, in three steps (run_ms; host/rfm1.c calls them on its host's clock): the clock and
 * what runs before the render (sim_ms_begin), the APP_BLOCK-frame renders that have fallen due (sim_render, each),
 * then the UI (sim_ms_end: a frame every 16 ms, else the input only) */
static void sim_ms_begin(uint32_t ms)
{
    now_ms = ms;
    ble_tick();
#if FM1B_BT_AUDIO
    bta_tick();
#endif
#if FM1B_WIFI
    wifi_tick();
#endif
#if defined APP_KEY_SENSE
    if (sense_on)
        app_host_tick();                        /* the keys' readings, as the scan hands them over */
#endif
#ifdef APP_TICK
    app_tick();                                 /* 1 kHz, as the device's timer runs it */
#endif
#ifdef APP_LOOP
    app_loop();
#endif
}

static void sim_render(int32_t *blk)            /* one APP_BLOCK-frame block, as the device renders it */
{
    double t0 = now_ns(), dt;
    uint32_t k;
#if FM1B_UAC_CH > 2
    memset(usb_ch, 0, sizeof usb_ch);
#endif
    app_render(blk, APP_BLOCK, uac_full ? 4096u : APP_GAIN(master));
#if FM1B_UAC_CH > 2
    usb_ch_master(blk);
    if (uwav) {
        fwrite(usb_ch, sizeof usb_ch, 1, uwav);
        uwav_frames += APP_BLOCK;
    }
#else
    if (uwav) {                                 /* (two channels: the master, as the tap takes it) */
        for (k = 0; k < APP_BLOCK; k++)
            for (uint32_t c = 0; c < 2u; c++) {
                int32_t v = (blk[2u * k + c] + 128) >> 8;
                int16_t x = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                fwrite(&x, 2, 1, uwav);
            }
        uwav_frames += APP_BLOCK;
    }
#endif
    if (uac_full) {                             /* (the USB audio input took it at full level) the speaker: MASTER */
        const float g = (float)APP_GAIN(master) * (1.0f / 4096.0f);
        for (k = 0; k < 2u * APP_BLOCK; k++)
            blk[k] = (int32_t)((float)blk[k] * g);
    }
#ifdef APP_LOCAL_MONITOR
    static unsigned monitor_level;
    local_monitor(blk, APP_BLOCK, &monitor_level, app_local_monitor_muted());
#endif
    dt = now_ns() - t0;
    render_ns_total += dt;
    render_budget_ns_total += APP_BLOCK * 1e9 / 44100.0;
    cpu_pct = (uint32_t)(100.0 * dt / (APP_BLOCK * 1e9 / 44100.0));
}

static void sim_ms_end(int frame)
{
    ds_poll(now_ms - hcr_boot_ms, 0u, 0u);       /* (the debug stats: main_fm1.c's pass, once a millisecond) */
    if (sim_listen && now_ms % 2000u == 0) {        /* the reader's HELLO, as it sends it */
        static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
        ann_frame(HELLO, sizeof HELLO, now_ms);
    }
    if (frame)
        app_frame();
    else
        app_input_only();
#if FM1B_MIRROR
    mirror_step();
    host_mirror_ms();
#endif
    hsx_ms();
#ifdef APP_HOST_MS
    app_host_ms();
#endif
}

static void run_ms(uint32_t ms)
{
    static int32_t blk[2 * APP_BLOCK];
    uint32_t i, k;
    for (i = 0; i < ms; i++) {
#if FM1B_EMU_REBOOT
        if (web_down) {                             /* (the clock runs and the blocks are silence, as a stopped FM-1) */
            now_ms++;
            while (audio_due_ms <= (double)now_ms) {
                memset(blk, 0, sizeof blk);
                web_audio(blk, APP_BLOCK);
                audio_due_ms += APP_BLOCK * 1000.0 / 44100.0;
            }
            continue;
        }
#endif
        sim_ms_begin(now_ms + 1u);
        while (audio_due_ms <= (double)now_ms) {      /* APP_BLOCK-frame blocks, as the device renders them */
            sim_render(blk);
#ifdef PLAT_WEB
            web_audio(blk, APP_BLOCK);
#endif
            for (k = 0; k < APP_BLOCK; k++) {
                float l = (float)blk[2 * k] / 8388608.0f, r = (float)blk[2 * k + 1] / 8388608.0f;
                int16_t s[2];
                if (fabsf(l) > peak_out)
                    peak_out = fabsf(l);
                if (fabsf(r) > peak_out)
                    peak_out = fabsf(r);
                s[0] = (int16_t)(l * 32767.0f);
                s[1] = (int16_t)(r * 32767.0f);
                if (wav) {
                    fwrite(s, 2, 2, wav);
                    wav_frames++;
                }
            }
            audio_due_ms += APP_BLOCK * 1000.0 / 44100.0;
            hds_halves++;
        }
        if (now_ms >= ui_due) {
            ui_due = now_ms + 16;
            sim_ms_end(1);
        } else {
            sim_ms_end(0);
        }
        if (restart_req) {                          /* (as "reboot": the flash kept, the app's RAM forgotten) */
            restart_req = 0;
            restarts++;
#if FM1B_EMU_REBOOT                                 /* (no printf: the browser's stdout takes nothing, it never returns) */
            web_down = 1;                           /* (the page starts the app again from a fresh wasm: the app's RAM
                                                     * forgotten as the FM-1's, the saved data kept) */
#else
            printf("  restart (plat_restart) at %u ms\n", now_ms);
#ifdef APP_HOST_REBOOT
            app_host_reboot();
#endif
            boot();
#endif
        }
    }
}

/* ---------------------------------------------------------- script --- */
static const char *const BTN_N[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                      "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_N[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};

static int find_name(const char *const *t, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i], s))
            return i;
    return -1;
}

static int key_of(const char *s)
{
    if (s[0] == 'w')
        return HOST_WHITE_K[atoi(s + 1) & 15];
    if (s[0] == 'b')
        return HOST_BLACK_K[atoi(s + 1) % 11];
    return atoi(s);
}

static uint32_t mk_pkt(const char *a, const char *b, const char *c)
{
    uint32_t st = (uint32_t)strtoul(a, 0, 16), d1 = b[0] ? (uint32_t)strtoul(b, 0, 16) : 0,
             d2 = c[0] ? (uint32_t)strtoul(c, 0, 16) : 0;
    uint32_t cin = st >= 0xF0 ? 0x0F : st >> 4;
    return cin | st << 8 | d1 << 16 | d2 << 24;
}
static const char *const PORT_N[NPORTS] = {"USB", "BLE", "TRS"};
/* this message went out to PORT since the last "out" (sent 1), or did not (sent 0); PORT BLE is the computer's link
 * (TO_BLE_HOST), BLE-DEV the device's */
static int expect_out(const char *port, uint32_t pkt, int sent)
{
    int p = find_name(PORT_N, NPORTS, port);
    uint32_t to = !strcmp(port, "BLE-DEV") ? TO_BLE_DEV : !strcmp(port, "USBH") ? TO_USBH_SIM : p >= 0 ? 1u << p : 0u, i;
    for (i = 0; i < n_out; i++)
        if (((out_pkt[i] ^ pkt) & 0xFFFFFF00u) == 0 && (out_to[i] & to))
            break;
    if (to && (i < n_out) == sent) {
        printf("  ok %s %02X %02X %02X -> %s\n", sent ? "out" : "not out", (pkt >> 8) & 0xFF, (pkt >> 16) & 0xFF,
               pkt >> 24, port);
        return 0;
    }
    printf("FAIL %s %s %02X %02X %02X: %s\n", sent ? "expect_out" : "expect_no_out", port, (pkt >> 8) & 0xFF,
           (pkt >> 16) & 0xFF, pkt >> 24, !to ? "no such port" : sent ? "not sent" : "sent");
    return 1;
}

static int expect(const char *what, const char *val)
{
    int got;
#ifdef APP_HOST_EXPECT
    if (app_expect(what, &got))                   /* the app's own state (app.h), first: it may count its own way */
        ;
    else
#endif
    if (usbh_expect(what, &got))                /* the USB host's device (usbh, usbh_in, usbh_out, usbh_clocks) */
        ;
    else
    if (!strcmp(what, "store_writes"))
        got = (int)store_writes;
    else if (!strcmp(what, "restarts"))            /* plat_restart's since the start */
        got = (int)restarts;
    else if (!strcmp(what, "store_tries"))
        got = (int)store_tries;
    else if (!strcmp(what, "stats")) {             /* the debug stats' records in the flash (at most 60) */
        uint8_t at[DS_SLOTS];
        got = (int)ds_list(at);
    } else if (!strcmp(what, "stats_pending")) {   /* their queue's not in the flash yet */
        uint32_t j;
        for (j = 0, got = 0; j < ds_ni.nq; j++)
            got += !ds_ni.q[j].seq;
    } else if (!strcmp(what, "stats_on"))          /* the switch (plat_stats_enable) */
        got = ds_on;
    else if (!strcmp(what, "stk_ever_main"))       /* plat_stack_least_ever: the least room ever, bytes (-1 not known) */
        got = plat_stack_least_ever(PLAT_STK_MAIN);
    else if (!strcmp(what, "stk_ever_irq"))
        got = plat_stack_least_ever(PLAT_STK_IRQ);
    else if (!strcmp(what, "stk_ever_c1"))
        got = plat_stack_least_ever(PLAT_STK_C1);
    else if (!strcmp(what, "stats_rc"))            /* the last write's: 0 kept, < 0 not (-7: an update staged) */
        got = ds_last_rc;
    else if (!strcmp(what, "crashes")) {           /* the flash history's records, as a readout lists them (at most 8) */
        uint32_t at[CL_KEEP];
        got = (int)cl_list(at);
    } else if (!strcmp(what, "crash_count"))       /* the simulated .noinit record's count (0: none, a power-on) */
        got = hcr.magic == HCR_MAGIC ? (int)hcr.count : 0;
    else if (!strcmp(what, "crash_boot_rc"))       /* the last boot's cl_boot: 1 kept, 0 nothing new, < 0 not kept */
        got = hcr_rc;
#if FM1B_BACKUP
    else if (!strcmp(what, "restore_rc"))
        got = restore_rc;
#endif
    else if (!strncmp(what, "data.", 5)) {         /* data.ADDR: the flash's byte at ADDR as plat_data reads it (-1 outside
                                                     * its windows) */
        const uint8_t *p = plat_data((uint32_t)strtoul(what + 5, 0, 0));
        got = p ? (int)*p : -1;
    } else if (!strcmp(what, "data_rc"))           /* the last data_erase / data_write: 0 done, -1 refused, -2 no bytes */
        got = data_cmd_rc;
#ifdef APP_BATTERY_RAW
    else if (!strcmp(what, "battery_raw"))          /* plat_battery_raw: the battery's reading (the host's: full) */
        got = (int)plat_battery_raw();
#endif
#ifdef APP_DIM_LEVELS
    else if (!strcmp(what, "dim_ns"))               /* the dim pulse (plat_dim_ns, else the glow's), ns */
        got = (int)dim_pulse();
#endif
    else if (!strcmp(what, "clocks_out"))
        got = (int)clock_out_count;
    else if (!strcmp(what, "usb_play_gain"))        /* plat_usb_play_gain's level, 0..4096 */
        got = (int)usb_play_q12;
    else if (!strcmp(what, "usb_audio_full"))       /* plat_usb_audio_full: 1 full, 0 at MASTER */
        got = uac_full;
    else if (!strcmp(what, "midi_out"))
        got = (int)midi_out_count;
    else if (!strcmp(what, "midi_in_from"))         /* plat_midi_in_from: 1 USB, 2 BLE, 8 BLE-DEV, 0 TRS */
        got = (int)plat_midi_in_from();
    else if (!strcmp(what, "room_ble") || !strcmp(what, "room_ble_dev"))   /* plat_midi_out_room of one link */
        got = (int)plat_midi_out_room(what[8] ? TO_BLE_DEV : TO_BLE_HOST);
    else if (!strcmp(what, "ble"))
        got = ble_state;
    else if (!strcmp(what, "ble_dev"))              /* the Bluetooth MIDI device: BLE_DEV_* (4 connected) */
        got = bdev_state;
#ifdef APP_CPU_KHZ
    else if (!strcmp(what, "cpu_khz"))              /* plat_cpu_khz: the CPU's clock (the host's: 0, none) */
        got = (int)plat_cpu_khz();
#endif
#if FM1B_BT_AUDIO
    else if (!strcmp(what, "bta"))                  /* Bluetooth audio: PLAT_BTA_* (7 streaming) */
        got = bth_st;
    else if (!strcmp(what, "bta_known"))            /* its remembered devices */
        got = (int)bth_nmem;
#endif
    else if (!strcmp(what, "midi_in_usb"))          /* plat_midi_count: messages in from USB */
        got = (int)plat_midi_count(PORT_USB, 0);
    else if (!strcmp(what, "headphones"))           /* plat_headphones: the script's jack (1 in, 0 out) */
        got = plat_headphones();
    else if (!strcmp(what, "peak_db_max") || !strcmp(what, "peak_db_min")) {
        double db = 20.0 * log10(peak_out > 1e-9f ? peak_out : 1e-9f);
        int bad = what[8] == 'm' && what[9] == 'a' ? db > atof(val) : db < atof(val);
        printf("%s peak %.1f dBFS (%s %s)\n", bad ? "FAIL" : "  ok", db, what + 8, val);
        return bad;
    } else {
        printf("FAIL unknown expect %s\n", what);
        return 1;
    }
    if (val[0] == '<' || val[0] == '>') {
        int lim = (int)strtol(val + 1, 0, 0);
        if (val[0] == '<' ? got >= lim : got <= lim) {
            printf("FAIL expect %s %s, got %d\n", what, val, got);
            return 1;
        }
        printf("  ok %s == %d (%s)\n", what, got, val);
        return 0;
    }
    if (got != (int)strtol(val, 0, 0)) {
        printf("FAIL expect %s == %s, got %d\n", what, val, got);
        return 1;
    }
    printf("  ok %s == %d\n", what, got);
    return 0;
}

#ifndef PLAT_WEB
/* a file name of a script's: "out:NAME" is NAME in the output directory */
static void out_path(char *out, size_t n, const char *dir, const char *a)
{
    if (!strncmp(a, "out:", 4))
        snprintf(out, n, "%s/%s", dir, a + 4);
    else
        snprintf(out, n, "%s", a);
}
#if FM1B_MIRROR
/* --serve: the simulator driven over stdin (see the top) */
static uint32_t sv_unpack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)(*in++ & 0x7Fu) << nb;
        nb += 7u;
        if (nb >= 8u) {
            out[o++] = (uint8_t)acc;
            acc >>= 8;
            nb -= 8u;
        }
    }
    return o;
}
static int serve(const char *dir)
{
    static char line[8192];
    static uint8_t raw[4096], dec[4096];
    char *buf = 0;
    size_t blen = 0;
    FILE *ms = open_memstream(&buf, &blen);
    mr_out = ms;
    mr_keep = 0;
    setvbuf(stdout, 0, _IOLBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        char cmd[16] = {0}, a[4096] = {0}, b[64] = {0};
        if (sscanf(line, "%15s %4095s %63s", cmd, a, b) < 1)
            continue;
        if (!strcmp(cmd, "quit"))
            break;
#ifdef APP_HOST_CMD
        if (!strcmp(cmd, "expect") && app_host_cmd(cmd, a, b, "", ""))
            continue;                                /* (an app's own check with a value of its own: its line) */
#endif
        if (!strcmp(cmd, "sx")) {
            uint32_t n = 0, d;
            for (const char *h = a; h[0] && h[1] && n < sizeof raw; h += 2) {
                unsigned v;
                sscanf(h, "%2x", &v);
                raw[n++] = (uint8_t)v;
            }
            if (n < 3 || raw[0] != 0xF0 || raw[n - 1] != 0xF7)
                continue;
            if (cl_request(raw + 1, n - 2u) || ds_request(raw + 1, n - 2u))
                continue;                            /* (the crash history, the stats: the platform's, before the app's) */
            d = sv_unpack7(raw + 1, n - 2, dec);
            if (d >= 7 && dec[0] == 0 && dec[1] == 0x59 && (uint32_t)(dec[3] | dec[4] << 8 | dec[5] << 16) + 7u == d) {
                uint32_t sum = 0;
                for (uint32_t i = 6; i + 1 < d; i++)
                    sum += dec[i];
                if ((uint8_t)~sum != dec[d - 1])
                    continue;
                if (d == 8 && dec[2] == MR_REQ)
                    mirror_cmd(dec[6]);
                else if (d == 12 && dec[2] == MR_REMOTE)
                    mirror_remote(dec + 6);
            }
        } else if (!strcmp(cmd, "run")) {
            static uint8_t part[1 << 16];             /* a message cut at the end of a run: its start */
            static size_t pn;
            run_ms((uint32_t)atoi(a));
            fflush(ms);
            for (size_t i = 0; i < blen; i++) {
                uint8_t v = (uint8_t)buf[i];
                if (v == 0xF0)
                    pn = 0;
                if (pn < sizeof part)
                    part[pn++] = v;
                if (v == 0xF7 && part[0] == 0xF0) {
                    fputs("sx ", stdout);
                    for (size_t k = 0; k < pn; k++)
                        printf("%02X", part[k]);
                    putchar('\n');
                    pn = 0;
                }
            }
            rewind(ms);
            blen = 0;
            printf("ok %u\n", now_ms);
        } else if (!strcmp(cmd, "shot") || !strcmp(cmd, "fbdump")) {
            char out[4200];
            snprintf(out, sizeof out, "%s%s%s", a[0] == '/' ? "" : dir, a[0] == '/' ? "" : "/", a);
            if (cmd[0] == 's')
                png_write(out);
            else {
                FILE *f = fopen(out, "wb");
                if (f) {
                    fwrite(fb, 2, 240 * 240, f);
                    fclose(f);
                }
            }
            printf("ok\n");
        } else if (!strcmp(cmd, "expect"))
            expect(a, b);                            /* (its line: "  ok ..." or "FAIL ...", notes before it) */
        else
            printf("fail ?\n");
    }
    return 0;
}
#endif

/* a frame's hex (spaces allowed between the bytes) into raw: its length, 0 = not hex */
static uint32_t hsx_hex(const char *h, uint8_t *raw, uint32_t max)
{
    uint32_t n = 0;
    unsigned v;
    while (*h) {
        if (*h == ' ' || *h == '\t' || *h == '\n' || *h == '\r') {
            h++;
            continue;
        }
        if (n >= max || sscanf(h, "%2x", &v) != 1 || !h[1] || h[1] == ' ')
            return 0;
        raw[n++] = (uint8_t)v;
        h += 2;
    }
    return n;
}
/* "sysex": the frame as felucca-bsp's usb.c collects one (whole, 7-bit, HOST_SX_MAX at most between F0 and F7) and the
 * main loop offers it (device/main_fm1.c: to the app after the platform's own) */
#define HOST_SX_MAX 640u
static int hsx_in(const char *hex)
{
    static uint8_t raw[8192];
    uint32_t n = hsx_hex(hex, raw, sizeof raw), i, ok;
    if (n < 2u || raw[0] != 0xF0u || raw[n - 1u] != 0xF7u) {
        printf("FAIL sysex: not a frame (F0 .. F7, hex)\n");
        return 1;
    }
    for (i = 1, ok = 1; i + 1u < n; i++)
        ok &= raw[i] < 0x80u;
#ifdef APP_SYSEX_BYTE
    for (i = 0; i < n && (i == 0 || i + 1u == n || raw[i] < 0x80u); i++)
        app_sysex_byte(raw[i]);                   /* (as the USB interrupt hands them over: to a status inside) */
#endif
    if (!ok || n - 2u > HOST_SX_MAX || !hsx_session)
        return 0;                                 /* (a status byte inside, or too long: the device drops it) */
    if (cl_request(raw + 1, n - 2u) || ds_request(raw + 1, n - 2u))
        return 0;                                 /* (the crash history, the stats: the platform's, before the app's) */
#ifdef APP_SYSEX
    (void)app_sysex(raw + 1, n - 2u);
#endif
    return 0;
}
static int hsx_frame_at(uint32_t at)              /* the length of the frame sent at hsx_out[at] (to its F7; one cut
                                                   * short, to the next F0) */
{
    uint32_t k = at;
    while (k < hsx_n && hsx_out[k] != 0xF7u && (k == at || hsx_out[k] != 0xF0u))
        k++;
    if (k < hsx_n && hsx_out[k] == 0xF0u)
        return (int)(k - at);
    return (int)(k < hsx_n ? k + 1u - at : hsx_n - at);
}
static int hsx_expect(const char *hex)            /* 0: this frame went out since the last "sysex_out" */
{
    static uint8_t want[HSX_KEEP];
    uint32_t n = hsx_hex(hex, want, sizeof want), at;
    for (at = 0; n && at < hsx_n; at += (uint32_t)hsx_frame_at(at))
        if ((uint32_t)hsx_frame_at(at) == n && !memcmp(hsx_out + at, want, n)) {
            printf("  ok sysex out (%u bytes)\n", n);
            return 0;
        }
    printf("FAIL expect_sysex: not sent (%u bytes)\n", n);
    return 1;
}
static void hsx_print(void)
{
    uint32_t at, k;
    for (at = 0; at < hsx_n; at += k) {
        k = (uint32_t)hsx_frame_at(at);
        printf("  sysex<");
        for (uint32_t i = 0; i < k; i++)
            printf(" %02X", hsx_out[at + i]);
        printf("\n");
    }
    if (hsx_lost)
        printf("  sysex< (%u bytes more, not kept)\n", hsx_lost);
    hsx_n = hsx_lost = 0;
}

int main(int argc, char **argv)
{
    FILE *sc;
    static char line[20000];
    char out[400];
    const char *dir = argc > 2 ? argv[2] : ".";
    int lineno = 0, fails = 0;
    plat_host_dir = dir;
    if (argc < 2) {
        fprintf(stderr, "usage: %s SCRIPT [OUTDIR]%s\n", argv[0], FM1B_MIRROR ? " | --serve [OUTDIR]" : "");
        return 2;
    }
    if (getenv("FM1B_FLASH") && getenv("FM1B_FLASH")[0]) {   /* a power-on with that flash (a 1 MiB image) */
        uint32_t k;
        if (himg_load(getenv("FM1B_FLASH"), &k)) {
            fprintf(stderr, "FM1B_FLASH %s: no 1 MiB image there\n", getenv("FM1B_FLASH"));
            return 2;
        }
        if (strcmp(argv[1], "--serve"))            /* (--serve: stdout is the tool's) */
            printf("  ok   power-on with the flash of %s (%u store objects)\n", getenv("FM1B_FLASH"), (unsigned)k);
    }
#if FM1B_MIRROR
    if (!strcmp(argv[1], "--serve")) {
        snprintf(out, sizeof out, "%s/midi_out.txt", dir);
        midi_log = fopen(out, "w");
        boot();
        return serve(dir);
    }
#endif
    sc = fopen(argv[1], "r");
    if (!sc) {
        perror(argv[1]);
        return 2;
    }
    snprintf(out, sizeof out, "%s/midi_out.txt", dir);
    midi_log = fopen(out, "w");
    if (getenv("FM1B_SAID_LOG")) {                       /* a reader listens from the start, quietly: the log only */
        said_log_f = fopen(getenv("FM1B_SAID_LOG"), "a");
        sim_listen = 1;
        said_print = 0;
    }
    boot();
    if (sim_listen) {
        static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
        ann_frame(HELLO, sizeof HELLO, now_ms);
    }
    while (fgets(line, sizeof line, sc)) {
        char cmd[32] = {0}, a[256] = {0}, b[64] = {0}, c[64] = {0}, d[64] = {0};
        lineno++;
        if (line[0] == '#' || sscanf(line, "%31s %255s %63s %63s %63s", cmd, a, b, c, d) < 1)
            continue;
        plat_host_line = line;
#ifdef APP_HOST_CMD
        if (app_host_cmd(cmd, a, b, c, d))          /* the app's own commands (app.h; more words: plat_host_line) */
            continue;
#endif
        if (!strcmp(cmd, "wait"))
            run_ms((uint32_t)atoi(a));
        else if (!strcmp(cmd, "press") || !strcmp(cmd, "release") || !strcmp(cmd, "tap")) {
            int i = find_name(BTN_N, NB, a);
            if (i < 0) {
                printf("line %d: no button %s\n", lineno, a);
                return 2;
            }
            if (cmd[0] != 'r')
                held_btn |= 1u << i;
            if (cmd[0] == 't')
                run_ms(40);
            if (cmd[0] != 'p')
                held_btn &= ~(1u << i);
            run_ms(cmd[0] == 't' ? 40 : 2);
        } else if (!strcmp(cmd, "key") || !strcmp(cmd, "tapkey")) {
            int k = key_of(a);
            int down = cmd[0] == 't' || !strcmp(b, "down");
            uint32_t vel = (uint32_t)atoi(cmd[0] == 't' ? b : c), press = (uint32_t)atoi(cmd[0] == 't' ? "" : d);
            (void)vel;
            (void)press;
#ifdef APP_KEY_SENSE
            app_host_key((uint32_t)k, down ? (vel ? vel : 100u) : 0u, press);   /* the readings it gives (app.h) */
#endif
            if (down)
                held_keys |= 1u << k;
            else
                held_keys &= ~(1u << k);
            if (cmd[0] == 't') {
                run_ms(60);
#ifdef APP_KEY_SENSE
                app_host_key((uint32_t)k, 0, 0);
#endif
                held_keys &= ~(1u << k);
                run_ms(30);
            } else {
                run_ms(2);
            }
        } else if (!strcmp(cmd, "strum")) {          /* strum A B [MS]: a finger across the white keys */
            int x = atoi(a), y = atoi(b), gap = c[0] ? atoi(c) : 25, s = y >= x ? 1 : -1;
            for (;; x += s) {
                held_keys |= 1u << HOST_WHITE_K[x & 15];
                run_ms((uint32_t)gap);
                held_keys &= ~(1u << HOST_WHITE_K[x & 15]);
                if (x == y)
                    break;
            }
            run_ms(2);
        } else if (!strcmp(cmd, "turn")) {
            int e = find_name(ENC_N, NE, a), n = atoi(b), s = n > 0 ? 1 : -1;
            uint32_t gap = c[0] ? (uint32_t)atoi(c) : 80u;
            if (e < 0) {
                printf("line %d: no encoder %s\n", lineno, a);
                return 2;
            }
            for (; n; n -= s) {
                enc_acc[e] += s;
                run_ms(gap);
            }
        } else if (!strcmp(cmd, "spin")) {
            int e = find_name(ENC_N, NE, a);
            if (e < 0) {
                printf("line %d: no encoder %s\n", lineno, a);
                return 2;
            }
            enc_acc[e] += atoi(b);
            plat_host_exact = 1;
            run_ms(1);
            plat_host_exact = 0;
        } else if (!strcmp(cmd, "jack")) {          /* jack in | jack out: the headphone jack (plat_headphones) */
            host_hp = !strcmp(a, "in");
            run_ms(2);
        } else if (!strcmp(cmd, "master")) {
            master = (uint32_t)atoi(a);
            run_ms(40);                         /* as the device's smoothing settles */
        } else if (!strcmp(cmd, "midi")) {
            int dev = !strcmp(a, "BLE-DEV"), p = dev ? PORT_BLE : find_name(PORT_N, NPORTS, a);
            if ((p < 0 || p == PORT_USB) && !dev && usbh_port()) {   /* (the port is a host: no computer to send it) */
                printf("  midi USB: the port is a USB host, no computer: dropped\n");
            } else {
                min_q[mi_w % MQ] = p < 0 ? mk_pkt(a, b, c) : mk_pkt(b, c, d) | (dev ? 0x10u : 0u);   /* cable 1 */
                min_port[mi_w++ % MQ] = p < 0 ? PORT_USB : (uint32_t)p;
            }
            run_ms(2);
        } else if (!strcmp(cmd, "usbh") && !strcmp(a, "clock")) {   /* usbh clock N [MS]: N clocks from the device */
            int n = atoi(b), gap = c[0] ? atoi(c) : 21;
            for (; n > 0; n--) {
                fails += usbh_cmd("midi", "F8", "", "");
                run_ms((uint32_t)gap);
            }
        } else if (!strcmp(cmd, "usbh")) {
            fails += usbh_cmd(a, b, c, d);
            run_ms(2);
        } else if (!strcmp(cmd, "ble")) {
            if (!strcmp(a, "connect") && ble_state == BLE_READY) {
                ble_conn = 1;
                ble_say("connected");
            } else if (!strcmp(a, "link") && ble_state == BLE_READY) {   /* linked, its MIDI not subscribed */
                ble_link = 1;
                ble_say("connected");
            } else if (!strcmp(a, "disconnect")) {
                ble_conn = ble_link = 0;
                ble_say("disconnected: reason 13");
            } else {
                printf("line %d: ble %s: the stack is not ready\n", lineno, a);
                fails++;
            }
            run_ms(2);
#if FM1B_BT_AUDIO
        } else if (!strcmp(cmd, "bta")) {
            fails += bta_script(a, b, lineno);
            run_ms(2);
#endif
        } else if (!strcmp(cmd, "out")) {
            uint32_t i;
            for (i = 0; i < n_out; i++)
                if (((out_pkt[i] >> 8) & 0xFF) != 0xF8)
                    printf("  out %02X %02X %02X ->%s%s%s\n", (out_pkt[i] >> 8) & 0xFF, (out_pkt[i] >> 16) & 0xFF,
                           out_pkt[i] >> 24, out_to[i] & TO_USB ? " USB" : out_to[i] & TO_USBH_SIM ? " USBH" : "", out_to[i] & TO_BLE_HOST ? " BLE" : "",
                           out_to[i] & TO_BLE_DEV ? " BLE-DEV" : "");
            n_out = 0;
        } else if (!strcmp(cmd, "expect_out") || !strcmp(cmd, "expect_no_out")) {
            fails += expect_out(a, mk_pkt(b, c, d), !strcmp(cmd, "expect_out"));
        } else if (!strcmp(cmd, "wav")) {
            snprintf(out, sizeof out, "%s/%s", dir, a);
            wav_open(out);
        } else if (!strcmp(cmd, "wavstop"))
            wav_close();
        else if (!strcmp(cmd, "usbwav")) {
            snprintf(out, sizeof out, "%s/%s", dir, a);
            if (uwav)
                wav_end(uwav, uwav_frames, FM1B_UAC_CH);
            uwav = wav_start(out);
            uwav_frames = 0;
        } else if (!strcmp(cmd, "usbwavstop")) {
            if (uwav)
                wav_end(uwav, uwav_frames, FM1B_UAC_CH);
            uwav = 0;
        }
        else if (!strcmp(cmd, "peakreset"))
            peak_out = 0.0f;
        else if (!strcmp(cmd, "shot")) {
            snprintf(out, sizeof out, "%s/%s", dir, a);
            png_write(out);
        } else if (!strcmp(cmd, "leds")) {
            int i;
            printf("  leds: buttons");
            for (i = 0; i < NB; i++)
                if (lit_btn >> i & 1u)
                    printf(" %s", BTN_N[i]);
            printf(" | white ");
            for (i = 0; i < 16; i++)
                printf("%c", (lit_keys >> HOST_WHITE_K[i] & 1u) ? '#' : ((dim_keys | mid_keys) >> HOST_WHITE_K[i] & 1u) ? '+' : '.');
            printf(" black ");
            for (i = 0; i < 11; i++)
                printf("%c", (lit_keys >> HOST_BLACK_K[i] & 1u) ? '#' : ((dim_keys | mid_keys) >> HOST_BLACK_K[i] & 1u) ? '+' : '.');
#ifdef APP_DIM_LEVELS
            printf(" dim %u ns", (unsigned)dim_pulse());
#endif
            printf("\n");
        } else if (!strcmp(cmd, "expect"))
            fails += expect(a, b);
#if FM1B_RES
        else if (!strcmp(cmd, "res"))             /* res 0|1: the resource region there or not */
            res_there = atoi(a) != 0;
#endif
        else if (!strcmp(cmd, "data_erase"))      /* data_erase OFFSET: plat_data_erase, as an app calls it */
            data_cmd_rc = plat_data_erase((uint32_t)strtoul(a, 0, 0));
        else if (!strcmp(cmd, "data_write")) {    /* data_write OFFSET HEX: plat_data_write of those bytes */
            uint8_t w[32];
            uint32_t n = 0;
            while (n < sizeof w && isxdigit((unsigned char)b[2u * n]) && isxdigit((unsigned char)b[2u * n + 1u])) {
                char h[3] = {b[2u * n], b[2u * n + 1u], 0};
                w[n++] = (uint8_t)strtoul(h, 0, 16);
            }
            data_cmd_rc = n && !b[2u * n] ? plat_data_write((uint32_t)strtoul(a, 0, 0), w, n) : -2;
        } else if (!strcmp(cmd, "data")) {        /* data FILE OFFSET: a file into the data area (plat_data) */
            FILE *df = fopen(a, "rb");
            uint32_t off = (uint32_t)strtoul(b, 0, 0), hi = data_window_hi(off);
            data_init();
            if (!df || !hi) {
                printf("FAIL data %s %s: no such file, or outside the data area\n", a, b);
                fails++;
            } else {                              /* (data_area starts at DATA_BASE: the first window) */
                size_t n = fread(&data_area[off - DATA_BASE], 1, hi - off, df);
                printf("  ok   data: %u B at %#x\n", (unsigned)n, (unsigned)off);
            }
            if (df)
                fclose(df);
        } else if (!strcmp(cmd, "flash_dump") || !strcmp(cmd, "flash_load")) {   /* FILE: the whole flash out / in */
            char path[400];
            uint32_t k = 0;
            int load = cmd[6] == 'l';
            out_path(path, sizeof path, dir, a);
            if (!a[0] || (load ? himg_load(path, &k) : himg_dump(path))) {
                printf("FAIL %s %s: %s\n", cmd, a, load ? "no 1 MiB image there" : "not written");
                fails++;
            } else if (load)
                printf("  ok   flash_load %s: the data windows and %u store objects\n", a, (unsigned)k);
            else
                printf("  ok   flash_dump %s\n", a);
        } else if (!strcmp(cmd, "reboot")) {
#ifdef APP_HOST_REBOOT
            app_host_reboot();                  /* the app forgets what RAM would (app.h) */
#endif
            usbh_reboot();                      /* (every boot is a device: plat_usb_host) */
            boot();
        } else if (!strcmp(cmd, "crash") || !strcmp(cmd, "crash_in_save") || !strcmp(cmd, "power")) {
            if (!strcmp(cmd, "power")) {
                memset(&hcr, 0, sizeof hcr);    /* (.noinit lost; the flash kept) */
                memset(&ds_ni, 0x5A, sizeof ds_ni);   /* (the stats' queue: what a power-on finds, not its own) */
                hds_boots = 0;                  /* (fm1b_dbg.boots: from 0 after a power-on) */
            } else                               /* crash [PC [VEC]] (hex): the fault, then the restart */
                hcr_crash(a[0] ? (uint32_t)strtoul(a, 0, 16) : 0x02012344u, b[0] ? (uint32_t)strtoul(b, 0, 16) : 3u,
                          cmd[5] != 0);
#ifdef APP_HOST_REBOOT
            app_host_reboot();
#endif
            boot();
            printf("  %s at %u ms: restarted, crash count %u, history %d\n", cmd, now_ms,
                   hcr.magic == HCR_MAGIC ? hcr.count : 0u, hcr_rc);
        } else if (!strcmp(cmd, "stats_switch")) {   /* stats_switch 1|0: plat_stats_enable, as an app's setting */
            plat_stats_enable(atoi(a));
        } else if (!strcmp(cmd, "stack")) {         /* stack MAIN IRQ C1: the marks used down to these bytes left */
            if (a[0] && b[0] && c[0])
                hst_left[0] = atoi(a), hst_left[1] = atoi(b), hst_left[2] = atoi(c);
            else
                printf("stack: MAIN IRQ C1 (bytes left, -1 not measured)\n");
        } else if (!strcmp(cmd, "stack_guard")) {   /* stack_guard 1: the next crash is a stack in its guard */
            hst_guard = (uint32_t)(atoi(a) != 0);
        } else if (!strcmp(cmd, "ota_stage") || !strcmp(cmd, "ota_unstage")) {
            data_init();                        /* (ota.c: a loader's first word at 0xE0000; its unstage: all 5) */
            if (cmd[4] == 's')
                memset(&data_area[HDS_OTA_AREA - DATA_BASE], 0x00, 4u);
            else
                memset(&data_area[HDS_OTA_AREA - DATA_BASE], 0xFF, 0x5000u);
        } else if (!strcmp(cmd, "listen")) {
            static const uint8_t HELLO[] = {ANN_H0, ANN_H1, ANN_H2, ANN_HELLO, ANN_VER};
            sim_listen = 1;
            said_len = 0;
            said_log[0] = 0;
            ann_frame(HELLO, sizeof HELLO, now_ms);
            run_ms(40);
        } else if (!strcmp(cmd, "speakdump")) {        /* every screen's text for a screen reader, to stdout */
#ifdef APP_HOST_SPEAKDUMP
            app_speak_dump();
#else
            printf("speakdump: this app has none (app_speak_dump)\n");
#endif
        } else if (!strcmp(cmd, "where")) {
            static const uint8_t WHERE[] = {ANN_H0, ANN_H1, ANN_H2, ANN_WHERE};
            ann_frame(WHERE, sizeof WHERE, now_ms);
            run_ms(40);
        } else if (!strcmp(cmd, "said") || !strcmp(cmd, "unsaid")) {
            int un = cmd[0] == 'u';
            char *t = line + strlen(cmd), *e;
            while (*t == ' ')
                t++;
            e = t + strlen(t);
            while (e > t && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
                *--e = 0;
            if ((strstr(said_log, t) == 0) != un) {
                printf("FAIL line %d: %s \"%s\" (since the last check: %s)\n", lineno, un ? "said, not wanted" : "not said",
                       t, said_len ? said_log : "nothing\n");
                fails++;
            } else {
                printf("  ok %s \"%s\"\n", cmd, t);
            }
            said_len = 0;
            said_log[0] = 0;
        } else if (!strcmp(cmd, "said_none")) {
            if (said_len) {
                printf("FAIL line %d: said_none: said %s", lineno, said_log);
                fails++;
            } else {
                printf("  ok said nothing\n");
            }
            said_len = 0;
            said_log[0] = 0;
        } else if (!strcmp(cmd, "fbdump")) {          /* fbdump FILE: the screen's pixels as stored (high byte first) */
            FILE *f;
            snprintf(out, sizeof out, "%s/%s", dir, a);
            if (!(f = fopen(out, "wb"))) {
                printf("line %d: cannot write %s\n", lineno, out);
                return 2;
            }
            fwrite(fb, 2, 240 * 240, f);
            fclose(f);
        } else if (!strcmp(cmd, "flash_fail")) {
            store_fail = atoi(a);
        } else if (!strcmp(cmd, "clock")) {           /* clock N [MS]: N MIDI clocks in (F8), MS apart (default 21) */
            int n = atoi(a), gap = b[0] ? atoi(b) : 21;
            for (; n > 0; n--) {
                if (usbh_port()) {                  /* (the port is a host: no computer) */
                    run_ms((uint32_t)gap);
                    continue;
                }
                min_port[mi_w % MQ] = PORT_USB;
                min_q[mi_w++ % MQ] = 0x0Fu | 0xF8u << 8;
                run_ms((uint32_t)gap);
            }
#if FM1B_MIRROR
        } else if (!strcmp(cmd, "mirror")) {
            if (!strcmp(a, "off"))
                mirror_cmd(0);
            else if (!strcmp(a, "refresh"))
                mirror_cmd(2);
            else if (!strcmp(a, "nokeep"))            /* the viewer stops its keep-alive (gone without "off") */
                mr_keep = 0;
            else if (!strcmp(a, "unplug")) {          /* the cable out (once what was queued went out): no host */
                if (mr_out) {
                    uint32_t r0 = mr_rate;
                    mr_rate = MRQ + sizeof hctl;
                    host_mirror_ms();
                    mr_rate = r0;
                    fclose(mr_out);
                }
                mr_out = 0;
                mr_mid = 0;
                mr_link_n++;
            } else {
                snprintf(out, sizeof out, "%s/%s", dir, !strncmp(a, "out:", 4) ? a + 4 : a);
                if (mr_out)
                    fclose(mr_out);
                if (!(mr_out = fopen(out, "wb"))) {
                    printf("line %d: cannot write %s\n", lineno, out);
                    return 2;
                }
                mr_keep = 1;
                mr_keep_t = now_ms;
                mirror_cmd(2);
            }
            run_ms(1);
        } else if (!strcmp(cmd, "mirror_rate")) {     /* mirror_rate N: USB's pace for the mirror, bytes a millisecond */
            mr_rate = (uint32_t)atoi(a);
#endif
#if FM1B_BACKUP
        } else if (!strcmp(cmd, "backup") || !strcmp(cmd, "restore")) {
            out_path(out, sizeof out, dir, a);
            if (cmd[0] == 'b') {
                if (sim_backup(out)) {
                    printf("line %d: cannot write %s\n", lineno, out);
                    return 2;
                }
            } else {                                  /* (the device's message after it: main_fm1.c usb_cmd) */
                restore_rc = sim_restore(out);
                app_say(restore_rc == 10 ? "STOP FIRST" : "RESTORE FROM USB",
                        !restore_rc ? "RESTORED" : restore_rc == 10 ? "then restore" : "NOT RESTORED");
            }
            run_ms(1);
        } else if (!strcmp(cmd, "backup_damage")) {   /* a byte of a backup file changed (at its middle) */
            FILE *bf;
            out_path(out, sizeof out, dir, a);
            if ((bf = fopen(out, "r+b"))) {
                long n;
                int ch;
                fseek(bf, 0, SEEK_END);
                n = ftell(bf);
                fseek(bf, n / 2, SEEK_SET);
                ch = fgetc(bf);
                fseek(bf, n / 2, SEEK_SET);
                fputc(ch ^ 1, bf);
                fclose(bf);
            }
#endif
        } else if (!strcmp(cmd, "sysex")) {
            fails += hsx_in(strstr(line, "sysex") + 5);
            run_ms(1);
        } else if (!strcmp(cmd, "sysex_out"))
            hsx_print();
        else if (!strcmp(cmd, "expect_sysex"))
            fails += hsx_expect(strstr(line, "expect_sysex") + 12);
        else if (!strcmp(cmd, "usb")) {
            if (!strcmp(a, "unplug"))
                hsx_session = 0, hsx_q = 0;
            else if (!strcmp(a, "plug"))
                hsx_session = ++hsx_plugs;
            else if (!strcmp(a, "stall") || !strcmp(a, "read"))
                hsx_stalled = a[0] == 's';
            else {
                printf("line %d: usb %s: unplug, plug, stall or read\n", lineno, a);
                return 2;
            }
            run_ms(1);
        } else if (!strcmp(cmd, "sysex_log")) {
            if (hsx_log)
                fclose(hsx_log);
            hsx_log = 0;
            if (strcmp(a, "off")) {
                out_path(out, sizeof out, dir, a);
                if (!(hsx_log = fopen(out, "wb"))) {
                    printf("line %d: cannot write %s\n", lineno, out);
                    return 2;
                }
            }
        } else if (!strcmp(cmd, "echo"))
            printf("%s", line + 5);
        else {
            printf("line %d: unknown command %s\n", lineno, cmd);
            return 2;
        }
    }
    wav_close();
    if (uwav)
        wav_end(uwav, uwav_frames, FM1B_UAC_CH);
    if (hsx_log)
        fclose(hsx_log);
    if (midi_log)
        fclose(midi_log);
#ifdef APP_HOST_FAILS
    fails += app_host_fails();                      /* the app's own checks along the way */
#endif
#if FM1B_MIRROR
    if (mr.frames)
        printf("mirror: %u frames, %u bytes (%u rects, %u rows sent, %u rows skipped, %u drops, %u redraws, %u inputs; "
               "%u pixels blitted)\n", mr.frames, mr.bytes, mr.rects, mr.rows, mr.skipped, mr.drops, mr.redraws,
               mr.inputs, mr.px);
    if (mr_out)
        fclose(mr_out);
#endif
    printf("host: %u ms simulated, %u blits, render %.1f%% of real time (host CPU)%s\n", now_ms, blits,
           render_budget_ns_total > 0 ? 100.0 * render_ns_total / render_budget_ns_total : 0.0,
           fails ? ", EXPECTATIONS FAILED" : "");
    return fails ? 1 : 0;
}
#endif /* PLAT_WEB */
#include <stdint.h>
uint32_t plat_audio_peak_us(void) { return 0; }
void plat_audio_peak_reset(void) {}
