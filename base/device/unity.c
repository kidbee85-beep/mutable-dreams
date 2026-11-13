/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* An fm1-base firmware for the M-VAVE FM-1: one compilation unit for the platform and the app
 * (Felucca's layout: the chip and board layers are header-only, so everything that touches them is here).
 * An app's heavy code (its DSP) may be separate units at -O2 (fm1.toml: o2); this one is -Os. Order matters.
 *
 * The app provides, on the include path:
 *   app.h  its configuration (below) and the contract: app_init, app_render, app_panic, app_ui_init,
 *          app_frame, app_input_only, app_say; its store objects (OBJ_PANEL among them, OBJ_NOBJ) and their
 *          flash map APP_STORE_MAP
 *   app.c  the app itself (its own unity build), compiled here after the platform
 * Configuration, each with a default:
 *   APP_NAME "TEMPLATE"     on the splash, crash and safe-mode screens and for the screen reader
 *   APP_VERSION "DEV"       a release build passes its own (tools/build.py --release)
 *   APP_BLOCK 256           frames per audio half (5.8 ms at 44.1 kHz), the render's block
 *   FM1B_CDC 0              USB serial console (a second USB function; fm1.toml usb_console = true): the app's text over
 *                           plat_console_* (console.c)
 *   FM1B_UAC 1              USB audio input: the master output, recordable over the cable at 44.1 kHz (checked on an
 *                           FM-1 2026-10-16: no lost frame); FM1B_UAC_48K 0 (48 kHz too: needs the resampler's filter)
 *   FM1B_UAC_DEEP 0         1: the USB audio input keeps a half more in hand, for renders that take most of
 *                           their time (its 4 KB ring in the app's pool; 5.8 ms more latency)
 *   FM1B_UAC_RING_POOL 0    1: the USB audio input's 2 KB ring in the pool, its RAM left to the app
 *   FM1B_UAC_OUT 1          USB audio output: the computer plays through the FM-1, mixed after the app and after the
 *                           USB recording's tap, at MASTER and plat_usb_play_gain (device/uac_mix.h)
 *   FM1B_TRS 1              MIDI in on the TRS jack
 *   APP_TOUCH               velocity and pressure from the keys (lib/touch.h)
 *   FM1B_BLE 0              Bluetooth MIDI (fm1.toml ble = true; tools/build.py --no-ble leaves it out)
 *   FM1B_MIRROR 0           the screen and panel mirrored over USB (fm1.toml mirror = true: mirror.c)
 *   FM1B_MSC 0              disk mode: the FM-1 as a USB drive of the app's blocks (fm1.toml msc = true: msc.c)
 *   FM1B_BACKUP 0           backup and restore of the app's objects over USB (fm1.toml backup = true: backup.c)
 *   FM1B_RES 0              the resource region (fm1.toml resources = FILE: res.c; with backup)
 *   FM1B_CPU2 0             the second core (fm1.toml cpu2 = true: cpu2.c, felucca-bsp's fm1_cpu1.h)
 *   FM1B_CACHE_RAM 0        data-cache ways as RAM, 2, 4 or 7 (fm1.toml cache_ram_ways: plat_cache_ram, felucca-bsp's
 *                           fm1_cache_ram.h; set and tested in fm1_cstart, main_fm1.c) */
#include <stdint.h>
#include "plat.h"
#include "app.h"                  /* first: the scan's hooks below may call the app */
#ifdef APP_TOUCH
#include "touch.h"                /* velocity and pressure from the keys (lib/touch.c) */
#endif
#ifdef APP_KEY_SENSE              /* the keys' readings (plat_sense) to the app, by key: 0..26 */
#define FM1_KEY_OF(col, rowbit) ((uint32_t)FM1_KEYMAP[rowbit][col] - 14u)
#define FM1_INPUT_ANALOG(col, rowbit, v, driven) \
    (FM1_KEYMAP[rowbit][col] >= 14 ? app_key_sample(FM1_KEY_OF(col, rowbit), (v), fm1_ticks(), (int)(driven)) : (void)0)
#define FM1_INPUT_DRIVE(col, rowbit) (FM1_KEYMAP[rowbit][col] >= 14 && app_key_drive(FM1_KEY_OF(col, rowbit)))
#define FM1_INPUT_PROBE(col, rowbit, v, pressed) \
    (FM1_KEYMAP[rowbit][col] >= 14 ? app_key_probe(FM1_KEY_OF(col, rowbit), (v), (pressed)) : (void)0)
#define FM1_INPUT_LED_OK(col, rowbit) (FM1_KEYMAP[rowbit][col] < 14 || app_key_led_ok(FM1_KEY_OF(col, rowbit)))
#endif
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_jack.h"               /* the headphone jack's switch (PB5): plat_headphones */
#include "fm1_lcd_hw.h"
#if FM1B_WIFI || FM1B_BLE
#define FL_UID 1                          /* the flash's unique ID: the WiFi MAC, the Bluetooth name (plat_chip_uid) */
#endif
#include "fm1_flash.h"
#include "fm1_xip.h"
#include "fm1_perf.h"
#include "fm1_clock.h"
#ifndef FM1B_CACHE_RAM
#define FM1B_CACHE_RAM 0
#endif
#if FM1B_CACHE_RAM
#include "fm1_cache_ram.h"        /* data-cache ways as RAM (fm1.toml cache_ram_ways): plat_cache_ram */
#endif

#include "libc.c"
#include "lcd.c"
#include "gfx.c"

static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR) */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")

#include "fm1b_config.h"          /* APP_NAME, APP_VERSION, APP_BLOCK ...: the app's, or the defaults */
#include "sys_text.c"             /* the faces of the platform's own screens: fm1-base's, or the app's (APP_SYS_TEXT) */
#define HALF_FRAMES APP_BLOCK     /* the audio half, as felucca-bsp's usb.c expects it */

#define FELUCCA_OTA 1             /* M-UPGRADE update entry: keeps the web installer working */
#ifndef FM1B_CDC
#define FM1B_CDC 0
#endif
#define FELUCCA_CDC FM1B_CDC
#ifndef FM1B_UAC
#define FM1B_UAC 1
#endif
#define FELUCCA_UAC FM1B_UAC
#ifndef FM1B_UAC_48K
#define FM1B_UAC_48K 0            /* 48 kHz too: needs uac_fir.h, the resampler's filter (not in fm1-base yet) */
#endif
#define FELUCCA_UAC_48K FM1B_UAC_48K
#ifndef FM1B_UAC_OUT
#define FM1B_UAC_OUT 1            /* the computer plays through the FM-1 (USB audio output) */
#endif
#define FELUCCA_UAC_OUT FM1B_UAC_OUT
#ifndef FM1B_UAC_CH
#define FM1B_UAC_CH 2             /* the USB audio input's channels: the master, and the app's on 3..N (plat_usb_ch) */
#endif
#define FELUCCA_UAC_CH FM1B_UAC_CH
#if FM1B_UAC_CH > 2
#define FELUCCA_UAC_STALL_POLLS 200u     /* the stream serviced every 10 kHz tick (main_fm1.c): 20 ms of polls */
#endif
#ifndef FM1B_UAC_DEEP
#define FM1B_UAC_DEEP 0           /* 1: the USB audio input holds a half more (renders past ~40 % of their time
                                   * otherwise run its ring dry: holds in the recording); its 4 KB ring in the pool */
#endif
#define FELUCCA_UAC_DEEP FM1B_UAC_DEEP
#ifndef FM1B_UAC_RING_POOL
#define FM1B_UAC_RING_POOL 0      /* 1: the USB audio input's ring (2 KB; deep: 4) in the pool, not in RAM (an app whose
                                   * RAM is full: fm1-radio) */
#endif
#if FM1B_UAC_DEEP || FM1B_UAC_RING_POOL
#define FELUCCA_UAC_RING_ATTR PLAT_POOL
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_000"     /* package identity (tools/build.py passes the firmware's) */
#endif
#ifndef FM1B_MIRROR
#define FM1B_MIRROR 0
#endif
#ifndef FM1B_RES
#define FM1B_RES 0
#endif
#ifndef FM1B_BACKUP
#define FM1B_BACKUP 0
#endif
#ifndef FM1B_CPU2
#define FM1B_CPU2 0
#endif
#define FELUCCA_MIRROR FM1B_MIRROR /* usb.c's ring for mirror.c's frames */
#ifndef FM1B_USB_HOST
#define FM1B_USB_HOST 0
#endif
#define FELUCCA_USB_HOST FM1B_USB_HOST /* the USB MIDI host (plat_usb_host) */
#ifndef FM1B_MSC
#define FM1B_MSC 0
#endif
#define FELUCCA_MSC FM1B_MSC      /* disk mode: usb.c's mass-storage descriptors and EP3 (msc.c) */
#ifndef FM1B_MIDI_STAMP
#define FM1B_MIDI_STAMP 0         /* 1: MIDI in stamped where it lands, USB and the jack (fm1.toml midi_stamp = true;
                                   * plat_midi_in_t24); 0: when the app read it */
