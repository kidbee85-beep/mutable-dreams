# fm1-base

A base for M-VAVE FM-1 firmware, on [felucca-bsp](https://github.com/hugelton/felucca-bsp): the platform layer
above the hardware (screen, drawing and fonts, panel, LEDs, audio, MIDI over USB and TRS, storage, the screen reader),
a host simulator and a browser emulator that run the same app, and the tools to build, package and install it.
MPL-2.0: an app built on it may use any licence. See [DESIGN.md](DESIGN.md), [LICENSING.md](LICENSING.md) and
[CREDITS.md](CREDITS.md).

**Status: beta.** Two firmwares are built on it so far, and they are its proof points:
[CTL-1](https://github.com/charlesvestal/fm1-ctl) (with Bluetooth MIDI) and
[Radio](https://github.com/charlesvestal/fm1-radio) (with WiFi). Expect the platform interface (`include/plat.h`)
and `fm1.toml`'s settings to change before 1.0.

The template (`template/`) runs on an FM-1: screen, keys and their LEDs, sound, MASTER, encoders, settings kept
across a power cycle, USB-MIDI both ways, the screen reader, and an update started from the template itself. The same
app runs on the computer in the host simulator and in the browser: try the template's at
<https://charlesvestal.github.io/fm1-base/emu/>.

    git clone --recursive https://github.com/charlesvestal/fm1-base

## Optional parts

A firmware has only what its fm1.toml asks for (and its app.h, for velocity and pressure); every build prints the
parts it has.

| Part | How to include it | What the app calls |
| --- | --- | --- |
| Bluetooth MIDI with a computer or tablet | `ble = true` (`tools/build.py --no-ble` leaves it out of one build) | `TO_BLE`, `PORT_BLE`; `plat_ble_state`, `plat_ble_connected`; `plat_ble_enable`; with `ble_stack = "own"` a stream of SysEx frames read by the radio at the link's rate (`plat_ble_stream`: a screen mirrored) and the link as agreed (`plat_ble_link_info`) |
| Bluetooth MIDI devices (the FM-1 connects to a keyboard or controller) | `ble_central = true`, with `ble` | `plat_ble_scan`, `plat_ble_found`, `plat_ble_connect`, `plat_ble_disconnect`, `plat_ble_device`; `TO_BLE_HOST`, `TO_BLE_DEV`, `plat_midi_in_from` (one link at a time); `plat_ble_passkey` on the own stack |
| Bluetooth on fm1-base's own stack instead of JieLi's libraries (below) | `ble_stack = "own"`, with `ble` | the same calls |
| Pairing on the own stack (LE legacy pairing, the bond kept) | `ble_pairing = true`, with `ble_stack = "own"` | nothing |
| Bluetooth MIDI in by the sender's time (for an app that follows a clock by time) | `ble_midi_time = true`, with `ble_stack = "own"` | `plat_midi_in_t24` after `plat_midi_in` |
| Bluetooth audio to headphones and speakers (A2DP) | `bt_audio = true`, with `ble_stack = "own"`; `bt_log = true` puts its set-up on the radio's log | `plat_bta_on`, `plat_bta_scan`, `plat_bta_found`, `plat_bta_known`, `plat_bta_pick`, `plat_bta_reconnect`, `plat_bta_forget`, `plat_bta_hangup`, `plat_bta_state` |
| USB audio: the FM-1 recorded over the cable | on unless `usb_audio_in = false` | nothing: the app's output at MASTER, without the computer's audio (taken before it is mixed in) |
| USB audio: a computer plays through the FM-1 | on unless `usb_audio_out = false` (needs the input too) | nothing: mixed after the app and after the recording's tap, at MASTER; `plat_usb_play_gain` (0 muted .. 4096, the default) for the app's own setting |
| USB MIDI after the USB audio interfaces, so macOS 13-15 show the audio device | on with the USB audio input, unless `usb_midi_last = false` | nothing |
| Velocity and pressure from the keys | `#define APP_TOUCH` in app.h | `touch_on`, `touch_poll`, `touch_keys`, `touch_vel`, `touch_pressure` (lib/touch.h) |
| WiFi: a station on the networks around, with lwIP's sockets | `wifi = true` (not with `ble`, yet), and `net = [...]` for the app's network code | `plat_wifi_on`, `plat_wifi_state`, `plat_wifi_scan`, `plat_wifi_found`, `plat_wifi_join`, `plat_wifi_saved`, `plat_wifi_forget`, `plat_net_task` |
| A page for Bluetooth MIDI devices: the scan while it's up, the list, SAVE to connect or let go, and what happened in words | `#define APP_BTDEV` in app.h, with `ble_central` | `btdev_page`, `btdev_event`, `btdev_word`, `btdev_found`, `btdev_choose`, `btdev_save`, `btdev_title` (lib/btdev.h) |
| USB audio at full level, MASTER for the speaker and headphones only | with the USB audio input | `plat_usb_audio_full` (the app's setting) |
| Screen mirroring: the screen and panel live on a computer (web/mirror, a PNG with `tools/fm1_mirror.py`), the panel played from it | `mirror = true` | nothing; `APP_REDRAW` (what a slow link dropped, drawn again), `APP_MIRROR_TEST` (a test session from the remote) |
| Backup and restore of the app's saved objects over USB, all or nothing (web/backup, `tools/fm1_backup.py`) | `backup = true` | app.h: `APP_BACKUP_OBJS`, `APP_BACKUP_MAGIC`, `APP_BACKUP_VERSION`, `APP_BACKUP_WHAT`; `app_backup_playing`, `_flush`, `_hold`, `_restored` |
| The app's own commands over USB, in the update's framing, data pulled from the host | `#define APP_USB_CMD` in app.h | `app_usb_cmd`; `plat_host_read`, `plat_host_patience` |
| The USB host's device by name (its product string), for the app's own page | `#define APP_USB_HOST_NAME` in app.h, with `usb_host` | `plat_usb_host_name` |
| Lower case in the big fonts | `font_full = ["L"]` | the fonts as ever |
| The firmware as an `.rfm1` for host apps (RemoteFM1 for iOS), below | `tools/build.py APP_DIR --rfm1` | nothing |
| The browser emulator's own parts (`tools/build.py --web`): web/emu/parts, each file saying what it does; without any, the page and app.wasm are as before. One is on without asking: `resample`, the FM-1's 44.1 kHz played at the browser's rate (many computers run audio at 48 kHz), where the page takes parts (fm1-base's page and worklet) | `emu = ["name", ...]`, `"-resample"` to leave it out (or `FM1B_EMU="name ..."` for one build: those parts only) | nothing, unless a part says so |

Every firmware also counts each port's MIDI messages, in and out: `plat_midi_count(PORT_*, out)` for counters,
`plat_midi_ago` for activity lights.
And: `plat_midi_out_room(to)` (room in the out queues, each Bluetooth link's its own, for a sequencer's note-offs),
`plat_midi_in_from()` (where the last message in came from, as an out: for MIDI thru) and `plat_midi_out_link()` (a new
USB connection); `plat_store_size` and `plat_store_head` (an object whole or not at all; its header only); big data in
flash beyond the store with `plat_data*`, in the data area and two more windows (`PLAT_XDATA0`, `PLAT_XDATA1`), and a
third on request (`xdata2 = true`: `PLAT_XDATA2`, 0xFC000..0xFEFFF).

## Bluetooth and WiFi

A firmware has Bluetooth or WiFi, not both: each fills most of what the FM-1 has (the chip has no external RAM),
and together they don't fit. WiFi, and Bluetooth on JieLi's stack, need JieLi's AC79 SDK (`$AC79_SDK`) for its
libraries. Bluetooth on fm1-base's own stack (`ble_stack = "own"`) links nothing of JieLi's, and neither does a
build without a radio. Every build still takes the three SDK files a package needs (LICENSING.md).

### Two Bluetooth stacks

By default (`ble = true`), Bluetooth runs on JieLi's binary libraries from the SDK, through a shim (`ble/`). With
`ble_stack = "own"` it runs instead on Henri David's stack from [Optimist](https://github.com/w0ts/optimist),
in `bt/` under MPL-2.0, with felucca-bsp's radio drivers. Nothing of JieLi's is linked, there is no shim and no
task, and the build checks it. The radio calibrates itself at every start, so an FM-1 whose stock calibration was
overwritten works the same. The own stack is smaller and does more:

- Bluetooth MIDI with a computer or a phone, as on JieLi's.
- With `ble_pairing = true` it pairs and encrypts the link (LE legacy pairing), and keeps the bond in `OBJ_BLE`.
  Without it a pairing is refused; macOS and iOS connect anyway.
- `ble_diag = false` leaves out the link layer's diagnostics (its counters and event rings: about 2.4 KB of RAM
  and some code; the radio log's `BT ll` line). They are kept by default.
- With `ble_midi_time = true` each message from Bluetooth comes with the time it was sent, from its BLE-MIDI
  timestamp (`plat_midi_in_t24`): the connection's bunching taken out, for an app that times a clock it follows
  itself. Without it the clock and transport are held to the sender's time (spaced as sent, up to 0.12 s later).
- With `ble_central = true` the FM-1 connects to Bluetooth MIDI devices, as on JieLi's. A device that asks for a
  code gets one on the FM-1's screen (`plat_ble_passkey`).
- With `bt_audio = true` it sends its sound to headphones and speakers (A2DP, SBC), beside Bluetooth MIDI on the
  one radio. While it streams, the FM-1's own output is muted. Four devices are remembered, the last one first, and
  `plat_bta_reconnect(0)` at power-on connects it again. `bt_log = true` adds the audio set-up's steps to the
  radio's log.

It was tried on one FM-1 with a Mac, an iPhone (GarageBand; Cubasis as host and as a peripheral with the code),
headphones (AIAIAI TMA-2) and a speaker (Anker SoundCore 2), MIDI and audio at once. Its limits:

- One Bluetooth MIDI link at a time: while the FM-1 is connected to a device, a computer can't connect to it
  (JieLi's keeps both).
- Audio reaches the ear 150-300 ms after a key (the device's own buffer): for listening, not for playing in time.
- After an abrupt drop, a speaker may not answer for a minute or two.
- Each firmware keeps its own keys. After switching firmware, forget the FM-1 on the phone or speaker and pair
  again.

`examples/ble_own` is the template on the own stack with every option. For how the stack works, see
[docs/bt/README.md](docs/bt/README.md).

### Turning Bluetooth on

1. In fm1.toml, `ble = true`. Add `ble_central = true` for the FM-1 to connect to MIDI devices too (a keyboard,
   a controller), beside a computer. For the own stack, add `ble_stack = "own"` and its options (above).
2. In app.h, give it a store object: `OBJ_BLE` in the app's object enum, and its offset in `APP_STORE_MAP` (the
   radio's address and pairing are kept there). On the own stack an app may keep them in its own saved data instead
   (a settings record of its own): `#define APP_BLE_STORE` and `app_ble_load` / `app_ble_save` (plat.h `struct
   plat_ble_rec`), and no `OBJ_BLE`.
3. In the app: MIDI goes out with `TO_BLE` and comes in on `PORT_BLE`, as USB's does. For a page of devices, add
   `#define APP_BTDEV` to app.h and use lib/btdev.h (the template's REC does).
4. With devices there are two links, the computer's and the device's. `TO_BLE` goes to both; `TO_BLE_HOST` or
   `TO_BLE_DEV` goes to one, each with its own queue, so a slow device never holds up the computer. Both come in on
   `PORT_BLE`, and `plat_midi_in_from()` says which. MIDI thru, with nothing sent back where it came from (a keyboard
   played through to a phone): after each `plat_midi_in`,
   `plat_midi_send(pkt, (TO_USB | TO_BLE) & ~plat_midi_in_from())`.

Bluetooth starts at every power-on, as on M-VAVE's firmware, unless the last Bluetooth run crashed. A firmware
that lets the player turn it off keeps that as a setting, calls `plat_ble_enable(0)` / `plat_ble_enable(1)` when it
changes, and tells the platform whether to start it at power-on with `APP_BLE_AT_BOOT` in app.h (an expression,
e.g. `(set.bluetooth != 0)`). Off ends every link and stops advertising and scanning: the radio is quiet. Its manual
should say where the setting is (the template's: for details, see template/docs/MANUAL.md).
`tools/build.py --no-ble` builds the same firmware without Bluetooth (NAME-usb.fwsc).

### Turning WiFi on

1. In fm1.toml, `wifi = true` (and not `ble`). Leave USB audio out (`usb_audio_in = false`, `usb_audio_out =
   false`): WiFi needs its RAM.
2. In app.h, two store objects: `OBJ_BLE` (the radio's address and calibration) and `OBJ_WIFI` (the networks
   kept), with their offsets in `APP_STORE_MAP`; and a smaller canvas, `#define APP_CANVAS_PX (240u * 48u)` (draw
   the screen in strips 48 rows high or less).
3. The app's network code goes in its own files, listed in fm1.toml's `net = [...]`: they're built as the SDK
   builds code that talks lwIP, and include `lwip/sockets.h`. That code runs in a task of its own
   (`plat_net_task`): a socket call waits, and the main loop must not.
4. In the app: `plat_wifi_on(1)` turns it on; it joins the networks it joined before (six, the most recent first).
   `plat_wifi_scan` and `plat_wifi_found` list the networks around, `plat_wifi_join` joins one (kept once joined),
   `plat_wifi_state` and `plat_wifi_ip` say how far it got. Passwords come over USB:
   `tools/fm1_remote.py wifi join SSID PASSWORD` (or typed on the keys: Radio has a page for it).

For a working example, see `examples/wifi`; for a whole firmware, fm1-radio.

### What each costs

The FM-1 has a 581 KB app slot in the flash, 96 KB of RAM for `.data` and `.bss` (128 KB in a WiFi build), and a
336 KB pool of RAM for big buffers (304 KB in a WiFi build). With `ram_layout = "packed"` in fm1.toml
(`device/app_packed.ld`) the RAM code, `.data` and `.bss` and the pool share one budget of 464 KB instead, packed from
its bottom, 8 KB kept spare (and `main_stack` / `irq_stack` may give it the stacks' unused part). Measured on the
template (October 2026), in KB:

| | Flash | RAM | Pool |
| --- | --- | --- | --- |
| The template without a radio (`--no-ble`) | 77 | 27 | 68 |
| Bluetooth MIDI with a computer (`ble`) | + 124 | + 45 | + 24 |
| Bluetooth MIDI devices too (`ble_central`) | + 8 | + 1 | 0 |
| Bluetooth MIDI with a computer on the own stack (`ble_stack = "own"`) | + 41 to 46 | + 10 | + 1 |
| Pairing, devices and Bluetooth audio too (`ble_pairing`, `ble_central`, `bt_audio`) | + 57 to 62 | + 8 | + 30 |
| WiFi (`wifi`, from fm1-radio's measurements) | + about 300 | + about 100 | + about 276 |

- Bluetooth: JieLi's BT controller and stack (the flash), its 24 KB heap and their tables (the RAM), three task
  stacks of 8 KB (the pool). The template with Bluetooth and devices is 214 KB of flash, 75 of 96 KB of RAM: room
  for a big app (CTL-1, with a General MIDI synth, is 491 KB).
- The own stack: the template with Bluetooth MIDI alone is 118 to 123 KB of flash, 37 KB of RAM and 69 KB of pool;
  with pairing, devices and audio, 180 KB, 45 KB and 99 KB. On a big app the
  difference shows: LOOM-8 on JieLi's stack is 506 KB of flash, 88 KB of RAM and 327 KB of pool; on the own stack
  with MIDI alone, 454 KB, 68 KB and 318 KB; with pairing, devices, audio and its pages for them, 512 KB, 91 KB and
  328 KB.
- WiFi: JieLi's driver (146 KB), the WPA supplicant (77 KB), lwIP (51 KB), the radio layer, crypto and the shim
  (25 KB) in the flash; the driver's buffers, lwIP, the shim's task table and 24 KB of the heap (about 100 KB) in
  the RAM; the rest of the heap (248 KB) and lwIP's packet buffers (28 KB) in the pool. The heap, 272 KB, holds the
  WiFi library's own buffers (about 60 KB) and its tasks' stacks (4 to 8 KB each, by task, as a task starts: about
  6), and what is left is the app's (`malloc`; fm1-radio's HE-AAC player and HTTPS stream peak at 256 KB of it).
  That leaves an app about 170 KB of flash and a few KB of RAM.
  WiFi calibrates its radio at every start (a kept calibration was Bluetooth's, which the access point could not
  hear). `examples/wifi` is 420 KB of
  flash, 119 of 128 KB of RAM and 299 of 304 KB of pool; fm1-radio (MP3 and HE-AAC decoders, HTTPS) is
  576 KB, 127 KB and 300 KB.
- In a WiFi build, fm1-base's large font (the splash's name, the panel's screens) can go too:
  `#define APP_BIG_FONT FONT_M` in app.h saves 20 KB of flash.
- An app with fonts of its own can give them to fm1-base's screens (crash, safe mode, update, restore,
  calibration, the splash): `#define APP_SYS_TEXT` in app.h and its `app_sys_h`, `app_sys_w`, `app_sys_text`
  (`device/sys_text.c`); fm1-base's faces are then left out (LOOM-8: 18 KB).

## Development over USB

`tools/fm1_remote.py` drives an FM-1 running any fm1-base firmware as a player would: buttons, keys, encoders,
MASTER, and reads back what the screen says (the screen reader's channel), the last crash and the radio's log; with
WiFi, it turns it on and off, joins networks and reads its state (`wifi ...`).
For example, `tools/fm1_remote.py listen tap REC wait 3000 screen log`.
It finds the FM-1 as the web pages do (`tools/fm1ports.py`, the same rules as `web/fm1ports.js`): a port with an
FM-1's usual name, or else one that answers the screen reader's HELLO, whatever it is called; `--port NAME` picks one.
`tools/fm1_install.py`, `fm1_backup.py`, `fm1_mirror.py` and `fm1_debug.py` find it the same way.

The site's installer page (`tools/make_site.py`, from `web/installer.html`) reads the same crash record and log in
Chrome or Edge: "Report a problem" shows them, ready to copy, so a player can send them without Python. The record
survives a restart, not switching off.

The pages find the FM-1 with `web/fm1ports.js`, under any port name: a port with one of the FM-1's usual names, one
the player picked (Try), or one that answers the screen reader's HELLO, which every fm1-base firmware does. Only the
HELLO goes to other ports, three times: its ID, 7D, is the one for non-commercial use, which other gear ignores.
The [FM-1 Screen Reader](https://github.com/charlesvestal/fm1-reader)'s page uses the same file, as a copy.

The screen reader's firmware side lives here too: `device/announce.c` and `tools/gen_words.py` (MPL-2.0, like the
rest; see LICENSING.md) are the one copy. fm1-reader keeps the page, the voice, the protocol (its PROTOCOL.md), the audit and the skill, and its
`tools/sync_announce.sh` copies these two files into the firmwares not on fm1-base.

## .rfm1: your firmware in host apps

An `.rfm1` file is your firmware built to run inside an app instead of on the FM-1: RemoteFM1 for iOS first, as
an instrument or an AUv3. It is the host simulator compiled to WebAssembly, the same app as on the FM-1, with a
manifest (name, version, licence, the source at its commit), your credits and the licence texts. Users download it
from your release and put it in the app's firmware folder. The format is RemoteFM1IOS's
[docs/RFM1-FORMAT.md](https://github.com/Quixotic7/RemoteFM1IOS/blob/main/docs/RFM1-FORMAT.md).

    tools/build.py APP_DIR --rfm1                    # APP_DIR/build/rfm1/ID-dev+COMMIT.rfm1 (emcc, as --web)
    tools/build.py APP_DIR --rfm1 --release X.Y      # APP_DIR/build/NAME-X.Y.rfm1, beside NAME-X.Y.fwsc

The ID is fm1.toml's `name` in lower case. Without `--release` the version is `dev+` and the app repository's commit.
The template's release workflow builds the release's `.rfm1` and attests it with the package.
The manifest's `source_url` is fm1.toml's `repo` at that commit, so commit (and push) what you build. Its `license`
is fm1.toml's `licence`, and its authors are `authors = ["..."]` or the repository's first author. The memory is fixed
at 8 MiB (`rfm1_memory_mib`, at most 32). The build checks the module against the format before it writes the file.

In the app, the panel, screen, LEDs, sound, MIDI both ways, velocity and pressure (`APP_TOUCH`) and the screen
reader (over SysEx) work as in the browser emulator. Saved objects and the data area are kept by the app with its
state. There is no radio, no USB host and no environment (`getenv` returns nothing). `tests/rfm1_test.mjs FILE`
drives a file as a host app does.

## firmware.json: your firmware in hubs

Every build writes `firmware.json`, a description of the firmware for the places that list and install FM-1
firmwares (The Firmware Hub, RemoteFM1, M-Flasher): its name, author, licence, a paragraph on it, tags, the install
page, and the hardware it needs (the flash parts it saves to, the parts the build has, such as Bluetooth MIDI or USB
audio). It comes from fm1.toml (`author`, `description`, `tags`, `install`, `site`, with the site's `tagline`, `repo`,
`licence` and `credits`) and lands in `APP_DIR/build/` and, for an app in `app/`, at the repository's root, to commit.
The format, its fields and its vocabularies are in [docs/FIRMWARE-JSON.md](docs/FIRMWARE-JSON.md).

## Moving a Felucca-based firmware here

A Felucca-based firmware (GPL-3.0) can move onto fm1-base and felucca-bsp and keep its licence: their MPL-2.0 files
may be part of a GPL work, and stay MPL-2.0 file by file. It then has the platform's parts and fixes with every other
firmware, instead of its own copy of Felucca. `skill/felucca-to-fm1-base/` is a Claude skill that walks through the
move: the inventory (what is platform, what is the app), what fm1-base needs, the port, the checks (the app's
scenarios compared byte for byte with the old simulator's, then the FM-1), and the licensing files. It never changes
the firmware's licence: it asks. Relicensing to MPL-2.0 is needed only to link JieLi's Bluetooth or WiFi libraries;
`skill/felucca-to-fm1-base/RULES.md` has what it takes (`tools/provenance.py` finds Felucca's lines and whose they
are). To move a firmware with Claude Code, run this from inside its repository (Node.js's `npx` fetches the skill
into the project):

    npx degit --force charlesvestal/fm1-base/skill/felucca-to-fm1-base .claude/skills/felucca-to-fm1-base && claude "Move this firmware onto fm1-base with the felucca-to-fm1-base skill"

Claude starts with the inventory and shows it before changing anything. To have the skill in every project instead,
fetch it to `~/.claude/skills/felucca-to-fm1-base`. It reads as plain instructions too, without Claude.
