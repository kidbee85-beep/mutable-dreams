# Mutable Dreams 2.4

An open-source musical garden for the M-VAVE FM-1, created by Kid Bunka. Six freely assignable tracks, 24 sound models, 664 Tones, 300 factory entries including Blank and 25 Dreams.

Qualified application build **8FBE5AF8**, identity **FM-1_710020400**. Package SHA-256: `0923a0e1bb05f8b0fc6111e4274be38436e37a2a66790a58d12bf60bb7a2e004`.

[Website and installer](https://kidbee85-beep.github.io/mutable-dreams/) · [Full release notes](Release-Notes-2.4.md) · [2.4 manual](https://kidbee85-beep.github.io/mutable-dreams/Mutable-Dreams-2.4-Manual.pdf)

[2.4 feature film](https://youtu.be/8P0lTBVRz0g) · [YouTube Short](https://youtube.com/shorts/KZrkoWiPnmA) · [Instagram Reel](https://www.instagram.com/mutabledreams/reel/DgGGRqByh3i/) · [TikTok](https://www.tiktok.com/@kid.bunka/video/7711447337851227414)

With special thanks to **Sarmiento (Funkelectric)**, Yimiblú, Screen Reader testers and all our helpers.

## New in 2.4

Twelve warm Tones, twenty Biomes, eleven additional Dreams, Grain Size/Position for both LFOs, bidirectional menu navigation, expanded hold help, optional character effects and both-core audio work. [Read the exact changes and qualification](Release-Notes-2.4.md). [Build the supplied source](BUILDING-2.4.md).

## Retained from 2.3

- Twelve new Tones across Satin, Silk, Choir, Bowed, Aero and Mellow. Choose any sound on any track while keeping its notes and rhythm.
- Four composed Bloom Studies: **Petals After Rain**, **Willow Waltz**, **Blue Hour Bloom** and **Lanterns Upstream**.
- Music-linked river rings in **Terra Pluvia** and note-lit ribbons in **Aurora Polaris**. The original design stays; audio is never delayed for the graphics.
- More consistent living-meter decay, a fresh external-clock wait after a timeout, and broader Screen Reader announcements.

The 2.2 performance views, Live List, Tone stars, live Itinera macros, timed Biome changes, generated six-channel USB MIDI notes and eight-channel USB audio remain available. The previous 640 Tones and 276 Biome entries keep their parameters and IDs. Reverb and delay are existing effects; 2.3 does not add Shimmer or second-core processing. Itinera still uses internal clock.

## Back up before updating

**Updates can erase Favorites and personal / User Biomes.** Export all banks as `.mddb`, export an edited live Biome separately and note your Favorites and Tone stars. Restore and check afterwards. Original M-VAVE V15 has a different saved-data format; it needs the documented bank restoration.

## Historical 2.3 qualification

The following figures and routes apply to 2.3 only; read [2.4 qualification](Release-Notes-2.4.md#qualified-test-scope) for the current build.

Tested build **CF133B2D**, package identity **FM-1_710020300**. Package SHA-256:

`c70c4371a4287ecbf68b1bb6f53d8d519f968276417ec94a058a0051c6b1ca96`

Measured minimum reserve: **12.61%** in three specified 180-second stress cases on one Puya-chip FM-1, with USB audio, MIDI, mirroring and Reader checks. Direct update/return routes include Mutable Dreams 1.5, 2.0, 2.1, 2.2 and original V15. See the [release notes](Release-Notes-2.3.md) for the exact scope and limits. UC25HQ80 qualification is now verified in emulation by [fm1-chiptest](https://github.com/kidbee85-beep/fm1-chiptest) — boot, save and reboot-persistence all pass on both flash chips — while physical validation on a real UC25HQ80 unit remains open. Blind-user Reader usability remains open. m-bootloader is separate and optional, not silently installed by this application update.

## Explore the wider garden

[Performance Garden / VIBE FM](https://kidbee85-beep.github.io/mutable-dreams/#performance-garden) · [MPC product preview](https://kidbee85-beep.github.io/mutable-dreams/mpc.html) · [Handheld and SMK projects](https://kidbee85-beep.github.io/mutable-dreams/#workbench-title)

Each edition has its own compatibility notes and installer. The FM-1 package is not for MPC, SMK or handheld devices.

[Mutable Dreams FM](https://kidbee85-beep.github.io/mutable-dreams/radio.html) · [TuneIn](https://tunein.com/radio/Mutable-Dreams-FM-s364904/) · [YouTube](https://www.youtube.com/@kidbunka) · [Instagram](https://www.instagram.com/mutabledreams/) · [Feedback](https://github.com/kidbee85-beep/mutable-dreams/issues)

Special thanks to [Yimiblú](https://www.instagram.com/yimiblu_lofi_ambient/), [Sarmiento](https://github.com/Funkelectric) for sharing the real-time DSP techniques, our Screen Reader testers and everyone sharing ideas and music. [Optional support](https://www.paypal.com/donate/?business=kidbee85%40gmail.com&item_name=Mutable%20Dreams%20development) goes entirely into updates, firmware and synthesizer-software development.

## Source and credits

See [BUILDING-2.4.md](BUILDING-2.4.md), the corresponding source archive in the [2.4 release](https://github.com/kidbee85-beep/mutable-dreams/releases/tag/v2.4), `LICENSE`, `LICENSE-MIT.txt` and `LICENSING-GPL.txt`. The combined application is GPL-3.0, selected original DSP retains MIT notices, and the platform retains MPL-2.0 notices. Mutable Dreams is independent of M-VAVE and Mutable Instruments.
