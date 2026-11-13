# Mutable Dreams 2.1 for the M-VAVE FM-1

Mutable Dreams is an independent six-track instrument for generative ambient music, evolving soundscapes and rhythms, made by Kid Bunka.

- Start one of 14 Itinera dreams and let it travel through many Biomes for 30 minutes up to four hours, with smooth transitions and a moving picture for every dream.
- Explore 272 factory Biome entries including Blank and 632 tone presets. Mix synth voices, lo-fi kits and hand percussion with shared effects and the granular bus.
- Record and edit sequences, build chord progressions, and shape movement with Living Links and Biome Drift.
- Use Journey to arrange up to eight sections. Chapel of Fog, Pearl Fern and Copper Step start with a Journey loop. The other factory Biomes start with Journey Off. Saved User Biomes keep their chosen mode.
- Map external MIDI controls with MIDI Learn and mirror the actual FM-1 display over USB.
- Record the stereo mix and six individual mono voices over USB at 44.1 kHz.

See [BUILDING.md](BUILDING.md) for source builds and [LICENSING-GPL.txt](LICENSING-GPL.txt) for licensing and third-party notices. This snapshot targets the FM-1. SMK-37 firmware is a separate build target and is currently unavailable.

## Installation and recovery

Export your saved User Banks as an `.mddb` file and any unsaved live Biome separately. The browser installer and Python USB-MIDI updater support the Mutable Dreams package. The manufacturer's M-UPGRADE utility accepts official stock files. A return to stock can erase User Banks and settings. After reinstalling Mutable Dreams, the editor can restore the bank file when you confirm with SEL on the device.

Build 2467CC38 keeps the 2.0 project and bank format. Build 2467CC38 was tested on an FM-1 via USB-MIDI in both directions with Mutable Dreams 2.0 and with M-VAVE stock V15. The saved banks and live Biome matched their backups byte for byte after the V15 recovery. Mare Nebularum completed a scene transition in a two-minute test with eight USB audio channels and screen mirroring, with zero measured dropouts. A physical four-hour run, a separate stock V16 round trip and validation on UC25HQ80 (B36014) remain open for this exact build.

Thanks to the players who tested builds and reported issues, and to the authors of the open-source DSP and platform code retained with their original notices.

[Project website](https://kidbee85-beep.github.io/mutable-dreams/) · [Illustrated walkthrough](https://kidbee85-beep.github.io/mutable-dreams/Mutable-Dreams-2.1-Walkthrough.pdf)
