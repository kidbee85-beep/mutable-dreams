#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build an fm1-base firmware: the app, the update loader (felucca-bsp's) and an installable .fwsc package.

  tools/build.py APP_DIR [--release X.Y[.Z][-suffix]] [--no-ble]
  tools/build.py APP_DIR --host        the host simulator instead: APP_DIR/build/host/NAME_host (cc, no toolchain)
  tools/build.py APP_DIR --web         the browser emulator instead: APP_DIR/build/emu/ (emcc); the page is the
                                       simulator alone (panel, screen, sound, screen reader), for a site to link or frame;
                                       an app may bring its own (APP_DIR/web/emu/index.html, worklet.js)
  tools/build.py APP_DIR --rfm1        the app as an .rfm1 instead: APP_DIR/build/rfm1/ID-dev+COMMIT.rfm1 (emcc), the
                                       host simulator in WebAssembly for host apps (RemoteFM1 for iOS: RemoteFM1IOS's
                                       docs/RFM1-FORMAT.md); host/rfm1.c, tools/rfm1_pack.py. With --release X.Y:
                                       APP_DIR/build/NAME-X.Y.rfm1, beside the release's package

APP_DIR holds the app (app.h, app.c, its sources) and its fm1.toml:
  name = "TEMPLATE"          the build's file names (lower case)
  id_prefix = "FM-1_0"       package identity: dev builds are the prefix + "00", releases the prefix + XXYYZZ
  usb_manufacturer = "..."   the USB names (felucca-bsp FELUCCA_USB_MANUFACTURER / FELUCCA_USB_PRODUCT)
  usb_product = "..."
  o2 = ["dsp/engine.c"]      separate units compiled at -O2 (the app's DSP), relative to APP_DIR (globs too); each with
                             its own directory on the include path
  unity_opt = "z"            the unity build (the platform, the app but its o2 units) at -Oz instead of -Os: smaller,
                             a little slower (what runs in the audio interrupt outside the o2 units, too)
  o2_cflags = ["-DX=1"]      more flags for those; o2_include = ["lib/prefix.h"]: a header included first in each
  include = ["dsp/lib"]      more include directories, relative to APP_DIR
  device_include = ["inc"]   include directories for the firmware only (headers the toolchain lacks)
  mmio_skip = ["dsp/lib/"]   paths whose numbers are not addresses (a library's tables): the register check skips
                             its address literals there (still checks casts, asm and register macros)
  rom_check_skip = ["pv_log2"]   functions whose constants only look like ROM addresses (0xFFC00000..): the check
                             that nothing calls or loads the chip ROM skips them
  mul64_check_skip = ["mul_f256"]   functions the note on inlined 64-bit multiplies beside loads leaves out
                             (felucca-bsp docs/CPU.md; the compiler runtime's __* are left out anyway)
  web_cflags = ["-sX=1"]     more emcc flags for the emulator, compiling and linking (fm1-ripple: Lua's setjmp in
                             WebAssembly itself, -sSUPPORT_LONGJMP=wasm)
  web = ["build/web/*.c"]    units for the browser's emulator only (relative to APP_DIR, globs too): what the device
                             loads at run time, linked in (fm1-ripple's currents)
  web_link = ["build/web/*.a"]   archives linked into the browser's emulator as they are (relative to APP_DIR, globs
                             too): what a firmware builds for it itself (fm1-ripple: its C++ currents, by emcc)
  vendored = ["third_party/"]   other people's libraries, compiled for the FM-1 with their machine-specific code off
                             by flags: the register check skips them entirely (it reads the text: x86 asm behind
                             an #if would fail it)
  rfm1_memory_mib = 8        the .rfm1's fixed memory (MiB, 1..32; default 8)
  authors = ["Name"]         the .rfm1 manifest's authors (default: author, below, else the app repository's first
                             commit's author)
  words = "words.tsv"        the screen reader's words (fm1-reader), relative to APP_DIR
  font_full = ["L"]          font roles with lower case too (by default M and L have capitals and digits only)
  font_trim = true           each glyph stored as its lit box only (tools/gen_font.py FM1_FONT_TRIM): the same
                             pixels drawn, the fonts about 55 % smaller in flash
  font_pack = true           (with font_trim) each glyph's alphas Huffman-coded too (FM1_FONT_PACK), decoded as
                             device/gfx.c draws: the same pixels, Barlow's faces about a third smaller again; text
                             then costs about 3x the instructions, which app.h's APP_FONT_CACHE gives back: that many
                             bytes of RAM (e.g. 6144u, at least the header's FONT_BOX_MAX) keep the glyphs drawn lately
                             decoded, APP_FONT_CACHE_BITS (default 7) the log2 of its slots, 6 B each
  ble_stack_kb = 6           the Bluetooth tasks' stacks, KB each (default 8: three of them, in the pool; radio.c
                             logs each one's deepest, "stacks", 10 s after Bluetooth is ready and as they grow)
  ble_heap_kb = 16           its heap, KB (default 24, in RAM; the same log line's "heap": its most ever)
  ble_trim = true            Bluetooth as a MIDI peripheral only: JieLi's central, classic and test code cut (flash and
                             RAM: Melodee's measure ~18 KB and ~4 KB); not with ble_central
  ble_no_pairing = true      with ble_trim: no pairing or encryption either (~19 KB more flash; iOS and macOS connect)
  ble_central = true         and the FM-1 connects to Bluetooth MIDI devices too (FM1B_BLE_CENTRAL), beside a computer;
                             with ble_stack = "own", 3: without pairing (no bonds, no passkey: smaller)
  bt_audio_mem = "app"       with bt_audio: its audio packets' queue and the receptions' ring (17.7 KB) lent by the app
                             while an AUDIO mode is on, not static RAM (FM1B_BTA_MEM; plat.h plat_bta_mem): an app
                             whose arena gets them back while Bluetooth audio is off. Default "static"
  bt_audio_sim = true        with bt_audio: the host simulator and the browser emulator built with FM1B_BT_AUDIO, on
                             fm1-base's stand-in for plat_bta_* (the script's "bta add NAME"); off by default, so an
                             app keeps its own stand-in there (without FM1B_BT_AUDIO)
  usb_audio_in = true        the FM-1 recordable over USB (FM1B_UAC; the default: true)
  usb_audio_out = true       a computer plays through the FM-1 over USB (FM1B_UAC_OUT; the default: true)
  usb_audio_channels = 10    the USB audio input's channels, 4, 6, 8 or 10 (the default 2: the master alone): the master on 1 and
                             2, the app's own on 3..N (FM1B_UAC_CH: plat_usb_ch in plat.h; felucca-bsp FELUCCA_UAC_CH).
                             44.1 kHz only; the ring takes 2 N bytes a frame (768 frames with FM1B_UAC_DEEP: 15 KB at 10)
  usb_audio_first = true     the USB audio streaming interfaces listed first in the audio control collection
                             (felucca-bsp FELUCCA_UAC_AS_FIRST, from Felucca 1.5.1): macOS then shows the FM-1 as an audio
                             device beside its MIDI; bcdDevice +0.08 (a changed device to the computer). Not enough
                             for macOS 13-15 (fm1-loom#7, Felucca #67): usb_midi_last is; with it this changes nothing
  usb_midi_last = false      the USB MIDI interface after the audio streaming ones (felucca-bsp FELUCCA_UAC_MIDI_LAST;
                             the default: true, with usb_audio_in): audio control, audio in, audio out, MIDI, and the
                             audio control collection in that order. macOS 13-15 show no USB audio device while MIDI is
                             interface 1, between audio control and audio streaming (bisected by darosh on macOS 15.8.1);
                             bcdDevice +0.80. false: the order of fm1-base before 2026-10-31 (MIDI interface 1).
                             2: MIDI left out of the collection too (a test build: no USB MIDI on Linux)
  usb_iad = true             without the USB console, an interface association descriptor over the audio function, the
                             device then misc / IAD (felucca-bsp FELUCCA_UAC_IAD; the default: false); bcdDevice +2.00
  usb_console = true         a USB serial console beside USB MIDI (FM1B_CDC: felucca-bsp's CDC-ACM function, device/
                             console.c): the app's text over plat_console_* (plat.h); FM1B_CDC=0 / 1 in the
                             environment over it
  midi_stamp = true          MIDI in from USB and the TRS jack stamped where it lands (FM1B_MIDI_STAMP: felucca-bsp's
                             FELUCCA_MIDI_STAMP, usb_midi_peek and midi_uart.c's uart_midi_peek from TIMER5's 10 kHz
                             tick, the render's ticks too): plat_midi_in_t24 within 0.05 ms of the arrival instead of
                             when the app read it; off by default; FM1B_MIDI_STAMP=0 / 1 in the environment over it
  crash_history = true       the crash records kept in flash across power-offs (FM1B_CRASHLOG: device/crashlog.c, the
                             update's staging area's 0xE2000..0xE3FFF, which an install erases; read with F0 7D 4C 48 F7,
                             tools/fm1_remote.py crashes), and each stack's room at a crash (stacks.c: plat_stack_left;
                             the stacks painted at every start); off by default
  debug_stats = true         and the debug stats (FM1B_DBGSTATS: device/dbgstats.c, a snapshot every 5 minutes while the
                             app has plat_stats_enable on, and one at each crash, in the staging's last sector
                             0xE4000..0xE4EFF; F0 7D 4C 53 F7, fm1_remote.py stats); with the crash history
  usb_host = true            the USB port as a MIDI host when the app asks (FM1B_USB_HOST: felucca-bsp's
                             FELUCCA_USB_HOST, docs/USB_HOST.md; plat_usb_host in plat.h): a USB-MIDI controller or
                             synth, directly or behind one powered hub (the FM-1 gives no power). Boot is a device
  mirror = true              the screen and panel mirrored over USB (FM1B_MIRROR: device/mirror.c; web/mirror/,
                             tools/fm1_mirror.py), the panel played from the host; FM1B_MIRROR=1 in the environment
                             builds it in whatever fm1.toml says (an app's demo build), FM1B_POOL_SPARE=N the
                             spare the build insists on (8192 B of the pool; of the SRAM with ram_layout =
                             "packed"; 2048 B of the pool with WiFi; a demo build may spend it)
  pool_spare = 2048          the spare this app's builds insist on (8192 B, of the SRAM with ram_layout = "packed": the
                             margin for what comes next; an app that spent it on purpose says how much it keeps)
  msc = true                 disk mode: the FM-1 as a USB drive of the app's blocks (FM1B_MSC: device/msc.c, plat_disk_* in
                             plat.h; felucca-bsp usb.c FELUCCA_MSC), no USB MIDI or audio meanwhile
  lcd_panel = true           the M-VAVE panel's own ST7789V setup after the reset (FM1B_LCD_PANEL: porch, gate voltages, VCOM
                             0x18, supplies, 60 Hz, gammas; device/lcd.c LCD_SEQ). Off: the init is byte-identical
  backup = true              backup and restore of the app's saved objects over USB (FM1B_BACKUP: device/backup.c;
                             tools/fm1_backup.py); app.h gives APP_BACKUP_OBJS, _MAGIC, _VERSION and the app_backup_* hooks
  resources = "FILE"         the resource region (FM1B_RES: device/res.c, plat_res in plat.h; with backup): FILE (relative
                             to APP_DIR, {gen} the generated headers' directory: an app's gen step may make it) kept out
                             of the image in app.h's APP_RES_BASE, APP_RES_SECTORS, written by the host after an update
                             (tools/fm1_resources.py, fm1_install.py and the installer page: build/NAME-res.bin beside
                             the package); the host and web builds have it compiled in. resources_what (and
                             resources_what_ja): what it holds, in the installer's words ("the 909 cymbals")
  cpu2 = true                the second core (FM1B_CPU2: device/cpu2.c, plat_cpu2_* in plat.h, on felucca-bsp's
                             chip/fm1_cpu1.*): a worker for a part of the render. A failure of it (a misread, a
                             fault, a late job) restarts the FM-1, the reason in the crash record (Melodee's releases)
  cpu2_fallback = true       instead, one core after a failure, until power-off (FM1_CPU1_FALLBACK); FM1B_BUILD, a
                             hash of the sources, lifts that bar for a new build
  cpu2_stack = 4096          its jobs' stack, bytes (FM1_CPU1_USTACK_BYTES: default 2816, the lowest 256 a guard the
                             core's stack limit traps in), a multiple of 8, 1024..16384: an app whose jobs go deeper
                             (plat_cpu2_stack reads how deep they went)
  cache_ram_ways = 2         2, 4 or 7 of the data cache's 8 ways of 4 KiB used as RAM at 0x01F28000 (FM1B_CACHE_RAM:
                             felucca-bsp's chip/fm1_cache_ram.h, plat_cache_ram in plat.h): set and tested at the start,
                             before anything uses them; a failed test puts the cache back and gives 0 bytes. The ways
                             kept are the flash's data cache: fewer of them make big flash reads slower (7 as RAM: 57x
                             over 16 KiB on an FM-1; 2 in daily use there). The host: a buffer of that size stands in.
                             An app's own objects in section .cache_ram come first (the build checks they fit)
  ram_layout = "packed"      one SRAM budget (device/app_packed.ld): the RAM code, .data/.bss and the pool packed from
                             the SRAM's bottom up to the stacks, 8 KiB kept spare, the rest free above _pool_end (the
                             default, "fixed", is device/app.ld: RAM code 32 KiB, .data/.bss 96 KiB, pool 336 KiB, each
                             region checked on its own). Not with wifi_stack = "jieli" (app_wifi.ld's fixed map)
  main_stack = 9216          the main loop's stack (crt0's sp), bytes above its 256-byte guard band, a multiple of 256,
  irq_stack = 6400           2048..24320 (default 24320); the interrupts' (ssp), the same, 2048..7936 (default 7936):
                             device/app_packed.ld's SRAM ends that much lower or higher (its free RAM, an app's arena),
                             the reset-kept record and the stacks' tops stay. Only with ram_layout = "packed"
  stack_check = true         the stacks' static worst (tools/stack_depth.py: the deepest call path's frames, the
                             interrupts nested at their worst, the fault path on top) checked against those: a stack
                             whose worst is over its size fails the build; one within it but under its margin (25 % or
                             1 KiB, the larger) is said. Each unit's optimised IR is written beside its code
                             (OUT/*.opt.ll: the indirect calls' targets, stack_ir.py). stack_bounds: an app's
                             function that calls itself at most N levels deep (its source's own bound): ["f=N", ..]
  emu = ["midi", "data"]     the browser emulator's optional parts (--web): web/emu/parts/NAME.js on the page and
                             NAME_worklet.js in the worklet, each file saying what it does; a part's own needs come
                             with it; FM1B_EMU_NAME=1 for the C. "resample" (the device's 44.1 kHz to the browser's
                             rate) is on without asking, where the page takes parts (fm1-base's page and worklet, or
                             an app's own that load app_worklet.js and call fm1App); "-resample" leaves it out.
                             FM1B_EMU="midi data" in the environment instead: those parts and no others ("" for
                             none). Without any, the page, the worklet and app.wasm are what they were
  xdata2 = true              a fourth plat_data window, PLAT_XDATA2 0xFC000..0xFEFFF (FM1B_XDATA2: plat.h; Felucca's
                             globals' sectors, for an app that was a Felucca firmware); the store's objects there are
                             refused to plat_data_erase / plat_data_write
  headphones = true          the headphone jack read every main-loop pass, debounced (FM1B_HEADPHONES: device/plat_hp.c,
                             felucca-bsp's boards/fm1/fm1_jack.h, PB5): plat_headphones 1 / 0; without it -1 on the
                             device, nothing read (the host simulator's script jack in / jack out either way)
  audio_aside = true         each audio half rendered into a buffer of its own and copied into the DMA's half only
                             while that half is still free (FM1B_AUDIO_ASIDE, device/audio_commit.c, Henri David's): a
                             late render leaves the half playing whole instead of tearing it. 2 KiB of RAM; off by default
Optional parts: only what an fm1.toml asks for is built in (Bluetooth, USB audio; an app's lib/touch by APP_TOUCH in
app.h); the build prints what it has.
  boot_pack = true           .ram_text and .data stored LZMA-packed (tools/boot_pack.py, Melodee's; device/boot_lzma.h): the
                             boot unpacks them, the decoder's tables in the pool before it is cleared (about 9 KB of
                             flash back on a large app)
  ble_stack = "own"          with ble: Optimist's own Bluetooth stack (bt/: Henri David's, docs/bt/BLE-STACK.md;
                             FM1B_BLE_OWN) instead of JieLi's libraries: nothing of the SDK's linked, no shim
  ble_pairing = true         with ble_stack = "own": pairing and an encrypted link (LE legacy pairing, Just Works),
                             the bond kept in OBJ_BLE; without it a pairing is refused (macOS and iOS connect anyway)
  ble_diag = false           with ble_stack = "own": the link layer's diagnostics left out (bt/ble/ble_diag.h BLE_DIAG 0:
                             its counters and event rings, ~2.4 KB of RAM, and the radio log's "BT ll" line); kept by default
  ble_midi_time = true       with ble_stack = "own": Bluetooth MIDI in by the sender's time (FM1B_BLE_MIDI_TIME): each
                             message's time sent, from its BLE-MIDI timestamp (bt/ble/ble_time.c), for plat_midi_in_t24;
                             the transport then comes at once instead of held to the sender's time
  ble = true                 Bluetooth MIDI (FM1B_BLE): ble/ on JieLi's BT libraries, from the AC79 SDK ($AC79_SDK);
                             --no-ble builds without it (NAME-usb.fwsc), and the build checks nothing of JieLi's is in
  wifi = true                WiFi (FM1B_WIFI): a station on JieLi's WiFi libraries and the SDK's lwIP (built from its
                             sources), on the same shim (ble/shim, ble/wifi) with fm1-radio's RAM split (device/
                             app_wifi.ld). Not with ble (yet)
  net = ["net/stream.c"]     with wifi: the app's network code (lwIP's sockets), compiled as the SDK builds code that
                             talks lwIP (its flags and headers), relative to APP_DIR (globs too: "tls/src/**/*.c"); it
                             calls the platform through plat.h, runs in a task of its own (plat_net_task)
  net_include = ["tls/inc"]  more include directories for those, relative to APP_DIR
  net_cflags = ["-DX=0"]     more flags for those
  gen = ["tools/places.py --out {gen}/places.h"]   the app's generators, run before the build (relative to APP_DIR;
                             {gen} is the generated headers' directory, on the include path)
  asm = ["cpu1/entry.S"]     the app's own assembly units (relative to APP_DIR), assembled as felucca-bsp's are; an
                             app's hardware code outside the BSP (its own second core, say) is the app's to keep
                             apart (vendored, so the register check leaves it)
  ir_pass = "tools/size_fns.py"   a pass over the LLVM IR of each unit before its code is made (relative to APP_DIR):
                             the build compiles unity.c and each o2 unit to IR without the optimiser, runs
                             `ir_pass IN.ll OUT.ll [UNIT]` (UNIT: an o2 unit's path relative to APP_DIR; none for
                             unity.c), then compiles OUT.ll at the unit's level (an app marking its cold code
                             minsize: Melodee's idea). Without changes the round trip makes the same code
The app's own code that runs from RAM (out of the XIP cache's way: an audio path) goes in section .app_ram (plat.h
PLAT_RAM_CODE): copied at boot with .ram_text, inside the guard's RAM code window (between _ar_start and _ar_end). It
may call only RAM code, to any depth, and nothing through a pointer (tools/ramcode.py, over the disassembly). XIP code
calls it through a pointer (PLAT_FAR: a direct call does not reach); a call from it into XIP, direct or through a
pointer, the check refuses, unless:
  ram_code_allow = ["fn"]    the app's RAM functions whose calls out of RAM or through a pointer it vouches for (a cold
                             path; a pointer that only holds RAM code); printed by every build