#endif
#define FELUCCA_MIDI_STAMP FM1B_MIDI_STAMP
#ifndef FM1B_CRASHLOG
#define FM1B_CRASHLOG 0           /* 1: the crash history in flash, 0xE2000..0xE3FFF, and the stacks' room at a crash
                                   * (fm1.toml crash_history = true: crashlog.c, stacks.c) */
#endif
#ifndef FM1B_DBGSTATS
#define FM1B_DBGSTATS 0           /* 1: the debug stats in flash, 0xE4000..0xE4EFF (fm1.toml debug_stats = true:
                                   * dbgstats.c; with FM1B_CRASHLOG) */
#endif
#ifndef FM1B_HEADPHONES
#define FM1B_HEADPHONES 0         /* 1: the headphone jack read every main-loop pass (fm1.toml headphones = true;
                                   * plat_headphones); 0: not read, plat_headphones -1 */
#endif
#if defined APP_USB_HOST_NAME && FM1B_USB_HOST
#define FELUCCA_USB_HOST_NAME 1      /* the device's product string (plat_usb_host_name) */
#endif
#define SXQ 128u                  /* the SysEx send ring, in packets: a whole screen-reader SAY (ANN_MAX + 9 bytes) */
#ifdef APP_SYSEX_BYTE
#define FELUCCA_SYSEX_BYTE(b) app_sysex_byte(b)   /* the app's, every SysEx byte from USB (plat.h APP_SYSEX_BYTE) */
#endif
#include "usb.c"
#include "console.c"              /* the USB serial console's bytes for the app (plat_console_*, FM1B_CDC) */
#ifndef FM1B_TRS
#define FM1B_TRS 1
#endif
#if FM1B_TRS
#include "midi_uart.c"
#endif

