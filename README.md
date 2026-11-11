# Mutable Dreams 2.0 for the M-VAVE FM-1

Mutable Dreams is an independent six-track instrument for generative ambient music, evolving soundscapes and rhythms, made by Kid Bunka.

- Explore 272 factory Biome entries including Blank and 632 tone presets. Mix synth voices, lo-fi kits and hand percussion with shared effects and the granular bus.
- Record and edit sequences, build chord progressions, and shape movement with Living Links and Biome Drift.
- Use Journey to arrange up to eight sections. Chapel of Fog, Pearl Fern and Copper Step start with a Journey loop. The other factory Biomes start with Journey Off. Saved User Biomes keep their chosen mode.
- Map external MIDI controls with MIDI Learn and mirror the actual FM-1 display over USB.
- Record the stereo mix and six individual mono voices over USB at 44.1 kHz.

See [BUILDING.md](BUILDING.md) for source builds and [LICENSING-GPL.txt](LICENSING-GPL.txt) for licensing and third-party notices. This snapshot targets the FM-1. SMK-37 firmware is a separate build target and is currently unavailable.

## Installation and recovery

Export your saved User Banks as an `.mddb` file and any unsaved live Biome separately. The browser installer and Python USB-MIDI updater support the Mutable Dreams package. The manufacturer's M-UPGRADE utility accepts official stock files. A return to stock can erase User Banks and settings. After reinstalling Mutable Dreams, the editor can restore the bank file when you confirm with SEL on the device.

The prior 2.0 candidate was exercised on the available FM-1 with the published 1.5 and stock V15 and V16. Build 4442417D adds the three factory arrangements. Offline flash and update tests cover JEDEC `B36014`. Installation, a save and cold start, and the stock recovery round trip with this exact build on a UC25HQ80 device are still awaiting confirmation.

Thanks to the players who tested builds and reported issues, and to the authors of the open-source DSP and platform code retained with their original notices.

[Project website](https://kidbee85-beep.github.io/mutable-dreams/) · [Illustrated walkthrough](https://kidbee85-beep.github.io/mutable-dreams/Mutable-Dreams-2.0-Walkthrough.pdf)
