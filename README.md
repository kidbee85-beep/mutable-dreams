# Mutable Dreams 1.5

A free, independent instrument for the M-VAVE FM-1 by **Kid Bunka**.

**168 musical Biomes + Blank · 364 tone presets · BLE MIDI reception · six freely assigned synth tracks, or a Lo-Fi kit plus three synths.**

[Play and edit in your browser](https://kidbee85-beep.github.io/mutable-dreams/) · [Install / return to V15](https://kidbee85-beep.github.io/mutable-dreams/Mutable-Dreams-1.5-Install.html) · [Illustrated update guide](https://kidbee85-beep.github.io/mutable-dreams/Mutable-Dreams-1.5-Guide.pdf) · [Release downloads](https://github.com/kidbee85-beep/mutable-dreams/releases/tag/v1.5)

Version 1.5 adds twelve contrasting Biomes and twelve tones, a Wireless MIDI page, brighter organic display families, corrected echo bounds and an updater restart fix. Thank you for the community feedback that shaped these changes.

## Wireless MIDI

An external BLE central, such as a Mac, can connect to the FM-1 service and send notes, controllers and transport/clock. Notes, CC74 and Start/Clock/Stop were tested on a physical FM-1, including reconnection after cold start. One BLE connection is supported. The opposite connection role, where the FM-1 links to a separate BLE peripheral, is experimental and still needs a controller test. USB and TRS input remain; physical TRS and BLE jitter measurements are not recorded in this cycle. Keyboard/sequencer MIDI output and arbitrary CC Learn are not new features of this release.

## Update and recovery

Back up .mddb User Banks and export unsaved live JSON before installation. Use our browser or Python installer; official M-UPGRADE rejects custom Mutable Dreams packages. Read [recovery instructions](docs/release-1.5/Recovery-1.5.txt). The tested package identifies as `FM-1_710010500`; SHA-256 `ee5d410fc13b9861baa2336303821ccf66ec8c7143c167649886fad368198b52`.

Mutable Dreams does not import projects or sample banks saved by stock firmware or other custom firmwares. Switching firmware can replace their storage formats; export the relevant project's backup first and keep its matching firmware/restore tool. A Mutable Dreams .mddb backup is for Mutable Dreams, not a universal FM-1 backup.

The UC25HQ80 flash ID is included, but physical installation on that newer board is still unverified. Do not confuse static support with a tested update. This release is FM-1 only; an SMK-37 build is not included.

## Source and credits

All source is browsable in this repository and at tag v1.5. See [BUILDING.md](BUILDING.md), [release notes](docs/release-1.5/Release-Notes-1.5.md), [licensing](LICENSING-GPL.txt) and [development evidence](DEVELOPMENT-NOTES-BLE.md). Earlier release tags and downloads remain available.

With gratitude to Émilie Gillet, Mutable Instruments, fm1-base and felucca-bsp contributors. Selected algorithms retain their original notices; other module-inspired features are independent interpretations. Mutable Dreams is unaffiliated with and not endorsed by Mutable Instruments or M-VAVE.