static uint8_t flash_ok;          /* JEDEC id matched at boot */
#if FM1B_WIFI || FM1B_BLE
static uint8_t flash_uid[16];     /* the flash's factory unique ID (plat_chip_uid), read at boot */
#endif
static void audio_silence(void);
static int st_read(uint32_t off, void *dst, uint32_t n)   /* 256-byte IRQ-off windows: audio keeps up */
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static int st_erase(uint32_t off)
{
    uint32_t took;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    if (!FL_STORE_OK(off, n))
        return -8;
    return fl_write(off, src, n);
}
#include "storage.c"

struct bootguard_s { uint32_t magic, failed, pending; };
extern struct bootguard_s bootguard;
/* (no OTA_FLASH_ID: the identity reply's bytes 24..26 stay 0. Installers that strip only trailing zeros, as The
 * Firmware Hub's, take a reply with the flash ID for no FM-1 at all: "No FM-1 found", 2026-11-06) */
#if FM1B_BACKUP
static int rs_pending(void);    /* backup.c: a restore's journal waits for the next boot; an update would erase it */
#define OTA_STAGE_REFUSED() rs_pending()
#endif
#include "ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sys_box(0, 92, 240, SYS_S, "UPDATE", C_WHITE, 1);
    if (step < 9u) {
        sys_box(0, 124, 240, SYS_S, STEP[step < 6u ? step : 0], C_HI, 1);
        return;
    }
    sys_box(0, 124, 240, SYS_S, code == 1 ? "DRY RUN OK" : code == OTA_E_REFUSED ? "RESTART FIRST" : "FAILED", C_HI, 1);
    fm1_delay_ms(1500);
}
static void radio_clean_exit(void);
static void ota_commit(const uint8_t *parm)
{
    radio_clean_exit();                                   /* an update, not a crash: Bluetooth starts again after it */
    bootguard.pending = 0;                              /* intentional reset */
    bootguard.failed = 0;                               /* a new install starts with a clean slate */
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}

