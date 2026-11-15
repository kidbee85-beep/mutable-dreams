# Mutable Dreams 2.2 · Played live

An independent, open-source instrument for the **M-VAVE FM-1**, by **Kid Bunka**.

**Six freely assignable tracks · 275 musical Biomes + Blank · 640 Tones · 24 sound models · 14 Itinera dreams**

[Start here](https://kidbee85-beep.github.io/mutable-dreams/Start-here.html) · [Play the Web Studio](https://kidbee85-beep.github.io/mutable-dreams/Mutable-Dreams-2.2.html) · [Complete manual](https://kidbee85-beep.github.io/mutable-dreams/Manual.html) · [62-page PDF](https://kidbee85-beep.github.io/mutable-dreams/Mutable-Dreams-2.2-Manual.pdf) · [Installer](https://kidbee85-beep.github.io/mutable-dreams/Mutable-Dreams-2.2-Install.html)

## New ways to play

- **HOME Live List:** 16 ordered references to your own saved or factory Biomes, plus permanent Blank access. Name-based Tone browsing and separate Tone stars.
- **HOME mixer and Voice Detail:** selected-track level, grain and room sends, Touch on supported voices, and a larger sound-detail view. Menus remember their pages; hold a menu key and turn SELECT to use its page wheel.
- **Musical Biome changes:** Now, Next Bar or Track by Track. KEEP SETTING carries root, scale, transpose, tempo and chords into the next Biome.
- **Play inside Itinera:** the 14 existing dreams gain live Density, Motion, Colour and Space offsets. SEL keeps the current Biome playing; ENV restores the project from before the dream.
- **Darker saw colours:** Velour Nightfall, Saw Cathedral, Ember Tape Dusk and Deep Saw Tide, with eight additional Satin Morph Tones.
- **Generated USB MIDI notes:** choose an output channel for each synth track. This release sends generated monophonic synth notes, not kit notes, incoming notes, MIDI clock or BLE/TRS output.
- **Stability and headroom:** corrected stopped-save timing with mirroring, MIDI backpressure/note-off handling and lower effect-loop overhead. Full scope is in the [release notes](Release-Notes-2.2.md).

All previous factory Biomes remain available. A Live List entry is a reference: save edited music to User Banks first. Factory ROM is not overwritten by saving a personal Biome. Journey Arrange is optional and follows each Biome's settings; ordinary generative playback remains available.

## Install, back up and return

Export saved banks as `.mddb` and an edited live Biome separately before an update. Use the matching installer with desktop Chrome or Edge. Keep the prior firmware and both backups for the return path. An `.mddb` is a Mutable Dreams backup, not a universal backup for other FM-1 firmware.

Tested binary: **23F2FA0E**, identity **FM-1_710020200**. Package SHA-256:

`25df8be7b5d87806739e3481f0d8af371762e29051e4153cd86ce5bdae882b68`

Six-track FULL/dense ECO tests with effects, MIDI and mirroring added no XRuns or USB overflows. The lowest measured ECO reserve was **12.30% in the specified tests**, not a guarantee for every patch. The owner accepted cold-start and listening tests on 15 November 2026. Exact UC25HQ80 hardware validation remains open. See [the measured scope](Release-Notes-2.2.md) before installing.

This package is **FM-1 only**. SMK-37 Elite/Pro and the handheld project have separate development and hardware validation. The [MPC product preview](https://kidbee85-beep.github.io/mutable-dreams/mpc.html) and [Performance Garden for VIBE FM](https://kidbee85-beep.github.io/mutable-dreams/#performance-garden) have their own downloads and compatibility notes.

## Listen and keep in touch

[Mutable Dreams FM radio](https://kidbee85-beep.github.io/mutable-dreams/radio.html) · [YouTube / Kid Bunka](https://www.youtube.com/@kidbunka) · [Instagram / Mutable Dreams](https://www.instagram.com/mutabledreams/) · [Feedback and issues](https://github.com/kidbee85-beep/mutable-dreams/issues)

[Support development](https://www.paypal.com/donate/?business=kidbee85%40gmail.com&item_name=Mutable%20Dreams%20development). Voluntary donations go entirely into firmware updates, new firmware and synthesizer-software projects. Thank you for playing, testing and helping this garden grow.

## Source and credits

Source use follows the included licenses: GPL-3.0 for the combined application, with original notices retained for selected MIT DSP and the MPL-2.0 platform. See `LICENSE`, `LICENSING-GPL.txt` and `BUILDING.md`. With thanks to the original DSP and platform contributors and to everyone who shared feedback. Mutable Dreams is independent of and not endorsed by M-VAVE or Mutable Instruments.

Direct installation from V15 and published 2.1, plus both return paths, was verified with the exact final package. A V15 return requires the saved-bank backup and separate live export; all 22 objects and the live project were restored exactly after physical SEL confirmation.