The second core's code that runs from RAM goes in section .c1_text: in .ram_text (copied at boot, inside the guard's
RAM code window), between _c1_start and _c1_end; the check that RAM code calls nothing in the flash leaves it out
(core 1 runs only while core 0 waits for it in the audio interrupt, and core 0 turns the flash off only with
interrupts off).
The firmware described for firmware hubs (firmware.json, docs/FIRMWARE-JSON.md; tools/firmware_json.py): these keys, and
the site's (tools/make_site.py). A key left out is left out of the file.
  tagline = "..."            a line: what the firmware is ("A MIDI controller for the FM-1")
  repo = "https://..."       its source
  licence = "MPL-2.0"        its licence, an SPDX expression
  credits = "..."            a sentence: what it is built from (the installer's footer too)
  author = "Name"            who made it (else authors, above, joined)
  description = "..."        one paragraph, at most 600 characters
  tags = ["synth", "fm"]     what it is, from a fixed list (tools/firmware_json.py TAGS; the build refuses another)
  install = "https://..."    the page that installs it
  site = "https://..."       its site
  title = "..."              the name people see, when it is not usb_manufacturer (Radio's is fm1-radio)
  screenshot = "docs/img/x.png"  a picture for the hubs' cards: a square PNG, at least 240 x 240, at most 2 MiB,
                             relative to the repository's root (the parent of an APP_DIR named app), / between
                             folders, no ".."; the build checks it (a host simulator's screenshot fits as it is)
The name is title (else usb_manufacturer, else app.h's APP_NAME), the id fm1.toml's name in lower case; the hardware's features
are the parts this build has (printed as "parts"). A --release build of the firmware refuses without author (or
authors), licence, tagline, description, tags, install, repo and a screenshot that passes the checks; any other
build warns and writes the file without them.
Outputs in APP_DIR/build/: NAME.bin (app), loader/ota.bin (update loader), NAME.fwsc (package), firmware.json
(firmware-usb.json from --no-ble; every build writes it, --host, --web and --rfm1 too). An app in a firmware's
repository (APP_DIR named app) also has the repository's firmware.json written, when it changes.

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import functools
import hashlib
import os
import platform
import re
import shlex
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]        # fm1-base
BSP = SRC / "bsp"                                 # felucca-bsp (git submodule)
APP = OUT = GEN = LDR = ROOT = None               # set by main() from APP_DIR
CONF = {}
LAYOUT = {}                                       # the linked script's MEMORY regions: {name: (origin, length)}
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import boot_pack  # noqa: E402
import firmware_json  # noqa: E402
import lz4blk  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
# the AC79's single-precision FPU (the SDK's own flags); no fused multiply-add, so the host
# build (-ffp-contract=off) computes the same samples
CPU = "r3"                          # the core: the compiler, the LTO plugin and objdump (to decode its FPU instructions)
FPU = [f"-mcpu={CPU}", "-mfprev1", "-ffp-contract=off"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

PRODUCT = None                      # package identity (fm1.toml id_prefix; see main)
VERSION = None                      # APP_VERSION for release builds (default: "DEV", device/unity.c)
RELEASE = None                      # --release X.Y as given: the release's file names (NAME-X.Y.fwsc, NAME-X.Y.rfm1)
BLE = False                         # fm1.toml ble = true, unless --no-ble
OWN = False                         # with BLE: fm1.toml ble_stack = "own" (bt/, no JieLi library)
WIFI = False                        # fm1.toml wifi = true
CPU2 = False                        # fm1.toml cpu2 = true (FM1B_CPU2=0 / 1 in the environment: without / with)
STEM = None                         # the build's file names: NAME, or NAME-usb (--no-ble)
BT_SYMS = ("btstack_init", "bt_ble_init", "bt_max_pwr_set", "le_controller_set_mac", "lmp_set_sniff_disable",
           "shim_os_init", "ble_midi_send_many")


def read_conf(path):
    """fm1.toml: key = "string" | number | true | false | ["string", ...] (a list may go on over several lines),
    # comments (the little TOML a firmware needs)"""
    conf = {}
    pending, start = "", 0
    for no, line in enumerate(path.read_text().splitlines(), 1):
        line = re.sub(r"\s+#.*$", "", line).strip()
        if not line or line.startswith("#"):
            continue
        if pending:                                  # a list's next line
            line, no = pending + " " + line, start
        if re.match(r"[A-Za-z_][A-Za-z0-9_]*\s*=\s*\[", line) and "]" not in line:
            pending, start = line, no
            continue
        pending = ""
        m = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+)", line)
        if not m:
            raise SystemExit(f"{path}:{no}: not key = value")
        v = m.group(2)
        if v.startswith("["):
            conf[m.group(1)] = re.findall(r'"([^"]*)"', v)
        elif v.startswith('"'):
            conf[m.group(1)] = v.strip('"')
        elif v in ("true", "false"):
            conf[m.group(1)] = v == "true"
        else:
            conf[m.group(1)] = int(v, 0)
    if pending:
        raise SystemExit(f"{path}:{start}: a list without its ]")
    return conf


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


_BUILDER = None
_BUILDER_LOCK = __import__("threading").Lock()


def builder():
    """one long-lived toolchain container per source tree (starting a container per tool call cost
    most of the build); it stops by itself after an hour without a build"""
    global _BUILDER
    with _BUILDER_LOCK:            # the loader and the app build in parallel: start it once
        return _builder_start()


def _builder_start():
    global _BUILDER
    if _BUILDER:
        return _BUILDER
    name = "jieli-" + hashlib.sha1(str(ROOT).encode()).hexdigest()[:10]
    st = subprocess.run(["docker", "inspect", "-f", "{{.State.Running}}", name], capture_output=True, text=True)
    if st.stdout.strip() != "true":
        subprocess.run(["docker", "rm", "-f", name], capture_output=True)
        r = subprocess.run(["docker", "run", "-d", "--rm", "--name", name, "--platform", "linux/amd64",
                            "-v", f"{ROOT}:/work", "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work",
                            DOCKER_IMAGE, "sh", "-c", "sleep 3600"], capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit("build: cannot start the toolchain container")
    else:                           # an hour from now again
        subprocess.run(["docker", "exec", "-d", name, "sh", "-c", "sleep 3600 && kill 1"], capture_output=True)
    _BUILDER = name
    return name


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd ROOT; paths relative to ROOT"""
    rel = [str(Path(a).resolve().relative_to(ROOT)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-no-canonical-prefixes", "-target", "pi32v2", *rel]
    if os.environ.get("JIELI_RUNNER"):
        cmd = [*shlex.split(os.environ["JIELI_RUNNER"]), str(toolchain() / tool), *rel]
    elif use_docker():
        cmd = ["docker", "exec", "-w", "/work", builder(), f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    # at most 4 at once: a podman machine drops concurrent container connections (ssh EOF)
    with ThreadPoolExecutor(min(len(cmds), int(os.environ.get("FM1B_JOBS", "4")))) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    """generated headers: the fonts, the screen reader's words link (fm1-reader)"""
    GEN.mkdir(parents=True, exist_ok=True)
    tools = SRC / "tools"
    for c in [[tools / "gen_font.py", GEN / "felucca_font.h"],
              [tools / "gen_words.py", APP / CONF.get("words", "words.tsv"), APP / "web" / "words", GEN / "ann_words.h"]]:
        env = {**os.environ, "X0X_FONTSET": CONF.get("font_set", "barlow"),
               "FM1_FONT_FULL": " ".join(CONF.get("font_full", [])),
               "FM1_FONT_TRIM": "1" if CONF.get("font_trim") else "",
               "FM1_FONT_PACK": "1" if CONF.get("font_pack") else ""}
        r = subprocess.run([sys.executable, *map(str, c)], capture_output=True, text=True, env=env)
        sys.stdout.write(r.stdout)
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit(f"build: {c[0].name} failed")
    for line in CONF.get("gen", []):          # the app's own generators: a script (relative to APP_DIR) and its arguments
        c = [a.replace("{gen}", str(GEN)) for a in line.split()]
        r = subprocess.run([sys.executable, str(APP / c[0]), *c[1:]], cwd=APP, capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode:
            sys.stderr.write(r.stderr)
            raise SystemExit(f"build: {c[0]} failed")
    if CONF.get("resources"):
        gen_resources()


RES_MAGIC = 0x31534552                        # "RES1" (device/res.c)


def res_payload():
    """the resource region's payload: fm1.toml resources = FILE"""
    f = APP / CONF["resources"].replace("{gen}", str(GEN))
    if not f.is_file():
        raise SystemExit(f"build: fm1.toml resources {CONF['resources']!r}: no such file ({f})")
    return f.read_bytes()


def res_file(payload):
    """NAME-res.bin: the region as the FM-1 holds it, its header (the commit record) then the payload"""
    import zlib
    crc = zlib.crc32(payload)
    head = struct.pack("<7I", RES_MAGIC, len(payload), crc, 0, 0, 0, 0)
    return head + struct.pack("<I", zlib.crc32(head)) + payload


def gen_resources():
    """fm1_res.h (the payload's length and CRC-32: device/res.c checks the region against them) and fm1_res_data.h
    (the payload itself, for the host and web builds)"""
    import zlib
    if not CONF.get("backup"):
        raise SystemExit("build: fm1.toml resources needs backup = true (the transfer is the restore's)")
    p = res_payload()
    if not p:
        raise SystemExit("build: fm1.toml resources: an empty file")
    (GEN / "fm1_res.h").write_text("/* generated by tools/build.py from fm1.toml resources: device/res.c */\n#pragma once\n"
                                   f"#define FM1B_RES_LEN {len(p)}u\n#define FM1B_RES_CRC {zlib.crc32(p):#010x}u\n")
    rows = ",\n".join(",".join(str(b) for b in p[i:i + 32]) for i in range(0, len(p), 32))
    (GEN / "fm1_res_data.h").write_text("/* generated by tools/build.py from fm1.toml resources: host/plat_host.c */\n"
                                        "#pragma once\nstatic const uint8_t fm1b_res_data[] = {\n" + rows + "};\n")


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = BSP / "loader"
    flags = [*CFLAGS, *inc(BSP / "chip", BSP / "boards" / "fm1")]
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", f"-mcpu={CPU}", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B")
    return ota


# ---- app

def inc(*dirs):
    return [f"-I{Path(d).resolve().relative_to(ROOT)}" for d in dirs]


# ---- Bluetooth: JieLi's BT libraries (Apache-2.0, binary only; never in git) and a few SDK sources, on the shim
# (ble/shim) and the MIDI service (ble/midi)
BLE_LIBS = ["btctrler.a", "btstack.a", "crypto_toolbox_Osize.a", "lib_ccm_aes.a", "wl_rf_common.a"]
BLE_SDK_SRC = ["apps/common/config/bt_profile_config.c",
               "apps/common/config/log_config/lib_btctrler_config.c",
               "apps/common/config/log_config/lib_btstack_config.c"]
BLE_INC = ["include_lib", "include_lib/driver", "include_lib/driver/device", "include_lib/driver/cpu/wl82",
           "include_lib/system", "include_lib/system/generic", "include_lib/btctrler",
           "include_lib/btctrler/port/wl82", "include_lib/update", "include_lib/btstack/third_party/common",
           "include_lib/btstack/third_party/rcsp", "include_lib/utils", "include_lib/utils/syscfg",
           "include_lib/utils/event", "include_lib/media", "include_lib/media/cpu/wl82", "apps",
           "apps/demo/demo_ble/include", "apps/common", "apps/common/include", "apps/common/config/include",
           "include_lib/btstack", "include_lib/net", "apps/common/ble/include", "include_lib/newlib/include"]
# ble_trim = true: what Bluetooth MIDI as a peripheral never runs, cut from JieLi's libraries (after Melodee's
# ble-spike). They are LLVM 4.0 bitcode linked with LTO, so a function's body replaced (the toolchain's llvm-link
# -override) lets the optimizer drop everything only it reached ({(library, member): the replacements' C source}); and
# the SDK's library configuration (compiled here as LTO bitcode: its constants fold into JieLi's code) cut to LE only,
# one link as a peripheral. Not with ble_central: a central needs the scan, connecting and the GATT client
BLE_TRIM = {
    ("btctrler.a", "bredr_link.c.o"): "int link_conn_tx_bulk_avaiable(void) { return 0; }",     # classic's ACL transmit
    ("btstack.a", "ancs_client.c.o"): "void ancs_client_btstack_init_for_check(void) {}",       # Apple's notifications
    ("btctrler.a", "ll_scan.c.o"): "int ll_scan_open(void) { return -1; }",                     # the central's scan
    ("btstack.a", "gatt_client.c.o"):                                       # the GATT client's reads and writes (the
        "int gatt_client_read_value_of_characteristic_using_value_handle(void) { return 1; }\n"   # server's
        "int gatt_client_read_long_value_of_characteristic_using_value_handle(void) { return 1; }\n"  # notifications
        "int gatt_client_write_value_of_characteristic_extend(void) { return 1; }\n"               # stay)
        "int gatt_client_write_value_of_characteristic_without_response(void) { return 1; }\n"
        "int gatt_client_discover_characteristic_descriptors_by_handle(void) { return 1; }",
}
# Not, as Melodee's spike had them (2026-10-25, on an FM-1): avctp_user.c.o's classic connection bookkeeping
# (stubbed, the controller never says it is ready: "still starting after 3 s", no Bluetooth), nor link_layer.c.o's
# ll_create_conn_link (a peripheral makes its link with it too: stubbed, a Mac's connecting crashed the FM-1 in
# ble_hw_tx, r0 = 0, from ble_event_irq_handler)
BLE_CONFIG = {"apps/common/config/log_config/lib_btctrler_config.c": [
    (r"(const int config_btctler_le_roles\s*=\s*)\([^)]*\)", r"\1(LE_ADV | LE_SLAVE)"),
    (r"(const int config_le_gatt_client_num\s*=\s*)[^;]+", r"\g<1>0"),
    (r"(const int CONFIG_TEST_DUT_CODE\s*=\s*)1", r"\g<1>0"),
    (r"(const int CONFIG_TEST_FCC_CODE\s*=\s*)1", r"\g<1>0"),
    (r"(const int CONFIG_LMP_NAME_REQ_ENABLE\s*=\s*)1", r"\g<1>0"),
    (r"(const int CONFIG_LMP_PASSKEY_ENABLE\s*=\s*)1", r"\g<1>0"),
    (r"(const int CONFIG_LMP_MASTER_ESCO_ENABLE\s*=\s*)1", r"\g<1>0"),
    (r"(const int CONFIG_BREDR_INQUIRY\s*=\s*)1", r"\g<1>0"),
    (r"(const int config_btctler_dual_a2dp\s*=\s*)1", r"\g<1>0"),
    (r"(const int config_btctler_le_master_multilink\s*=\s*)1", r"\g<1>0"),
    (r"(const int config_multilink_support_esco_enalbe\s*=\s*)1", r"\g<1>0")]}
# ble_no_pairing = true (with ble_trim): no pairing or encryption either (the security manager). Bluetooth MIDI needs
# neither: an iPhone and a Mac connect, MIDI both ways (LOOM-8, 2026-10-25)
BLE_CONFIG_NO_SM = {"apps/common/config/log_config/lib_btctrler_config.c": [
    (r"(const int config_le_sm_support_enable\s*=\s*)1", r"\g<1>0"),
    (r"(const uint64_t config_btctler_le_features\s*=\s*)\(LE_ENCRYPTION\)", r"\g<1>(0)")]}


def ble_config(sdk, dst):
    """the SDK's configuration sources into dst, as BLE_CONFIG cuts them (ble_trim); written only when changed"""
    import re as _re
    import shutil
    cuts = {}
    if BLE and CONF.get("ble_trim"):
        for d in (BLE_CONFIG, BLE_CONFIG_NO_SM if CONF.get("ble_no_pairing") else {}):
            for f, rs in d.items():
                cuts.setdefault(f, []).extend(rs)
    for f in BLE_SDK_SRC:
        text = (sdk / f).read_text(errors="ignore")
        for rx, rep in cuts.get(f, []):
            text, n = _re.subn(rx, rep, text)
            if not n:
                raise SystemExit(f"build: {f} in the SDK has changed: no {rx!r}")
        if not (dst / f).exists() or (dst / f).read_text(errors="ignore") != text:
            (dst / f).parent.mkdir(parents=True, exist_ok=True)
            (dst / f).write_text(text)


def ble_trim(dst):
    """OUT/ble/trim/LIB: the libraries with BLE_TRIM's functions replaced (cached by their stubs); {LIB: path}"""
    import shutil
    trim = OUT / "ble" / "trim"
    pick = os.environ.get("FM1B_STUBS")           # (a bisection: only these members' stubs, e.g. "ll_scan,gatt_client")
    items = {k: v for k, v in BLE_TRIM.items() if not pick or k[1].replace(".c.o", "") in pick.split(",")}
    key = repr(sorted(items.items()))
    libs = {lib: trim / lib for lib, _ in items}
    if (trim / "key").exists() and (trim / "key").read_text() == key:
        return libs
    if trim.exists():
        shutil.rmtree(trim)
    trim.mkdir(parents=True)
    for (lib, member), src in items.items():
        if not (trim / lib).exists():
            shutil.copy(dst / "lib" / lib, trim / lib)
        w = trim / member.replace(".c.o", "")
        w.mkdir()
        (w / "stub.c").write_text(src + "\n")
        tc("common/bin/llvm-ar", "x", trim / lib, member)         # (into ROOT, the tools' cwd: moved below)
        shutil.move(ROOT / member, w / member)
        tc("cc", "-mcpu=r3", "-mfprev1", "-Oz", "-flto", "-c", w / "stub.c", "-o", w / "stub.bc")
        tc("common/bin/llvm-link", w / member, f"-override={(w / 'stub.bc').resolve().relative_to(ROOT)}",
           "-o", w / "out.bc")
        shutil.copy(w / "out.bc", w / member)
        tc("common/bin/llvm-ar", "r", trim / lib, w / member)
    (trim / "key").write_text(key)
    return libs


# WiFi: the SDK's no-SDRAM builds (demo_audio) link the _sfc variants: smaller buffers, code from flash (fm1-radio's)
WIFI_LIBS = ["wl_wifi_sta_sfc.a", "wpasupplicant.a", "hsm.a"]
RADIO_LIBS = ["crypto_toolbox_Osize.a", "lib_ccm_aes.a", "wl_rf_common.a"]   # (either radio's)
# lwIP from the SDK's sources (its _sfc project's files and flags), with ble/lwip/lwipopts_sfc.h: a wider TCP window
LWIP_DIRS = ["src/api", "src/core", "src/core/ipv4", "src/netif", "port", "apps/sock_api", "apps/dhcp_srv", "apps/ntp"]
LWIP_SKIP = {"bridgeif.c", "bridgeif_fdb.c", "lowpan6.c", "lowpan6_ble.c", "lowpan6_common.c", "slipif.c", "zepif.c",
             "bt_ethernetif.c", "lte_ethernetif.c", "wired_ethernetif.c", "ext_wireless_ethernetif.c",
             "wifi_raw_ethernetif.c", "ethernetif.c"}
LWIP_CF = ["-D_POSIX_SOURCE", "-D_SYS_SELECT_H", "-DUSE_RTOS", "-DLIWP_USE_SMALL_MEMORY", "-Wno-unused-value"]
LWIP_INC = ["include_lib/net/device", "include_lib/net/device/eth", "include_lib/net/lwip_2_1_3/lwip/app",
            "include_lib/net/lwip_2_1_3/lwip/app/sock_api", "include_lib/net/lwip_2_1_3/lwip/app/dhcp_srv",
            "include_lib/net/lwip_2_1_3/lwip/port/arch", "include_lib/net/lwip_2_1_3/lwip/src/include/lwip",
            "include_lib/system/device", "include_lib/system/os/FreeRTOS/pi32_lto"]
WIFI_INC = ["include_lib/net/wifi", "include_lib/net/lwip_2_1_3/lwip/src/include",
            "include_lib/net/lwip_2_1_3/lwip/src/include/lwip", "include_lib/net/lwip_2_1_3/lwip/src/include/netif",
            "include_lib/net/lwip_2_1_3/lwip/port", "include_lib/net/lwip_2_1_3/lwip/port/arch", "include_lib/net/HSM"]
BLE_CF = ["-integrated-as", f"-mcpu={CPU}", "-mfprev1", "-fno-common", "-Oz", "-flto", "-fallow-pointer-null",
          "-fprefer-gnu-section", "-femulated-tls", "-fms-extensions", "-w", "-DSUPPORT_MS_EXTENSIONS",
          "-DCONFIG_RELEASE_ENABLE", "-DCONFIG_CPU_WL82", "-DCONFIG_FREE_RTOS_ENABLE", "-DCONFIG_THREAD_ENABLE",
          "-D__GCC_PI32V2__", "-DCONFIG_NEW_ECC_ENABLE", "-DCONFIG_SFC_ENABLE", "-DCONFIG_NO_SDRAM_ENABLE",
          "-DCONFIG_BT_ENABLE=1", "-mllvm", "-pi32v2-large-program=true"]


def bta_sim():
    """fm1.toml bt_audio_sim = true (with bt_audio, ble_stack = "own"): the host simulator and the browser emulator build
    with FM1B_BT_AUDIO, fm1-base's stand-in for Bluetooth audio (host/plat_host.c: the script's devices). Off by default:
    an app with Bluetooth audio keeps its own stand-in there (LOOM-8's and Polly's bt.c: without FM1B_BT_AUDIO)"""
    return bool(BLE and OWN and CONF.get("bt_audio") and CONF.get("bt_audio_sim"))


def bta_mem_flags():
    """fm1.toml bt_audio_mem -> flags: "app" lends Bluetooth audio's buffers (plat.h plat_bta_mem), "static" (the default)"""
    v = CONF.get("bt_audio_mem", "static")
    if v not in ("app", "static"):
        raise SystemExit(f'build: fm1.toml bt_audio_mem = "{v}": "app" or "static"')
    return ["-DFM1B_BTA_MEM=1"] if v == "app" and CONF.get("bt_audio") else []


def radio_units():
    """copy what the Bluetooth or WiFi build needs from the SDK into build/ble (the container sees only this tree);
    return the compile commands, the objects and the link's libraries"""
    import shutil
    sdk = Path(os.environ.get("AC79_SDK", Path.home() / "fw-AC79_AIoT_SDK"))
    dst = OUT / "ble" / "sdk"
    if not (dst / ".copied").exists():
        for d in ("include_lib", "apps/common/include", "apps/common/config/include", "apps/common/ble/include",
                  "apps/demo/demo_ble/include"):
            shutil.copytree(sdk / d, dst / d, dirs_exist_ok=True,
                            ignore=lambda dd, names: [n for n in names if (Path(dd) / n).is_file()
                                                      and not n.endswith((".h", ".hpp"))])
        (dst / "lib").mkdir(exist_ok=True)
        for f in BLE_LIBS:
            shutil.copy(sdk / "cpu/wl82/liba" / f, dst / "lib" / f)
        shutil.copy(sdk / "include_lib/newlib/pi32v2-lib/libcompiler_rt.a", dst / "lib")
        (dst / ".copied").write_text("")
    ble_config(sdk, dst)
    if WIFI and not (dst / ".copied_wifi").exists():
        for d in LWIP_DIRS:                       # lwIP's sources
            for c in (sdk / "lib/net/lwip_2_1_3" / d).glob("*.c"):
                if c.name not in LWIP_SKIP:
                    (dst / "lwip" / d).mkdir(parents=True, exist_ok=True)
                    shutil.copy(c, dst / "lwip" / d / c.name)
        for f in WIFI_LIBS:
            shutil.copy(sdk / "cpu/wl82/liba" / f, dst / "lib" / f)
        (dst / ".copied_wifi").write_text("")
    flags = [*inc(*(dst / d for d in BLE_INC + WIFI_INC * WIFI)), *inc(SRC / "ble" / "shim")]
    if WIFI:
        flags += ["-DSHIM_WIFI=1"]
    if CONF.get("ble_stack_kb"):                  # the Bluetooth tasks' stacks, KB each (default 8)
        flags.append(f"-DSHIM_STACK_WORDS={int(CONF['ble_stack_kb']) * 256}")
    if CONF.get("ble_heap_kb") and not WIFI:      # its heap, KB (default 24, in RAM)
        flags.append(f"-DSHIM_BLE_HEAP_KB={int(CONF['ble_heap_kb'])}u")
    o = OUT / "ble" / "o"
    o.mkdir(parents=True, exist_ok=True)
    units = [("cc", "-c", SRC / "ble" / "shim" / "ctx.S", "-o", o / "ctx.o")]
    objs = [o / "ctx.o"]
    # the SDK demo's app_config.h picks its BLE demo with #defines (a -D cannot override them): the peripheral alone
    # (TRANS_DATA_EN), or with ble_central one link as a peripheral and one as a central (TRANS_MULTI_BLE_EN: the
    # controller's link count, the multi-link stack). Written from the SDK's each build, as this one wants it
    cfg = "apps/demo/demo_ble/include/app_config.h"
    text = (sdk / cfg).read_text(errors="ignore")
    if BLE and CONF.get("ble_central"):
        flags.append("-DBLE_MIDI_CENTRAL=1")
        for old, new in (("#define TRANS_DATA_EN                             1",
                          "#define TRANS_DATA_EN                             0"),
                         ("#define TRANS_MULTI_BLE_EN                        0",
                          "#define TRANS_MULTI_BLE_EN                        1"),
                         ("#define TRANS_MULTI_BLE_SLAVE_NUMS                2",
                          "#define TRANS_MULTI_BLE_SLAVE_NUMS                1"),
                         ("#define TRANS_MULTI_BLE_MASTER_NUMS               2",
                          "#define TRANS_MULTI_BLE_MASTER_NUMS               1")):
            if old not in text:
                raise SystemExit(f"build: {cfg} in the SDK has changed: no {old!r}")
            text = text.replace(old, new)
    if (dst / cfg).read_text(errors="ignore") != text:
        (dst / cfg).write_text(text)
        for f in o.glob("*.o"):
            f.unlink()                            # (built for the other configuration)
    ble_only = [*(SRC / "ble" / "midi").glob("*.c"), *[dst / s for s in BLE_SDK_SRC]] if BLE else []
    for f in [*(SRC / "ble" / "shim").glob("*.c"), *ble_only]:
        units.append(("cc", *BLE_CF, *flags, "-c", f, "-o", o / (f.stem + ".o")))
        objs.append(o / (f.stem + ".o"))
    if WIFI:
        lwip_inc = [*inc(SRC / "ble" / "lwip"), *flags, *inc(*(dst / d for d in LWIP_INC))]
        for d in LWIP_DIRS:
            for f in sorted((dst / "lwip" / d).glob("*.c")):
                units.append(("cc", *BLE_CF, *LWIP_CF, *lwip_inc, "-c", f, "-o", o / ("lwip_" + f.stem + ".o")))
                objs.append(o / ("lwip_" + f.stem + ".o"))
        for f in (SRC / "ble" / "wifi").glob("*.c"):
            units.append(("cc", *BLE_CF, *LWIP_CF, *lwip_inc, "-c", f, "-o", o / ("wifi_" + f.stem + ".o")))
            objs.append(o / ("wifi_" + f.stem + ".o"))
        app_inc = inc(APP, SRC / "include", GEN, *(APP / d for d in CONF.get("include", []) + CONF.get("device_include", [])
                                                     + CONF.get("net_include", [])))
        for u in CONF.get("net", []):            # the app's network code: as the SDK builds code that talks lwIP
            for f in (sorted(APP.glob(u)) if any(ch in u for ch in "*?[") else [APP / u]):
                name = "net_" + "_".join(f.relative_to(APP).with_suffix("").parts) + ".o"
                units.append(("cc", *BLE_CF, *LWIP_CF, *CONF.get("net_cflags", []), *lwip_inc, *app_inc, "-c", f,
                              "-o", o / name))
                objs.append(o / name)
    linked = (BLE_LIBS if BLE else RADIO_LIBS) + WIFI_LIBS * WIFI
    if BLE and CONF.get("ble_trim") and CONF.get("ble_central"):
        raise SystemExit("build: ble_trim cuts the central's scan, connecting and GATT client: not with ble_central")
    trimmed = ble_trim(dst) if BLE and CONF.get("ble_trim") else {}
    libs = ["--start-group", *[trimmed.get(f, dst / "lib" / f) for f in linked], dst / "lib" / "libcompiler_rt.a",
            "--end-group"]
    return units, objs, libs


def cache_ram_ways():
    """fm1.toml cache_ram_ways: the data cache's ways used as RAM, 0 (none: off), 2, 4 or 7 (FM1B_CACHE_RAM=N in the
    environment over it)"""
    v = os.environ.get("FM1B_CACHE_RAM")
    n = int(v) if v is not None and v.isdigit() else CONF.get("cache_ram_ways", 0)
    if not isinstance(n, int) or isinstance(n, bool) or n not in (0, 2, 4, 7):
        raise SystemExit(f"fm1.toml cache_ram_ways {n!r}: 0, 2, 4 or 7 (of the data cache's 8 ways)")
    return n


BOOT_PACKED = None                                # (raw, packed) bytes of the boot-packed load images, or None
def cpu2_stack_flags():
    """fm1.toml cpu2_stack: the second core's job stack, for its entry's unit and the platform's (none: felucca-bsp's
    2816 B, the build as before)"""
    n = CONF.get("cpu2_stack")
    if n is None:
        return []
    if not isinstance(n, int) or isinstance(n, bool) or n % 8 or not 1024 <= n <= 16384:
        raise SystemExit(f"fm1.toml cpu2_stack {n!r}: bytes, a multiple of 8, 1024..16384")
    return [f"-DFM1_CPU1_USTACK_BYTES={n}"]


def build_app():
    flags = [*CFLAGS, *FPU, *inc(BSP / "chip", BSP / "boards" / "fm1", SRC / "device", SRC / "include", SRC / "lib", APP, GEN,
                                 *(APP / d for d in CONF.get("include", []) + CONF.get("device_include", [])))]
    if BLE and OWN:                               # the own stack: its units in the unity build (device/bt_own.c)
        flags += ["-DFM1B_BLE=1", "-DFM1B_BLE_OWN=1", *inc(SRC / "bt" / "ble", SRC / "bt" / "br")]
        if CONF.get("ble_central"):
            flags.append("-DFM1B_BLE_CENTRAL=1")
            if CONF.get("ble_central") == 3:      # the central without pairing (bt/ble/ble_cfg.h BLE_CENTRAL 3)
                flags.append("-DBLE_CENTRAL=3")
        if CONF.get("bt_audio"):                   # Bluetooth audio out (A2DP source, bt/br)
            flags += ["-DFM1B_BT_AUDIO=1", *bta_mem_flags()]
            if CONF.get("bt_log"):                 # its step-by-step lines on the radio's log (fm1_remote.py log)
                flags.append("-DFM1B_BT_LOG=1")
        if CONF.get("ble_midi_time"):              # BLE MIDI in by the sender's time (plat_midi_in_t24), transport not held
            flags.append("-DFM1B_BLE_MIDI_TIME=1")
        if CONF.get("ble_pairing"):                # pairing and an encrypted link (LE legacy, Just Works; the bond kept)
            flags += ["-DBLE_LL_ENC=1", "-DBLE_SMP_LEGACY=1"]
        if CONF.get("ble_diag") is False:          # the link layer's diagnostics left out (bt/ble/ble_diag.h)
            flags.append("-DBLE_DIAG=0")
        GEN.mkdir(parents=True, exist_ok=True)    # the radio's start-up program (fm1_ble_rf.h includes it)
        tables = (BSP / "chip" / "ble_rf_tables_v15.h").read_bytes()   # (Apache-2.0: JieLi's rf_init, from V15)
        if not (GEN / "ble_rf_tables.h").exists() or (GEN / "ble_rf_tables.h").read_bytes() != tables:
            (GEN / "ble_rf_tables.h").write_bytes(tables)
    elif BLE:
        flags += ["-DFM1B_BLE=1", *inc(SRC / "ble" / "shim")]
        if CONF.get("ble_central"):
            flags.append("-DFM1B_BLE_CENTRAL=1")
    if WIFI:
        flags += ["-DFM1B_WIFI=1", "-DSHIM_WIFI=1", *inc(SRC / "ble" / "shim")]
    flags += opt_flags()
    for key, flag in (("usb_audio_in", "FM1B_UAC"), ("usb_audio_out", "FM1B_UAC_OUT")):
        if key in CONF and flag not in os.environ:
            flags.append(f"-D{flag}={int(bool(CONF[key]))}")
    if CONF.get("usb_audio_first"):
        flags.append("-DFELUCCA_UAC_AS_FIRST=1")
    midi_last = CONF.get("usb_midi_last", True)   # (the bsp leaves it out without the USB audio input)
    if midi_last not in (True, False, 0, 1, 2):
        raise SystemExit(f"fm1.toml: usb_midi_last is true, false or 2, not {midi_last!r}")
    flags.append(f"-DFELUCCA_UAC_MIDI_LAST={int(midi_last)}")
    if CONF.get("usb_iad"):
        flags.append("-DFELUCCA_UAC_IAD=1")
    if CONF.get("usb_console") and "FM1B_CDC" not in os.environ:
        flags.append("-DFM1B_CDC=1")              # the USB serial console (device/console.c)
    if CONF.get("crash_history") or CONF.get("debug_stats"):
        flags.append("-DFM1B_CRASHLOG=1")         # the crash history in flash (device/crashlog.c, stacks.c)
    if CONF.get("debug_stats"):
        flags.append("-DFM1B_DBGSTATS=1")         # the debug stats in flash (device/dbgstats.c)
    if CONF.get("midi_stamp") and "FM1B_MIDI_STAMP" not in os.environ:
        flags.append("-DFM1B_MIDI_STAMP=1")       # MIDI in stamped where it lands (plat_midi_in_t24; unity.c)
    for flag in ("FM1B_CDC", "FM1B_UAC", "FM1B_UAC_OUT", "FM1B_TRS", "FM1B_MIDI_STAMP"):
        v = os.environ.get(flag)    # unset: fm1.toml's, the app's (app.h) or the platform's default (device/unity.c)
        if v in ("0", "1"):
            flags.append(f"-D{flag}={v}")
    flags.append(f'-DFELUCCA_ID="{PRODUCT}"')
    for key, macro in (("usb_manufacturer", "FELUCCA_USB_MANUFACTURER"), ("usb_product", "FELUCCA_USB_PRODUCT")):
        if CONF.get(key):
            flags.append(f'-D{macro}="{CONF[key]}"')
    if VERSION:
        flags.append(f'-DAPP_VERSION="{VERSION}"')
    flags.append(f"-DFM1B_BUILD={build_id():#010x}u")   # (the crash history's records carry it: device/crashlog.c)
    if CONF.get("audio_aside"):                  # each half rendered aside, copied in while the DMA's half is free
        flags.append("-DFM1B_AUDIO_ASIDE=1")
    if CPU2:
        flags += ["-DFM1_CPU1=1", f"-DFM1_CPU1_FALLBACK={int(bool(CONF.get('cpu2_fallback')))}"]
        flags += cpu2_stack_flags()
    o2 = [f if f != "-Os" else "-O2" for f in flags]
    unity_opt = CONF.get("unity_opt", "s")        # the unity build's level: -Os, or -Oz (the o2 units stay -O2)
    if unity_opt not in ("s", "z"):
        raise SystemExit(f"fm1.toml: unity_opt is \"s\" or \"z\", not {unity_opt!r}")
    flags = [f"-O{unity_opt}" if f == "-Os" else f for f in flags]
    ir = CONF.get("ir_pass")                      # the app's pass over the IR (see the top)
    ir_out = ["-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns"] if ir else []
    units = [("cc", "-c", BSP / "chip" / "crt0.S", "-o", OUT / "crt0.o"),
             ("cc", "-c", BSP / "chip" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
             ("cc", "-c", BSP / "chip" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
             ("cc", *flags, *ir_out, "-c", SRC / "device" / "unity.c", "-o", OUT / ("unity.ll" if ir else "unity.o"))]
    objs = [OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o", OUT / "unity.o"]
    if CPU2:                                      # the second core's entry and stacks
        units.append(("cc", *cpu2_stack_flags(), "-c", BSP / "chip" / "fm1_cpu1.S", "-o", OUT / "fm1_cpu1.o"))
        objs.append(OUT / "fm1_cpu1.o")
    for a in CONF.get("asm", []):                 # the app's own assembly units
        o = OUT / ("asm_" + "_".join((APP / a).relative_to(APP).with_suffix("").parts) + ".o")
        units.append(("cc", "-c", APP / a, "-o", o))
        objs.append(o)
    (OUT / "o2").mkdir(parents=True, exist_ok=True)
    o2_more = [*CONF.get("o2_cflags", []),
               *[a for h in CONF.get("o2_include", []) for a in ("-include", str((APP / h).resolve().relative_to(ROOT)))]]
    o2_units = []
    for u in CONF.get("o2", []):
        for f in (sorted(APP.glob(u)) if any(ch in u for ch in "*?[") else [APP / u]):
            o = OUT / "o2" / ("_".join(f.relative_to(APP).with_suffix("").parts) + ".o")
            cpp = ["-std=c++11", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"] if f.suffix in (".cpp", ".cc") else []
            units.append(("cc", *o2, *o2_more, *cpp, *inc(f.parent), *ir_out, "-c", f, "-o", o.with_suffix(".ll") if ir else o))
            objs.append(o)
            o2_units.append((f, o))
    jieli = (BLE and not OWN) or WIFI             # JieLi's libraries linked (the shim, LTO)
    b_units, b_objs, b_libs = radio_units() if jieli else ([], [], [])
    tc_all(*units, *b_units)
    if ir:                                        # the pass, then each unit's code from its IR at its own level
        def ir_pass(src, dst, unit=None):
            r = subprocess.run([sys.executable, str(APP / ir), str(src), str(dst), *([unit] if unit else [])],
                               capture_output=True, text=True)
            sys.stdout.write(r.stdout)
            if r.returncode:
                sys.stderr.write(r.stderr)
                raise SystemExit(f"build: {ir} failed")
        ir_pass(OUT / "unity.ll", OUT / "unity.pass.ll")
        for f, o in o2_units:
            ir_pass(o.with_suffix(".ll"), o.with_suffix(".pass.ll"), str(f.relative_to(APP)))
        opt = [f for f in flags if not f.startswith(("-I", "-D", "-W"))]
        cg = [("cc", *opt, "-c", OUT / "unity.pass.ll", "-o", OUT / "unity.o"),
              *[("cc", *[f if f not in ("-Os", "-Oz") else "-O2" for f in opt], "-c", o.with_suffix(".pass.ll"), "-o", o)
                for _, o in o2_units]]
        if os.environ.get("FM1B_OPT_IR") == "1" or CONF.get("stack_check"):   # each unit's IR as its code is made from it (the optimiser's
            # output, the backend's input: tools/stack_depth.py resolves the indirect calls on it)
            cg += [(*c[:-4], "-S", "-emit-llvm", c[-3], "-o", Path(str(c[-1])).with_suffix(".opt.ll")) for c in cg]
        tc_all(*cg)
    elf = OUT / (NAME + ".elf")
    # unreferenced functions (each in its own section: -ffunction-sections) are dropped: the DSP keeps
    # mono reference paths for the host tests that the firmware no longer calls. The BT libraries are LTO
    # bitcode: the linker needs the toolchain's LLVM plugin
    lto = ["--plugin", "/opt/jieli/pi32v2/bin/LLVMgold.so" if use_docker() else str(toolchain() / "pi32v2/bin/LLVMgold.so"),
           f"--plugin-opt=mcpu={CPU}", "--plugin-opt=-pi32v2-large-program=true"] if jieli else []
    global LAYOUT
    ld = SRC / "device" / ("app_wifi.ld" if WIFI else "app_packed.ld" if ram_packed() else "app.ld")
    if not WIFI and stacks() != STACKS:          # (other stack sizes: the SRAM's end and the bands moved)
        ld = stack_script(ld)
    LAYOUT = memory_regions(ld.read_text())
    tc("pi32v2/bin/ld", *lto, "--gc-sections", "-e", "_start", "-T", ld, *objs, *b_objs,
       *b_libs, "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", f"-mcpu={CPU}", elf),
                               ("common/bin/objdump", "-d", f"-mcpu={CPU}", "-j", ".ram_text", elf))
    (OUT / (NAME + ".dis")).write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    if CONF.get("boot_pack"):                     # boot_pack = true: .ram_text and .data stored LZMA-packed from _rt_load
        global BOOT_PACKED                        # (tools/boot_pack.py, Melodee's), main_fm1.c unpacks them
        raw = [(OUT / sect).read_bytes() if (OUT / sect).exists() else b"" for sect in boot_pack.SECTIONS]
        load = symv("_rt_load")
        if load % 4 or load - APP_XIP < len(img):
            raise SystemExit(f"_rt_load {load:#x}: not word aligned, or over the image")
        img += b"\xff" * (load - APP_XIP - len(img))
        packed = boot_pack.pack_images(raw)
        img += packed
        BOOT_PACKED = (sum(map(len, raw)), len(packed))
        img += b"\xff" * (-len(img) % 4)
        (OUT / (NAME + ".bin")).write_bytes(img)
        return bytes(img), syms, dis, rt
    # .ram_text and .data follow .text at their load addresses; crt0 copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / (NAME + ".bin")).write_bytes(img)
    return bytes(img), syms, dis, rt


STACKS = (24320, 7936)          # device/app.ld's main and interrupt stacks, each above a 256-byte guard band
STACK_TOP = 0x01C7C000          # (the interrupts' top: the reset-kept record, NOINIT, is above it)


def ram_packed():
    """fm1.toml ram_layout: "fixed" (the default: device/app.ld's three regions) or "packed" (device/app_packed.ld,
    one SRAM budget) -> True for packed"""
    v = CONF.get("ram_layout", "fixed")
    if v not in ("fixed", "packed"):
        raise SystemExit(f"build: fm1.toml ram_layout = {v!r}: \"fixed\" or \"packed\"")
    if v == "packed" and WIFI:
        raise SystemExit("build: fm1.toml ram_layout = \"packed\": not with JieLi's Wi-Fi (app_wifi.ld's fixed map)")
    return v == "packed"


def stacks():
    """fm1.toml main_stack / irq_stack -> (main, irq) bytes"""
    out = []
    for key, dflt, lo in (("main_stack", STACKS[0], 2048), ("irq_stack", STACKS[1], 2048)):
        n = CONF.get(key, dflt)
        if not isinstance(n, int) or n % 256 or not lo <= n <= dflt:
            raise SystemExit(f"build: fm1.toml {key} = {n}: a multiple of 256, {lo}..{dflt}")
        out.append(n)
    if WIFI and tuple(out) != STACKS:
        raise SystemExit("build: fm1.toml main_stack / irq_stack: not with JieLi's Wi-Fi (app_wifi.ld's fixed map)")
    if tuple(out) != STACKS and not ram_packed():
        raise SystemExit("build: fm1.toml main_stack / irq_stack: only with ram_layout = \"packed\" (device/app_packed.ld)")
    return tuple(out)


def stack_script(ld):
    """-> OUT/app_packed.ld with the stacks of fm1.toml (stacks()): each above its guard band, under the interrupts' top
    (STACK_TOP); SRAM up to the first band (the ASSERT keeps them together)"""
    main, irq = stacks()
    g1 = STACK_TOP - irq - 256
    g0 = g1 - main - 256
    text = ld.read_text()
    text, n1 = re.subn(r"(SRAM\s*\(rwx\)\s*:\s*ORIGIN = 0x01C00000, LENGTH = )0x74000", rf"\g<1>{g0 - 0x01C00000:#x}", text)
    text, n2 = re.subn(r"_guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;",
                       f"_guard0 = {g0:#010x}; _ustack_lo = {g0 + 256:#010x}; _ustack_top = {g1:#010x};", text)
    text, n3 = re.subn(r"_guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;",
                       f"_guard1 = {g1:#010x}; _sstack_lo = {g1 + 256:#010x}; _sstack_top = {STACK_TOP:#010x};", text)
    if (n1, n2, n3) != (1, 1, 1):
        raise SystemExit(f"build: {ld.name}: the stacks' lines not found (main_stack / irq_stack)")
    out = OUT / ld.name
    out.write_text(text)
    return out


def stack_check(dis):
    """fm1.toml stack_check: (errors, notes) of the stacks' static worst (tools/stack_depth.py) against their sizes"""
    import stack_depth
    irs = [p.read_text(errors="replace") for p in sorted(OUT.rglob("*.opt.ll"))]
    if not irs:
        return ["stack_check: no optimised IR (OUT/*.opt.ll)"], []
    words = stack_depth.elf_words(OUT / (NAME + ".elf"))
    bounds = {k: int(v) for k, v in (b.split("=") for b in CONF.get("stack_bounds", []))}
    r = stack_depth.analyse(dis, words, irs, bounds)
    errors, notes = [], []
    bad = {k: v for k, v in r["notes"].items() if v and k in ("recursion", "var", "indirect, all taken")}
    if stack_depth.STRADDLE:
        bad["tables' ends"] = stack_depth.STRADDLE[:8]
    for k, v in bad.items():
        errors.append(f"stack_check: {k}: {', '.join(v[:8])} (not bounded: tools/stack_depth.py)")
    main, irq = stacks()
    for what, d, path, size in (("main", r["main"][0], r["main"][1], main),
                                ("interrupt", r["irq_total"], [p[2][-1] for p in r["irq"] if p[2]], irq)):
        line = f"{what} stack: {d:,} B at most (static) of {size:,} ({' > '.join(path[-4:])})"
        if d > size:
            errors.append(f"{line}: over (fm1.toml {'main' if what == 'main' else 'irq'}_stack)")
        elif stack_depth.need(d) > size:
            notes.append(f"{line}: under its margin, {stack_depth.need(d):,} B wanted")
        else:
            notes.append(line)
    return errors, notes


def memory_regions(script):
    """a linker script's MEMORY regions: {name: (origin, length)} (lengths in hex, decimal or K)"""
    block = re.search(r"\bMEMORY\s*\{(.*?)\}", script, re.S)
    if not block:
        raise SystemExit("build: the linker script has no MEMORY block")

    def num(v):
        return int(v[:-1]) * 1024 if v.endswith("K") else int(v, 0)
    return {m.group(1): (num(m.group(2)), num(m.group(3))) for m in re.finditer(
        r"^\s*(\w+)\s*\([a-z]+\)\s*:\s*ORIGIN\s*=\s*(\w+)\s*,\s*LENGTH\s*=\s*(\w+)", block.group(1), re.M)}


def mul64_near_loads(dis, window=4):
    """functions with a load in flight beside a 32x32->64-bit multiply (rN_rM = rA * rB): one up to two instructions
    before it, or one after it before its product is used (using it waits for the multiply). On the FM-1 such code
    now and then leaves a register a bit off, most of all beside 16-bit loads (felucca-bsp docs/CPU.md); a
    multiply in a helper of its own (noinline) is clear of it"""
    # vsnprintf (ble/wifi): its 64-bit digits are a divide and a multiply a few times a log line, not a hot loop
    found, fn, body, skip = [], "", [], set(CONF.get("mul64_check_skip", [])) | {"vsnprintf"}

    def scan():
        for i, ins in enumerate(body):
            m = re.match(r"(r\d+)_(r\d+) [+-]?= r\d+ \* r\d+", ins)
            if not m:
                continue
            dst = {m.group(1), m.group(2)}
            before = [b for b in body[max(0, i - 2):i] if not re.search(r"\bcall\b", b)]
            if any(re.search(r"= [hbd]?\[", b) for b in before):
                return True
            for b in body[i + 1:i + 1 + window]:
                if any(re.search(r"\b" + r + r"\b", b) for r in dst):
                    break                       # the product used: the multiply has finished
                if re.search(r"= [hbd]?\[", b):
                    return True
        return False

    for ln in dis.splitlines() + ["end:"]:
        mm = LINE.match(ln)
        if not mm:
            m2 = re.match(r"^([A-Za-z_.$][\w.$]*):$", ln.strip())
            if m2 and not m2.group(1).startswith(".GJT"):   # (a jump table's piece: still the function before it)
                if fn and not fn.startswith("__") and fn.split(".")[0] not in skip and scan() and fn not in found:
                    found.append(fn)
                fn, body = m2.group(1), []
            continue
        body.append(mm.group(3))
    return found


def check(img, syms, dis, rt):
    import ramcode                                # (tools/, on the path above)
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    c1 = [int(m2.group(1), 16) for n in ("_c1_start", "_c1_end")
          for m2 in [re.search(r"^([0-9a-f]+) .*\s" + n + r"$", syms, re.M)] if m2]
    in_c1 = (lambda ln: (mm2 := LINE.match(ln)) is not None and c1[0] <= int(mm2.group(1), 16) < c1[1]) \
        if len(c1) == 2 else (lambda ln: False)   # (the second core's loop: it may call into the flash, see the top)
    ar = [symv for n in ("_ar_start", "_ar_end") for symv in [ramcode.symbol(syms, n)] if symv is not None]
    in_ar = (lambda ln: (mm2 := LINE.match(ln)) is not None and ar[0] <= int(mm2.group(1), 16) < ar[1]) \
        if len(ar) == 2 else (lambda ln: False)   # (the app's RAM code: checked apart, tools/ramcode.py)
    rt_calls = [ln for ln in rt.splitlines() if re.search(r"\bcall\b", ln) and not in_c1(ln) and not in_ar(ln)]
    jieli = (BLE and not OWN) or WIFI
    if rt_calls and jieli:          # JieLi's .volatile_ram_code calls out; it runs with the flash on
        notes.append(f"the radio: .ram_text contains calls (JieLi's .volatile_ram_code): {rt_calls[:3]}")
    elif rt_calls:                  # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        insns = [ln for ln in rt.splitlines() if LINE.match(ln) and not in_ar(ln)]
        notes.append(f".ram_text: {len(insns)} insns, no calls")
    if CONF.get("stack_check"):
        st_errors, st_notes = stack_check(dis)
        errors += st_errors
        notes += st_notes
    ar_errors, ar_notes = ramcode.check(syms, dis, CONF.get("ram_code_allow", []))
    errors += ar_errors
    notes += ar_notes
    import romref                                 # nothing may call the chip ROM or use its addresses (tools/romref.py;
    for val in romref.rom_refs(dis, set(CONF.get("rom_check_skip", []))):   # rom_check_skip: a vendored function whose
        errors.append(f"reference to ROM address {val:#010x}")              # constant only looks like a ROM address)
    mul64 = mul64_near_loads(dis)       # the CPU fault of bsp docs/CPU.md: a note, the code may be fine
    if mul64:
        notes.append(f"64-bit multiplies inlined beside loads (felucca-bsp docs/CPU.md: may corrupt a "
                     f"register; move them out of line): {', '.join(mul64[:8])}" + (" ..." if len(mul64) > 8 else ""))
    else:
        notes.append("no 64-bit multiply inlined beside a load")
    bt = [n for n in BT_SYMS if re.search(r"\s" + n + r"$", syms, re.M)]
    if BLE and OWN:                 # the own stack: nothing of JieLi's, and the radio's ramp in RAM (docs/bt/BLE-STACK.md §8.1)
        bt = [n for n in bt if n != "ble_midi_send_many"]   # (radio.c's name, which bt_own.c gives too)
        if bt:
            errors.append(f"ble_stack = own links JieLi's Bluetooth code: {bt}")
        import ram_run              # (tools/: the RAM code run is _rt_start.._rt_end, wherever app.ld puts it)
        if not ram_run.in_ram_text(syms, "fm1_rf_burst_run"):
            errors.append("fm1_rf_burst_run is not in .ram_text (the radio's crystal ramp must run from RAM)")
        else:
            notes.append("own Bluetooth stack: nothing of JieLi's linked, the radio's ramp in RAM")
    if CONF.get("ble") and not BLE:
        if bt:
            errors.append(f"--no-ble build links Bluetooth code: {bt}")
        else:
            notes.append("--no-ble: no Bluetooth code linked")
    if len(img) > APP_SLOT:
        errors.append(f"image {len(img)} B exceeds the app slot")
    # the FPU is single precision; a soft-double routine in the image means a double crept in
    soft = sorted(set(re.findall(r"\s(__(?:add|sub|mul|div|neg|cmp|eq|ne|lt|le|gt|ge|un|extendsf|truncdf|fixdf|fixunsdf|floatsidf|floatunsidf|floatdidf)\w*df\w*)$", syms, re.M)))
    if soft and jieli:
        notes.append(f"soft-double routines linked (JieLi's radio setup): {soft[:6]}")
    elif soft:
        errors.append(f"soft-double routines linked: {soft[:6]} (a double in the code)")
    else:
        notes.append("no soft-double routines (single-precision FPU only)")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    rt, bss, pool = (sym("_rt_end") - sym("_rt_start"), sym("_bss_end") - sym("_data_start"),
                     sym("_pool_end") - sym("_pool_start"))
    spare = 2048 if WIFI else 8192
    if isinstance(CONF.get("pool_spare"), int) and not isinstance(CONF.get("pool_spare"), bool):   # an app that has spent the margin knowingly (fm1.toml)
        spare = CONF["pool_spare"]
    cr_ways, cr_used = cache_ram_ways(), sym("_cr_end") - sym("_cr_start")   # the cache RAM's own objects (.cache_ram)
    if cr_used > cr_ways * 4096:
        errors.append(f".cache_ram {cr_used} B in {cr_ways} cache ways ({cr_ways * 4096} B; fm1.toml cache_ram_ways)")
    elif cr_ways:
        notes.append(f"cache RAM: {cr_ways} data-cache ways ({cr_ways * 4096} B), {cr_used} B of objects in .cache_ram, "
                     f"{cr_ways * 4096 - cr_used} B for the app (plat_cache_ram, after its test at the start)")
    if os.environ.get("FM1B_POOL_SPARE", "").isdigit():   # an app's own build for itself (a demo build with mirror)
        spare = int(os.environ["FM1B_POOL_SPARE"])
    if "SRAM" in LAYOUT:                          # device/app_packed.ld: one packed region, one budget
        org, size = LAYOUT["SRAM"]
        used = sym("_pool_end") - org
        notes.append(f"image {len(img)} B; SRAM {used} B of {size}: RAM code {rt} B, .data+.bss {bss} B, pool {pool} B; "
                     f"free {size - used} B ({spare} kept spare)")
        if size - used < spare:                   # keep some of the SRAM spare
            errors.append(f"SRAM headroom {size - used} B < {spare} B")
        if BOOT_PACKED:                           # the boot decoder runs from the flash alone (tools/boot_pack.py)
            errors += boot_pack.check_decoder(syms, dis, APP_XIP, APP_XIP + APP_SLOT)
            notes.append(f"boot_pack: .ram_text + .data {BOOT_PACKED[0]} B stored as {BOOT_PACKED[1]} B (LZMA, unpacked at boot)")
        return errors, notes
    (_, rt_max), (_, ram), (_, pool_max) = LAYOUT["RAMTEXT"], LAYOUT["RAM"], LAYOUT["POOL"]   # (app.ld, app_wifi.ld)
    notes.append(f"image {len(img)} B; RAM code .ram_text {rt} B of {rt_max}; RAM .data+.bss {bss} B of {ram}; "
                 f"pool {pool} B of {pool_max}")
    if BOOT_PACKED:                               # the boot decoder runs from the flash alone (tools/boot_pack.py)
        errors += boot_pack.check_decoder(syms, dis, APP_XIP, APP_XIP + APP_SLOT)
        notes.append(f"boot_pack: .ram_text + .data {BOOT_PACKED[0]} B stored as {BOOT_PACKED[1]} B (LZMA, unpacked at boot)")
    if bss > ram:
        errors.append("RAM region overflow")
    if pool_max - pool < spare:                   # keep some of the pool spare
        errors.append(f"pool headroom {pool_max - pool} B < {spare} B")
    return errors, notes


# Every hardware register access lives in felucca-bsp's chip/ and boards/. In device/, include/ and the app
# (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted([*(BSP / "chip").glob("*.h"), *(BSP / "boards" / "fm1").glob("*.h")]):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    skip = CONF.get("mmio_skip", [])
    files = [*(SRC / "device").glob("*.[ch]"), *(SRC / "include").glob("*.h"), *(SRC / "lib").glob("*.[ch]"),
             *[f for f in APP.rglob("*.[ch]") if "build" not in f.relative_to(APP).parts
               and not any(str(f.relative_to(APP)).startswith(v) for v in CONF.get("vendored", []))]]
    for f in sorted(files):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(ROOT)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                if rx is MMIO_LIT and any(str(f.relative_to(APP)).startswith(s) for s in skip if APP in f.parents):
                    continue
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside the BSP ({m.group(0).strip()}); add a BSP helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: BSP register macro {sorted(used)[0]} outside the BSP; use a BSP helper")
    return errors


def o2_files():
    """the o2 units, their globs expanded"""
    return [f for u in CONF.get("o2", []) for f in (sorted(APP.glob(u)) if any(ch in u for ch in "*?[") else [APP / u])]


def o2_objects(cc, base_flags, out):
    """the o2 units compiled apart (each with its own flags: o2_cflags, o2_include, its directory) for the host or the
    browser; their objects"""
    extra = [*CONF.get("o2_cflags", []), *[a for h in CONF.get("o2_include", []) for a in ("-include", str(APP / h))]]
    out.mkdir(parents=True, exist_ok=True)
    jobs = [(f, out / ("_".join(f.relative_to(APP).with_suffix("").parts) + ".o")) for f in o2_files()]

    def one(job):
        f, o = job
        cpp = f.suffix in (".cpp", ".cc")
        unit_flags = [a for a in base_flags if a != "-std=c99"] + (["-std=c++11", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics"] if cpp else ["-std=c99"])
        r = subprocess.run([cc, *unit_flags, *extra, "-iquote", str(f.parent), "-c", str(f), "-o", str(o)],
                           capture_output=True, text=True)
        return r.stderr if r.returncode else ""
    with ThreadPoolExecutor(8) as ex:
        errs = [e for e in ex.map(one, jobs) if e]
    if errs:
        sys.stderr.write(errs[0])
        raise SystemExit("build: an o2 unit failed")
    return [str(o) for _, o in jobs]


def web_objects(base_flags, out):
    """fm1.toml web = [...]: units compiled into the browser's emulator only (a firmware's plug-ins, linked in where
    the device loads them at run time), relative to APP_DIR (globs too); their objects"""
    files = [f for u in CONF.get("web", []) for f in (sorted(APP.glob(u)) if any(ch in u for ch in "*?[") else [APP / u])]
    out.mkdir(parents=True, exist_ok=True)
    jobs = [(f, out / ("web_" + "_".join(f.relative_to(APP).with_suffix("").parts) + ".o")) for f in files]

    def one(job):
        f, o = job
        r = subprocess.run(["emcc", *base_flags, f"-I{f.parent}", "-c", str(f), "-o", str(o)], capture_output=True, text=True)
        return r.stderr if r.returncode else ""
    with ThreadPoolExecutor(8) as ex:
        errs = [e for e in ex.map(one, jobs) if e]
    if errs:
        sys.stderr.write(errs[0])
        raise SystemExit("build: a web unit failed")
    return [str(o) for _, o in jobs]


@functools.cache
def build_id():
    """this build's sources (the app's, fm1-base's, felucca-bsp's chip/) and version, hashed: the second core's bar
    (fm1_cpu1_bar_build) is lifted for a new build, and each crash record in the flash history names the build that
    kept it (device/crashlog.c; printed by every build: tools/fm1_remote.py crashes shows it)"""
    h = hashlib.sha1((VERSION or "").encode())
    for d in (APP, SRC / "device", SRC / "include", BSP / "chip"):
        for f in sorted(d.rglob("*")):
            if f.suffix in (".c", ".h", ".S", ".toml", ".ld") and "build" not in f.relative_to(d).parts:
                h.update(str(f.relative_to(d)).encode())
                h.update(f.read_bytes())
    return int.from_bytes(h.digest()[:4], "little")


def opt_flags():
    """the optional parts an fm1.toml turns on that need no SDK: the same on the device, the host and the web"""
    flags = [f"-D{flag}=1" for key, flag in (("mirror", "FM1B_MIRROR"), ("backup", "FM1B_BACKUP"), ("cpu2", "FM1B_CPU2"), ("usb_host", "FM1B_USB_HOST"), ("msc", "FM1B_MSC"),
                                           ("resources", "FM1B_RES"), ("lcd_panel", "FM1B_LCD_PANEL"), ("xdata2", "FM1B_XDATA2"),
                                           ("headphones", "FM1B_HEADPHONES"))
            if (CONF.get(key) and os.environ.get(flag) != "0") or os.environ.get(flag) == "1"]   # (FM1B_MIRROR=1: a build with it, fm1.toml as it is; FM1B_BACKUP=0: without)
    if cache_ram_ways():
        flags.append(f"-DFM1B_CACHE_RAM={cache_ram_ways()}")   # (the host and the web: plat_cache_ram's stand-in)
    if usb_audio_channels() > 2:
        flags.append(f"-DFM1B_UAC_CH={usb_audio_channels()}")  # (the host and the web: plat_usb_ch's stand-in)
    return flags


def usb_audio_channels():
    """fm1.toml usb_audio_channels: the USB audio input's channels, 2 (the default: the master) or 4, 6, 8, 10 (the app's own
    on 3..N: plat_usb_ch; felucca-bsp FELUCCA_UAC_CH, 44.1 kHz only)"""
    n = CONF.get("usb_audio_channels", 2)
    if isinstance(n, bool) or not isinstance(n, int) or n not in (2, 4, 6, 8, 10):
        raise SystemExit(f"fm1.toml usb_audio_channels {n!r}: 2, 4, 6, 8 or 10")
    if n > 2 and CONF.get("usb_audio_in") is False:
        raise SystemExit("fm1.toml usb_audio_channels: needs usb_audio_in")
    return n


def build_host():
    """the app against a simulated FM-1 (host/plat_host.c), with this machine's compiler; -ffp-contract=off like the
    device (no fused multiply-add), so the samples match"""
    out = OUT / "host"
    out.mkdir(parents=True, exist_ok=True)
    exe = out / (STEM + "_host")                  # NAME_host; NAME-usb_host with --no-ble
    inc_dirs = [f"-I{d}" for d in (SRC / "device", SRC / "include", SRC / "lib", APP, GEN, *(APP / d for d in CONF.get("include", [])))]
    o2_objs = o2_objects(os.environ.get("CC", "cc"), ["-O2", "-ffp-contract=off", "-std=c99", "-w", "-DPLAT_HOST", *opt_flags(), *inc_dirs],
                         out / "o2")
    cmd = [os.environ.get("CC", "cc"), "-O2", "-ffp-contract=off", "-std=c99", "-Wall", "-Wextra",
           "-Wno-unused-function", "-Wno-unused-parameter", "-DPLAT_HOST", *opt_flags(), f"-DFM1B_BUILD={build_id():#010x}u", *["-DFM1B_BLE=1"] * BLE, *["-DFM1B_BLE_CENTRAL=1"] * (BLE and bool(CONF.get("ble_central"))), *["-DFM1B_BT_AUDIO=1"] * bta_sim(), *(bta_mem_flags() if bta_sim() else []), *["-DFM1B_WIFI=1"] * WIFI,
           *(f"-I{d}" for d in (SRC / "device", SRC / "include", SRC / "lib", APP, GEN, *(APP / d for d in CONF.get("include", [])))),
           "-o", str(exe), str(SRC / "host" / "plat_host.c"), *o2_objs, "-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode:
        raise SystemExit("build: the host simulator did not compile")
    print(f"host     {exe}")
    return 0


EMU_PARTS = SRC / "web" / "emu" / "parts"
# the parts every emulator build has unless its fm1.toml says "-NAME" (emu = ["-resample"]): resample, as many
# computers run audio at 48 kHz, where the page would play the FM-1 off pitch (at 44.1 kHz it does nothing)
EMU_DEFAULT = ["resample"]


def emu_takes_parts():
    """whether the page loads the parts: fm1-base's page and worklet do; an app's own (APP_DIR/web/emu/index.html,
    worklet.js) only if its page loads app_worklet.js and its worklet calls fm1App (as fm1-base's)"""
    own = APP / "web" / "emu"
    return ((not (own / "index.html").exists() or "app_worklet.js" in (own / "index.html").read_text())
            and (not (own / "worklet.js").exists() or "fm1App" in (own / "worklet.js").read_text()))


def emu_parts():
    """the browser emulator's optional parts: EMU_DEFAULT and fm1.toml emu = [...] ("-NAME" leaves a part out), or
    FM1B_EMU="a b" in the environment (those and no others), each one web/emu/parts/NAME.js (the page's) and / or
    NAME_worklet.js (the worklet's); a part's file may start with a line "// emu-needs: a b", parts it needs, which
    come before it. Names are lower-case letters and digits ("parts" is the mechanism, web/emu/parts/parts.js and
    parts_worklet.js); NAME_*.js are a part's other files. The defaults only where the page takes parts"""
    env = os.environ.get("FM1B_EMU")
    if env is not None:
        asked = re.split(r"[,\s]+", env.strip())
    else:
        asked = list(CONF.get("emu", []))
        if emu_takes_parts():
            asked = EMU_DEFAULT + asked
        else:
            print(f"emu      not on: {' '.join(EMU_DEFAULT)} (the app's own page or worklet, APP_DIR/web/emu, takes no "
                  "parts: its page loads no app_worklet.js, or its worklet calls no fm1App)")
    asked = [p for p in asked if p]
    known = sorted({m[1] for f in EMU_PARTS.glob("*.js") if (m := re.fullmatch(r"([a-z0-9]+)(_worklet)?\.js", f.name))}
                   - {"parts"}) if EMU_PARTS.is_dir() else []
    for p in asked:
        if p.startswith("-") and p[1:] not in known:
            raise SystemExit(f"fm1.toml emu: no part {p[1:]!r} to leave out (web/emu/parts has: {', '.join(known) or 'none'})")
    off = {p[1:] for p in asked if p.startswith("-")}
    asked = [p for p in asked if not p.startswith("-") and p not in off]
    out = []

    def needs(p):
        got = []
        for f in (EMU_PARTS / f"{p}.js", EMU_PARTS / f"{p}_worklet.js"):
            if f.exists():
                m = re.match(r"(?://[^\n]*\n){0,4}//\s*emu-needs:([^\n]*)", f.read_text())
                got += m[1].split() if m else []
        return got

    def add(p, chain):
        if p not in known:
            raise SystemExit(f"fm1.toml emu: no part {p!r} (web/emu/parts has: {', '.join(known) or 'none'})")
        if p in chain:
            raise SystemExit(f"web/emu/parts: {' -> '.join(chain + [p])} need each other")
        if p not in out:
            for n in needs(p):
                add(n, chain + [p])
            out.append(p)
    for p in asked:
        add(p, [])
    return out


def emu_parts_out(emu, parts):
    """the app's own part of the page and of the worklet (APP_DIR/web/emu/app.js, app_worklet.js) as they are; with
    parts, app.js and app_worklet.js are made to load the parts (web/emu/parts/parts.js, parts_worklet.js) and the app's
    own files under new names (app_own.js, app_own_worklet.js). What an earlier build with other parts left goes"""
    import json
    import shutil
    own = {f: APP / "web" / "emu" / f for f in ("app.js", "app_worklet.js") if (APP / "web" / "emu" / f).exists()}
    for f in ("app.js", "app_worklet.js", "app_own.js", "app_own_worklet.js"):
        if (emu / f).exists():
            (emu / f).unlink()
    if (emu / "parts").exists():
        shutil.rmtree(emu / "parts")
    if not parts:
        for f, src in own.items():
            shutil.copy(src, emu / f)
        return
    (emu / "parts").mkdir()
    for p in ["parts", *parts]:
        for f in sorted(EMU_PARTS.glob(f"{p}*.js")):
            if re.fullmatch(rf"{p}(_[a-z0-9_]+)?\.js", f.name):
                shutil.copy(f, emu / "parts" / f.name)
    if "app.js" in own:
        shutil.copy(own["app.js"], emu / "app_own.js")
    if "app_worklet.js" in own:
        shutil.copy(own["app_worklet.js"], emu / "app_own_worklet.js")
    page = [p for p in parts if (EMU_PARTS / f"{p}.js").exists()]
    (emu / "app.js").write_text(
        "// made by tools/build.py: the emulator's parts (fm1.toml emu) and the app's own part of the page\n"
        'import { compose } from "./parts/parts.js";\n'
        + "".join(f'import * as {p} from "./parts/{p}.js";\n' for p in page)
        + ('import * as own from "./app_own.js";\n' if "app.js" in own else "const own = {};\n")
        + f"export const {{ attach, message }} = compose({{ {', '.join(page)} }}, own, "
        + f"{{ id: {json.dumps(NAME)}, name: {json.dumps(CONF.get('name', NAME))}, "
        + f"backupExt: {json.dumps(CONF.get('backup_ext', 'bak'))} }});\n")
    (emu / "app_worklet.js").write_text(
        "// made by tools/build.py: the app's own part of the worklet, then the emulator's parts (fm1.toml emu)\n"
        + ('import "./app_own_worklet.js";\n' if "app_worklet.js" in own else "")
        + 'import "./parts/parts_worklet.js";\n'
        + "".join(f'import "./parts/{p}_worklet.js";\n' for p in parts if (EMU_PARTS / f"{p}_worklet.js").exists()))
    print(f"emu      parts: {' '.join(parts)}")


def build_web():
    """the host simulator in WebAssembly (web/emu/emu_web.c) and its page, in OUT/emu; the screen reader's voice
    (eSpeak NG, GPL: a separate program the page loads) from an fm1-reader checkout next to fm1-base, if there"""
    import shutil
    emu = OUT / "emu"
    emu.mkdir(parents=True, exist_ok=True)
    web_inc = [f"-I{d}" for d in (SRC / "host", SRC / "device", SRC / "include", SRC / "lib", APP, GEN, *(APP / d for d in CONF.get("include", [])))]
    wf = CONF.get("web_cflags", [])
    o2_objs = o2_objects("emcc", ["-O2", "-ffp-contract=off", "-std=gnu99", "-w", "-DPLAT_HOST", "-DPLAT_WEB", *wf, *opt_flags(), *web_inc],
                         emu / "o2")
    o2_objs += web_objects(["-O2", "-ffp-contract=off", "-std=gnu99", "-w", "-DPLAT_HOST", "-DPLAT_WEB", *wf, *opt_flags(), *web_inc],
                           emu / "web")
    parts = emu_parts()
    cmd = ["emcc", "-O2", "-ffp-contract=off", "-std=gnu99", "-Wall", "-Wno-unused-function", "-Wno-unused-parameter",
           "-Wno-unused-variable", "-DPLAT_HOST", "-DPLAT_WEB", *opt_flags(), *(f"-DFM1B_EMU_{p.upper()}=1" for p in parts), *["-DFM1B_BLE=1"] * BLE, *["-DFM1B_BLE_CENTRAL=1"] * (BLE and bool(CONF.get("ble_central"))), *["-DFM1B_BT_AUDIO=1"] * bta_sim(), *(bta_mem_flags() if bta_sim() else []), *["-DFM1B_WIFI=1"] * WIFI, *(f'-DAPP_VERSION="{VERSION}"',) * bool(VERSION),
           *(f"-I{d}" for d in (SRC / "host", SRC / "device", SRC / "include", SRC / "lib", APP, GEN, *(APP / d for d in CONF.get("include", [])))),
           *wf, "--no-entry", "-sSTANDALONE_WASM", "-sSTACK_SIZE=1048576", "-sINITIAL_MEMORY=33554432", "-sFILESYSTEM=0",
           "-o", str(emu / "app.wasm"), str(SRC / "web" / "emu" / "emu_web.c"), *o2_objs,
           *[str(f) for u in CONF.get("web_link", []) for f in (sorted(APP.glob(u)) if any(ch in u for ch in "*?[")
                                                               else [APP / u])]]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode:
        raise SystemExit("build: the emulator did not compile (emcc: Emscripten)")
    # the page: the app's own (APP_DIR/web/emu/index.html, worklet.js), or fm1-base's
    web = {f: (APP / "web" / "emu" / f if (APP / "web" / "emu" / f).exists() else SRC / "web" / "emu" / f)
           for f in ("index.html", "worklet.js")}
    page = web["index.html"].read_text()
    page = page.replace("{{NAME}}", CONF.get("name", NAME)).replace("{{ID}}", NAME)
    page = page.replace("{{PANEL_JS}}", re.sub(r"^export\s+", "", (SRC / "web" / "fm1panel.js").read_text(), flags=re.M))
    (emu / "index.html").write_text(page)
    shutil.copy(web["worklet.js"], emu / "worklet.js")
    emu_parts_out(emu, parts)
    # next to fm1-base, or next to the app's repository (fm1-base a submodule in it)
    found = [p / "fm1-reader" / "web" for base in (SRC.parent, APP.parent.parent) for p in (base, base / "fm1-parent")
             if (p / "fm1-reader" / "web").is_dir()]
    reader = Path(os.environ.get("FM1_READER", found[0] if found else SRC.parent / "fm1-reader" / "web"))
    if (reader / "words.js").exists() and (reader / "espeak" / "espeak.wasm").exists():
        (emu / "reader" / "espeak").mkdir(parents=True, exist_ok=True)
        for f in ("words.js", "tts.js", "tts_worker.js"):
            shutil.copy(reader / f, emu / "reader" / f)
        for f in ("espeak.js", "espeak.wasm", "LICENSE", "README.md"):
            shutil.copy(reader / "espeak" / f, emu / "reader" / "espeak" / f)
        (emu / "reader" / "FROM.txt").write_text(f"From fm1-reader (https://github.com/charlesvestal/fm1-reader) web/, "
                                                 f"copied by fm1-base's tools/build.py --web.\n")
    else:
        print(f"emu: no fm1-reader at {reader}: the screen is read as plain text, without its voice (set FM1_READER)")
    print(f"emu      {emu}  ({(emu / 'app.wasm').stat().st_size} B wasm)")
    return 0


def git(*args, cwd=None):
    r = subprocess.run(["git", *args], cwd=cwd or APP, capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


def c_str(s):
    """a C string literal (split by line, so a long text stays readable in the header)"""
    def esc(t):
        return t.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t").replace("?", "\\?")
    lines = s.split("\n")
    return " \\\n".join('"' + esc(ln) + ("\\n" if i < len(lines) - 1 else "") + '"' for i, ln in enumerate(lines))


def build_rfm1():
    """the host simulator in WebAssembly with host/rfm1.c's exports, packed as an .rfm1 (tools/rfm1_pack.py): the
    manifest from fm1.toml, app.h and git; the credits and licence texts of the app, fm1-base, felucca-bsp and
    Emscripten's runtime"""
    out = OUT / "rfm1"
    out.mkdir(parents=True, exist_ok=True)
    repo = Path(git("rev-parse", "--show-toplevel") or APP.parent)
    head = git("rev-parse", "HEAD")
    if git("status", "--porcelain", "--untracked-files=no"):
        print("rfm1: the app's repository has changes not committed: source_url names HEAD, which is not what was built")
    app_name = next((m[1] for h in sorted(APP.glob("*.h"))
                     for m in [re.search(r'^#define APP_NAME "([^"]*)"', h.read_text(errors="ignore"), re.M)] if m),
                    CONF.get("name", NAME))
    rid = re.sub(r"[^a-z0-9_]", "_", NAME)[:31]
    version = VERSION or "dev+" + (head[:7] or "local")
    url = CONF.get("repo") or re.sub(r"^git@github\.com:", "https://github.com/", git("remote", "get-url", "origin"))
    url = re.sub(r"\.git$", "", url)
    source_url = f"{url}@{head}" if head else url
    base_head = git("rev-parse", "HEAD", cwd=SRC)
    emver = subprocess.run(["emcc", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    emver = re.search(r"\) ([0-9][^ ]*)", emver)[1] if re.search(r"\) ([0-9][^ ]*)", emver) else emver
    authors = CONF.get("authors") or ([CONF["author"]] if CONF.get("author") else None)
    authors = authors or [a for a in [git("log", "--reverse", "--format=%an", cwd=repo).split("\n")[0]] if a]
    mem = int(CONF.get("rfm1_memory_mib", 8))
    if not 1 <= mem <= 32:
        raise SystemExit(f"fm1.toml rfm1_memory_mib {mem}: 1 .. 32")
    # credits: the app's (CREDITS.md, or fm1.toml credits), then fm1-base's
    credits = []
    if (repo / "CREDITS.md").exists():
        credits.append((repo / "CREDITS.md").read_text().strip())
    elif CONF.get("credits"):
        credits.append(f"{app_name}: {CONF['credits']}")
    credits.append((SRC / "CREDITS.md").read_text().strip())
    credits = "\n\n".join(credits) + "\n"
    (out / "credits.txt").write_text(credits)
    licence = CONF.get("licence", "MPL-2.0")
    (GEN / "rfm1_info.h").write_text(
        "/* tools/build.py --rfm1: the descriptor's strings (the manifest's) */\n"
        f"#define RFM1_ID {c_str(rid)}\n#define RFM1_NAME {c_str(app_name)}\n#define RFM1_VERSION {c_str(version)}\n"
        f"#define RFM1_SOURCE_URL {c_str(source_url)}\n#define RFM1_LICENSE {c_str(licence)}\n"
        f"#define RFM1_CREDITS \\\n{c_str(credits)}\n")
    # the licence texts: the app's, fm1-base's, felucca-bsp's, Emscripten's (its runtime and musl are compiled in)
    lic = []
    for f in [repo / "LICENSE", repo / "COPYING", *sorted((repo / "LICENSES").glob("*")), repo / "LICENSING.md"]:
        if f.is_file():
            lic.append((f"{app_name}: {f.relative_to(repo)}", f))
    lic += [("fm1-base: LICENSE (MPL-2.0)", SRC / "LICENSE"), ("felucca-bsp: LICENSE", BSP / "LICENSE")]
    em_root = subprocess.run(["em-config", "EMSCRIPTEN_ROOT"], capture_output=True, text=True).stdout.strip()
    em_lic = next((d / "LICENSE" for d in (Path(em_root), Path(em_root).parent) if em_root and (d / "LICENSE").exists()), None)
    if em_lic:
        lic.append(("Emscripten (its runtime, compiled in): LICENSE", em_lic))
        musl = Path(em_root) / "system" / "lib" / "libc" / "musl" / "COPYRIGHT"
        if musl.exists():
            lic.append(("musl libc (in Emscripten's runtime): COPYRIGHT", musl))
    else:
        print("rfm1: Emscripten's LICENSE not found (em-config EMSCRIPTEN_ROOT): the file goes without it")
    lic = [(n, f) for n, f in lic if f.is_file()]
    feats = ["-mcpu=mvp", "-msign-ext", "-mnontrapping-fptoint", "-mbulk-memory", "-mmutable-globals"]   # wasm3 0.5.0's
    inc = [f"-I{d}" for d in (SRC / "host", SRC / "device", SRC / "include", SRC / "lib", APP, GEN, *(APP / d for d in CONF.get("include", [])))]
    defs = ["-DPLAT_HOST", "-DPLAT_WEB", "-DPLAT_RFM1", *opt_flags(), *["-DFM1B_BLE=1"] * BLE,
            *["-DFM1B_BLE_CENTRAL=1"] * (BLE and bool(CONF.get("ble_central"))), *["-DFM1B_WIFI=1"] * WIFI,
            *(f'-DAPP_VERSION="{VERSION}"',) * bool(VERSION)]
    o2_objs = o2_objects("emcc", [*feats, "-O2", "-ffp-contract=off", "-std=gnu99", "-w", *defs, *inc], out / "o2")
    wasm = out / "app.wasm"
    cmd = ["emcc", *feats, "-O2", "-ffp-contract=off", "-std=gnu99", "-Wall", "-Wno-unused-function",
           "-Wno-unused-parameter", "-Wno-unused-variable", *defs, *inc,
           "--no-entry", "-sSTANDALONE_WASM", "-sFILESYSTEM=0", "-sSTACK_SIZE=1048576",
           f"-sINITIAL_MEMORY={mem << 20}", "-sALLOW_MEMORY_GROWTH=0",
           "-o", str(wasm), str(SRC / "host" / "rfm1.c"), *o2_objs]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode:
        raise SystemExit("build: the .rfm1 did not compile (emcc: Emscripten; rfm1_memory_mib if the data does not fit)")
    manifest = {"rfm1": 1, "abi": 6, "id": rid, "name": app_name, "version": version, "license": licence,
                "source_url": source_url, "authors": authors,
                "glue_url": f"https://github.com/charlesvestal/fm1-base/tree/{base_head}" if base_head else None,
                "built_by": f"fm1-base {base_head[:7]} tools/build.py --rfm1, emcc {emver}", "memory_mib": mem}
    manifest = {k: v for k, v in manifest.items() if v}
    import json
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1, ensure_ascii=False))
    # a release's beside its package, named as it is (NAME-X.Y.rfm1); a dev build's in rfm1/ with the commit
    dst = OUT / f"{STEM}-{RELEASE}.rfm1" if RELEASE else out / f"{rid}-{version}.rfm1"
    r = subprocess.run([sys.executable, str(SRC / "tools" / "rfm1_pack.py"), str(wasm), str(dst),
                        "--manifest", str(out / "manifest.json"), "--credits", str(out / "credits.txt"),
                        *[a for n, f in lic for a in ("--license", f"{n}={f}")]], capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    sys.stderr.write(r.stderr)
    if r.returncode:
        raise SystemExit("build: the .rfm1 is not to the format (tools/rfm1_pack.py)")
    return 0


def main():
    global PRODUCT, VERSION, RELEASE, APP, OUT, GEN, LDR, ROOT, CONF, NAME, BLE, OWN, WIFI, CPU2, STEM
    ap = argparse.ArgumentParser()
    ap.add_argument("app", type=Path, help="the app's directory (with fm1.toml)")
    ap.add_argument("--release", metavar="X.Y[.Z]", help="release build: identity id_prefix + XXYYZZ, version string X.Y[.Z]")
    ap.add_argument("--no-ble", action="store_true", help="without Bluetooth (fm1.toml ble = true): NAME-usb.fwsc")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    ap.add_argument("--host", action="store_true", help="build the host simulator instead of the firmware")
    ap.add_argument("--web", action="store_true", help="build the browser emulator instead of the firmware")
    ap.add_argument("--rfm1", action="store_true", help="build an .rfm1 (host apps) instead of the firmware")
    a = ap.parse_args()
    APP = a.app.resolve()
    CONF = read_conf(APP / "fm1.toml")
    NAME = CONF.get("name", APP.name).lower()
    BLE = bool(CONF.get("ble")) and not a.no_ble
    OWN = BLE and CONF.get("ble_stack", "jieli") == "own"
    if CONF.get("ble_stack", "jieli") not in ("jieli", "own"):
        raise SystemExit(f"fm1.toml ble_stack {CONF['ble_stack']!r}: \"jieli\" or \"own\"")
    WIFI = bool(CONF.get("wifi"))
    CPU2 = (bool(CONF.get("cpu2")) and os.environ.get("FM1B_CPU2") != "0") or os.environ.get("FM1B_CPU2") == "1"
    if BLE and WIFI:
        raise SystemExit("fm1.toml: ble and wifi together are not supported yet (one radio a build; --no-ble for WiFi)")
    STEM = NAME + ("-usb" if CONF.get("ble") and not BLE else "")
    OUT = APP / "build"
    GEN, LDR = OUT / "gen", OUT / "loader"
    ROOT = Path(os.path.commonpath([SRC, APP]))
    prefix = CONF.get("id_prefix", "FM-1_0")
    if not re.fullmatch(r"FM-1_\d+", prefix):
        raise SystemExit(f"fm1.toml id_prefix {prefix!r}: FM-1_ and digits only (the update tools parse it)")
    PRODUCT = prefix + "00"
    name = STEM + ".fwsc"
    if a.release:
        # identity: the prefix + major, minor, patch as two digits each (0.1 -> FM-1_0000100): FM-1 is the
        # model the update tools match on; the digits after the underscore are the firmware's.
        m = re.fullmatch(r"(\d{1,2})\.(\d{1,2})(?:\.(\d{1,2}))?(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y[.Z][-suffix], up to two digits each")
        PRODUCT = prefix + "%02d%02d%02d" % (int(m[1]), int(m[2]), int(m[3] or 0))
        VERSION = a.release.upper()
        RELEASE = a.release
        name = f"{STEM}-{a.release}.fwsc"
    # a release package needs firmware.json's keys; the simulator, the emulator, the .rfm1 and dev builds only warn
    firmware_json.require(CONF, bool(a.release) and not (a.host or a.web or a.rfm1), APP / "fm1.toml", APP)
    touch = any(re.search(r"^#define APP_TOUCH\b", f.read_text(errors="ignore"), re.M) for f in APP.glob("*.h"))
    words = firmware_json.words_published(APP / CONF.get("words", "words.tsv"))
    have = firmware_json.parts(CONF, BLE, OWN, WIFI, CPU2, touch, words)
    fw_json = firmware_json.dumps(firmware_json.make(CONF, NAME, firmware_json.display_name(CONF, APP), have,
                                                     app_dir=APP))
    for f in firmware_json.targets(APP, no_ble=bool(CONF.get("ble")) and not BLE):
        firmware_json.write(f, fw_json)        # (docs/FIRMWARE-JSON.md; a file already so is left alone)
    if a.host or a.web or a.rfm1:
        generate()
        return build_host() if a.host else build_web() if a.web else build_rfm1()
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
    img, syms, dis, rt = build_app()
    errors, notes = check(img, syms, dis, rt)
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: felucca-bsp only (device/, include/ and the app clean)")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    parts = [(label, have[f]) for f, label in firmware_json.FEATURES]
    print("parts    " + ", ".join(n for n, x in parts if x) + "  (not built in: " +
          (", ".join(n for n, x in parts if not x) or "none") + ")")
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
    print(f"app      {OUT / (NAME + '.bin')}  {len(img)} B")
    if CONF.get("resources"):                 # the resource region beside the package: the installer writes it after
        res = OUT / (Path(name).stem + "-res.bin")   # the firmware (fm1_install.py, the installer page)
        res.write_bytes(res_file(res_payload()))
        print(f"res      {res}  {res.stat().st_size} B (written after an update: tools/fm1_resources.py)")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}")
    print(f"build    {build_id():08X}  (FM1B_BUILD: the build a crash record names, fm1_remote.py crashes)")
    print(f"about    {firmware_json.targets(APP, no_ble=bool(CONF.get('ble')) and not BLE)[0]}  (for firmware hubs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
