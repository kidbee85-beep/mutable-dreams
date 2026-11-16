# Mutable Dreams 2.3 — Bloom Studies

Release · build **CF133B2D** · FM-1

Twelve new Tones, four separately composed Biomes, and two dreams with details that follow the music. The original Mutable Dreams design, sound models and six freely assignable tracks remain at the centre.

## Sound

- **12 new Tones** on six existing models: Satin, Silk, Choir, Bowed, Aero and Mellow. The library now holds **652 Tones across 24 models**. These are new starting points, not twelve new engines.
- **Four Bloom Studies:** Petals After Rain (58 BPM, D Dorian), Willow Waltz (87 BPM, A minor), Blue Hour Bloom (46 BPM, F major) and Lanterns Upstream (102 BPM, G Dorian).
- **280 Biome entries, including Blank.** The previous 640 Tones and 276 Biome entries keep their parameters, names, positions and IDs.
- All six tracks remain freely assignable. Recalling a Tone keeps the track's pattern, notes and mixer settings.

## Dreams and living graphics

- **Terra Pluvia:** real note events create track-specific rings in six positions on the river. Pitch class moves the landing position slightly. The falling rain remains ambient scenery.
- **Aurora Polaris:** six tracks contribute to three paired ribbons. Register changes lift the ribbons gradually; note events light short curtains at their pitch. The stars remain ambient.
- Living graphics' held peaks now fall according to elapsed time, so a slower screen update does not make them linger longer.
- The original palette, geometry, screen update rate and motion-off option remain. Very close notes on the same track may merge into one visual impulse. Audio is never delayed for the picture.

## MIDI and accessibility

- With an external clock selected, pressing PLAY after a timeout gives a late clock a fresh two-second waiting period. The firmware does not silently switch to internal tempo. External Stop releases the notes.
- Broader Screen Reader announcements cover pages, tracks, controls, Freeze, octave changes and MIDI output-channel values, alongside contextual long-press help.
- Itinera still uses its internal clock. External dream synchronisation remains an open request. Reader usability still needs confirmation from actual Screen Reader users; issue #10 stays open.

## Back up before updating

**Updating can erase Favorites and personal / User Biomes.** Export all device banks as `.mddb`, export your edited live Biome separately, and note or photograph Favorites and Tone stars before installation. Restore and check your work afterwards.

**Beim Update können Favoriten und persönliche Biomes verloren gehen.** Sichere die User-Bänke und das bearbeitete Live-Biome getrennt. Notiere zusätzlich Favoriten und Tone-Sterne und prüfe die Wiederherstellung nach dem Update.

Saved-project format 12 is unchanged. Newly added factory sounds are made portable when saved: their parameters remain, but their new factory names can appear as “User” labels for backwards compatibility. Live List does not overwrite factory Biomes; issue #4 remains open.

## Qualification and limits

The exact **CF133B2D** build passed three 180-second stress cases with eight-channel USB audio, MIDI, mirroring and Reader checks on one Puya-chip FM-1. The lowest measured reserve was **12.61%** (5072 of 5804 microseconds per audio block), with no added audio overruns, dropped events, USB errors or quality reductions in those cases. A separate 180-second FULL test of Lanterns Upstream measured 18.57% reserve. These are measured results, not a guarantee for every patch.

Direct update and return passed between 2.3 and Mutable Dreams 1.5, 2.0, 2.1, public 2.2 and the previous private Reader build. The separate **original M-VAVE V15** round trip also passed. V15 uses a different storage format and required backup restoration, confirmed with SEL on the hardware. After restoration and an application restart, all 22 original saved objects and the unsaved live project matched their backups exactly. Hardware routes used the USB-MIDI update tools; the browser installer has separate simulated route checks.

Not repeated or not available in this round: a fully power-disconnected cold start, exact UC25HQ80 hardware qualification, an analog Line-Out measurement, Logic Pro, and a four-hour physical soak. The long journey run was accelerated host simulation. USB MIDI was captured directly rather than tested in Logic's interface.

**m-bootloader is separate and optional**, by Julius Vitkauskas (MIT). It is not silently installed with this application update. Version 2.4 has not been qualified on UC25HQ80 (JEDEC 0xB36014). Check the actual flash identity; a logo or purchase date does not identify the chip. This build adds neither Shimmer nor second-core processing.

## Learn, play and stay in touch

- [Project and downloads](https://kidbee85-beep.github.io/mutable-dreams/)
- [Web Studio](Mutable-Dreams-2.3.html) · [Complete illustrated manual](Mutable-Dreams-2.3-Manual.pdf) · [Quickstart](Mutable-Dreams-2.3-Quickstart.pdf)
- [Mutable Dreams FM](https://kidbee85-beep.github.io/mutable-dreams/radio.html)
- [Instagram @mutabledreams](https://www.instagram.com/mutabledreams/) · [YouTube / Kid Bunka](https://www.youtube.com/@kidbunka)

Special thanks to [Yimiblú](https://www.instagram.com/yimiblu_lofi_ambient/) for detailed MIDI reports and music-linked animation ideas, and to the Screen Reader users and developers who keep testing with us.

Mutable Dreams is free and open source. [Voluntary support](https://www.paypal.com/donate/?business=kidbee85%40gmail.com&item_name=Mutable%20Dreams%20development) goes entirely into firmware updates, new firmware and synthesizer software.

Package identity: `FM-1_710020300`  
SHA-256: `c70c4371a4287ecbf68b1bb6f53d8d519f968276417ec94a058a0051c6b1ca96`