#define ANN_NAME APP_NAME " " APP_VERSION
#define ANN_DATA PLAT_POOL
#include "ann_words.h"            /* the link to the app's words file (tools/gen_words.py, from the app's words.tsv) */
#include "announce.c"             /* the screen as text for a screen reader (fm1-reader) */
#if FM1B_BACKUP
static uint8_t backup_streaming(void);
#else
static uint8_t backup_streaming(void) { return 0; }
#endif
static int ann_wire_send(const uint8_t *p, uint32_t n)   /* never waits: no room (nobody reading), a backup
                                                          * streaming: not sent */
{
    if (!usb.config || usb.suspended || backup_streaming() || sx_busy || SXQ - (so_w - so_r) < (n + 2u) / 3u)
        return -1;
    return ota_wire_send(p, n);
}
#include "panel.c"
#if FM1B_BLE || FM1B_WIFI
#include "radio.c"                /* the shim's platform, a radio's start; Bluetooth MIDI (FM1B_BLE, ble/: the shim, the
                                   * MIDI service) */
#else
#include "radio_none.c"           /* the build with no radio: nothing of JieLi's linked */
#endif
#if FM1B_WIFI
#include "wifi.c"                 /* WiFi (ble/wifi/, the SDK's lwIP) */
#endif
#if FM1B_MIRROR
#include "mirror.c"               /* the screen and the panel mirrored over USB (web/mirror/, tools/fm1_mirror.py) */
static uint32_t mr_free(void) { return MRQ - (mr_w - mr_r); }
static void mr_put(const uint8_t *p, uint32_t n)
{
    uint32_t w = mr_w, i;
    for (i = 0; i < n; i++)
        mr_q[(w + i) % MRQ] = p[i];
    RING_PUBLISH();                                 /* the bytes before the index: usb.c sends whole frames */
    mr_w = w + n;
}
static int mr_link(void) { return usb.config ? (int)(midi_links & 0x7FFFFFFFu) : -1; }
static int mr_busy(void) { return !usb.config || usb.suspended || backup_streaming(); }
static int mr_ctl_send(const uint8_t *p, uint32_t n)
{
    if (sx_busy || SXQ - (so_w - so_r) < (n + 2u) / 3u)
        return -1;
    return ota_wire_send(p, n);
}
static void mr_redraw(void)                         /* the app draws everything again (APP_REDRAW) */
{
#ifdef APP_REDRAW
    app_redraw();
#endif
}
static int mr_test(int on)                          /* a test session (APP_MIRROR_TEST): 1 done, 0 refused */
{
#ifdef APP_MIRROR_TEST
    return app_mirror_test(on);
#else
    return !on;
#endif
}
#else
static uint32_t mirror_in_buttons(void) { return 0; }
static uint32_t mirror_in_keys(void) { return 0; }
static int32_t mirror_in_enc(int role) { (void)role; return 0; }
static uint32_t mirror_in_master(uint32_t pot) { return pot; }
static void mirror_enc(int role, int32_t n) { (void)role, (void)n; }
static void mirror_leds(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys)
{
    (void)buttons, (void)keys, (void)dim_buttons, (void)dim_keys;
}
static void mirror_mid(uint32_t buttons, uint32_t keys) { (void)buttons, (void)keys; }
#endif
#include "plat_fm1.c"
#if FM1B_CPU2
#include "cpu2.c"                 /* the second core: plat_cpu2_* */
#endif
#if FM1B_MSC
#include "msc.c"                  /* disk mode: the FM-1 as a USB drive (plat_disk_*) */
#endif
#if FM1B_BACKUP
#include "backup.c"               /* backup and restore of the app's objects over USB (tools/fm1_backup.py) */
static uint8_t backup_streaming(void) { return bu.on; }
static int rs_fetch(uint32_t addr, uint8_t *dst, uint32_t n) { return ota_read(addr, dst, n); }
static int rs_note(uint32_t addr)                   /* (past the question: no session deadline, 3 tries) */
{
    uint8_t b[8];
    ota_deadline = 0;
    return ota_read(addr, b, 8);
}
static int rs_playing(void) { return app_backup_playing() != 0; }
static void rs_screen(const char *a, const char *b)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    sys_box(0, 92, 240, SYS_S, a, C_WHITE, 1);
    if (b)
        sys_box(0, 124, 240, SYS_S, b, C_HI, 1);
}
static void rs_begin(void)                          /* a backup is coming: patient now (5 s a read, 2 min in all) */
{
    app_panic();
    ota_try_ms = 5000u;
    ota_deadline = ota_now_ms() + 120000u;
}
static void rs_show(uint32_t pct);
#if FM1B_RES
#include "fm1_res.h"              /* FM1B_RES_LEN, FM1B_RES_CRC: the build's resources (build.py, from the file) */
#include "res.c"                  /* the resource region (tools/fm1_resources.py) */
#endif
static void rs_show(uint32_t pct)
{
    char b[8];
    uint32_t i = 0;
    if (pct >= 100u)
        b[i++] = '1';
    if (pct >= 10u)
        b[i++] = (char)('0' + pct / 10u % 10u);
    b[i++] = (char)('0' + pct % 10u);
    b[i++] = '%';
    b[i] = 0;
    if (!pct)
        lcd_fill(0, 0, 240, 240, C_BLACK);
    sys_box(0, 92, 240, SYS_S, "RESTORE", C_WHITE, 1);
    sys_box(0, 124, 240, SYS_S, b, C_HI, 1);
}
static int rs_ask(void)                             /* SEL yes, HOME no; playing or 60 s: no. The hand's buttons only */
{
    uint32_t t0 = fm1_ms, was = plat_buttons() & ~mirror_in_buttons() & ~remote.btn;
    rs_screen("RESTORE FROM USB?", "SEL yes   HOME no");
    sys_box(0, 156, 240, SYS_S, APP_BACKUP_WHAT, C_GRAY, 1);
    ann_say("Restore from USB? It " APP_BACKUP_WHAT ". SEL: yes. HOME: no.", 0, ANN_NOW | ANN_KEEP);
    while (fm1_ms - t0 < 60000u) {
        uint32_t b = plat_buttons() & ~mirror_in_buttons() & ~remote.btn, down = b & ~was;
        fm1_wdt_feed();
        was = b;
        if (down >> B_SEL & 1u)
            return 1;
        if ((down >> B_HOME & 1u) || rs_playing())
            return 0;
    }
    return 0;
}
static void rs_reload(void)
{
    app_panic();
    app_backup_restored();
}
static int rs_rec_erase(void) { return ota_erase(RS_REC); }
static int rs_rec_prog(uint32_t off, const void *src, uint32_t n) { return ota_prog(RS_REC + off, src, n); }
static int rs_rec_read(uint32_t off, void *dst, uint32_t n) { return ota_fread(RS_REC + off, dst, n); }
static int bu_room(void)                            /* the SysEx ring empty: a frame goes in whole; -1: USB gone */
{
    if (!usb.config || usb.suspended)
        return -1;
    return so_w == so_r;
}
static int bu_send(const uint8_t *p, uint32_t n) { return ota_wire_send(p, n); }
static void bu_flush(void) { app_backup_flush(); }
static void bu_hold(int on) { app_backup_hold(on); }
#endif
#ifdef APP_TOUCH
#include "touch.c"
#endif
#ifdef APP_BTDEV
#include "btdev.c"                /* Bluetooth MIDI devices for an app's page (lib/btdev.h) */
#endif
#include "app.c"
#include "main_fm1.c"
