# fm1-base: design

A base for M-VAVE FM-1 firmware, on felucca-bsp. A firmware is felucca-bsp (chip, board, update loader, packaging),
plus fm1-base (everything a firmware needs above the hardware), plus its own app. It is the platform layer that
FoMni-1, X0X, CTL-1 and the other firmwares share today, made into one maintained base.

## Licence

**Every file of fm1-base is MPL-2.0**, with a provenance that allows it:

| Source | Why it may be MPL-2.0 |
| --- | --- |
| Leo Kuroshita's code from Felucca | his permission, recorded in hugelton/felucca-bsp#4; his name in `CREDITS.md` |
| Charles Vestal's code (the firmwares' platform layer: X0X, FoMni and the rest) | his own |
| keremimo's code | only what he has agreed to on the record (felucca-bsp#3; the second-core start once its PR has his comment; Melodee's code, 2026-10-31: LICENSING.md) |

Nothing else goes in: no code by other Felucca contributors, no GPL code from anywhere. Every imported file's history
is checked first (its authors in git and, for files from Felucca, Felucca's README credits). Third-party material keeps its
own licence and is listed in `LICENSING.md`: the fonts (SIL OFL 1.1), JieLi's SDK files used by the build (Apache-2.0,
fetched, never committed).

An app built on fm1-base may use any licence. MPL-2.0 applies file by file: changes to fm1-base's files are published
under MPL-2.0, the app's own files are the app's choice. A firmware that links JieLi's closed Bluetooth or Wi-Fi
libraries therefore has no GPL file in it at all.

## Layers

```
app (ui.c, project.c, dsp/ ...)            the firmware's own code, any licence
  |  plat.h (the platform interface)
fm1-base  device: plat_fm1.c, main_fm1.c, panel.c, lcd.c, gfx.c, storage.c, midi_uart.c, announce.c
          host:   plat_host.c (simulator, scripted), web/emu (the simulator in the browser)
          tools:  build.py, fonts, words, installer and site pages
felucca-bsp (git submodule, pinned)  chip/ boards/fm1/ loader/ tools/fm1pkg.py tools/fm1_install.py
```

The app sees only `plat.h`. The device and the host implement it, so every app runs, unchanged, on the FM-1, in the
host simulator (tests, screenshots) and in the browser.

## The platform interface

`plat.h` is the union of what the 13 firmwares use today (68 functions), in groups. A firmware turns on the optional
groups it needs, and only those are compiled in.

| Group | Functions (today's names) | Used by |
| --- | --- | --- |
| core | `plat_ms`, `plat_buttons`, `plat_keys`, `plat_enc`, `plat_master`, `plat_leds`, `plat_glow`, `plat_play_red`, `plat_keys_dim`, `plat_store_load/save`, `plat_cpu_pct`, `plat_xruns` | all |
| MIDI | `plat_midi_in` (USB or TRS), `plat_midi_out`, `plat_midi3`, `plat_sysex`, `plat_midi_out_room`, `plat_midi_out_link`, `plat_midi_dropped`, SysEx in (`plat_sysex_get/done/send`) | all; the extras by some |
| storage extras | `plat_store_load_prev`, `plat_store_head`, raw sectors (`plat_gal_*`) | X0X, SPH-16, XY-1, Polly, Mocky |
| second core | `plat_cpu2_*` | X0X, NuSaw, SPH-16, XY-1 |
| analog keys | `plat_analog_*`, `plat_cap_probe`, `plat_sense` | Velo, MONK-1, CTL-1 |
| profiling | `plat_cycles*`, `plat_stalls*`, `plat_cpu_peak_pct`, `plat_irq_stack_*`, `plat_ticks24` | X0X, SPH-16, XY-1 |
| power | `plat_battery`, `plat_vdc14_mv`, `plat_sysvdd_mv` | X0X, SPH-16, NuSaw, XY-1 |
| crash | `plat_last_crash` | X0X, SPH-16, NuSaw, XY-1 |
| Bluetooth | `plat_ble_*` | CTL-1 |
| concurrency | `plat_lock`, `plat_unlock`, `plat_yield`, `plat_idle` | CTL-1, Mocky |

The names stay as they are, so moving a firmware is a change of include paths, not of code. A firmware's own oddities
(X0X's sample slots) stay in that firmware.

The app provides the other side: `app_init`, `app_frame` (the UI, about 60 a second), `app_render` (the audio,
called by the platform's audio interrupt), `app_tick` (1 kHz, optional), `app_say` (the screen reader), and
`app_config`: the block size, the second core, the USB names, the store's objects.

## Build

One `tools/build.py` for every firmware, driven by the firmware's `fm1.toml`: the name, the package identity
(`FM-1_` + digits), the USB names (felucca-bsp's `FELUCCA_USB_MANUFACTURER` / `FELUCCA_USB_PRODUCT`), the app's
sources, the optional groups, the fonts. It builds the app and the update loader, checks the rules (no `double`, no
hardware access outside the BSP and fm1-base's device files), and packs the `.fwsc` with felucca-bsp's `fm1pkg.py`.
`tools/run_tests.sh` builds the host simulator and runs the app's scripted tests. The web pages (installer, emulator,
backup) are generated from templates with the firmware's name and words file.

The FM-1 tells its identity to the update handshake (felucca-bsp `chip/ota.c`, cmd 0x11). M-VAVE's M-UPGRADE 3.11
takes `NAME_` and one to three digits only, so the plain handshake gets the name, `_` and the first three digits
(`FM-1_11000100` -> `FM-1_110`); the handshake with the one-byte body 01 gets the whole identity. The installers
(`tools/fm1_install.py`, `web/fm1ota.js`, the backup page) ask that way when the plain answer is `NAME_` and three
digits, and keep the plain answer when nothing answers (M-VAVE's firmware, the loaders, older builds). The scheme is
PERF-1's (deamonSLINKY/PERF-1), so its tools and ours read each other's firmwares.

## What fm1-base ships

- `template/`: a minimal firmware to copy: a screen with text, the keys playing a sine voice, MASTER as the volume,
  USB-MIDI in and out, a saved setting, the screen reader. It builds, installs, updates and runs in the simulator.
- `examples/`: one small program per subject: the screen and fonts, keys and LEDs, audio, MIDI over USB and TRS,
  storage, the second core, and later Bluetooth MIDI.
- `docs/`: the interface, the build, porting a firmware, the licence.

## Order of work

1. The repository: felucca-bsp as a submodule; FoMni's platform layer, the host simulator, the web emulator and the
   tools brought in, each file checked for its authors, MPL-2.0 headers, `CREDITS.md`, `LICENSING.md`.
2. The template, flashed and tested on an FM-1: boot, screen, keys, audio, MIDI, save, reboot, update over USB. This is
   also the device test of felucca-bsp's latest changes (the supplies, the second core held on reset, the USB names).
3. hwtest gets an update path from the template's hooks; the real hwtest is run on a device; the second-core PR is tested.
4. CTL-1 moves onto fm1-base, with its Bluetooth build, which can then be released.
5. The other firmwares, one at a time; the optional groups come in with the first firmware that needs each.
6. After CTL-1's move: a skill, "move a Felucca fork onto felucca-bsp and fm1-base", written from what that move took.
