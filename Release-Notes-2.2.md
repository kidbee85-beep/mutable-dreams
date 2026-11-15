# Mutable Dreams 2.2 — Played live

Owner-approved release, 15 November 2026. The exact final firmware is retained without recompilation.

Mutable Dreams 2.2 brings more of the instrument within reach while you play: a personal Live List, Tone stars, a HOME mixer, clearer performance views and musical Biome changes. The six-track generative instrument remains familiar, with 275 musical Biomes plus Blank, 640 Tones and 24 sound models.

## Keep your favourites close

- **Live List:** arrange up to 16 Biome references for a performance. Choose with K2 or PRESET, load with SEL and manage the current entry with REC. Blank remains directly accessible at position 00. Save sound edits to a User Biome before relying on a list reference.
- **Tone stars:** browse all 640 Tones by name and filter your starred sounds. Tone stars and Biome favourites are separate. Stop playback and SAVE to keep your preferences.
- **HOME mixer:** adjust the selected track's level, Grain and Room with K1–K3, alongside the six-track overview.
- **Voice Detail and Biome Stage:** focus on one voice or give the living Biome display the whole stage.
- **Page memory and Page Wheel:** return to the page you were using. Hold EDIT, SEQ, FX or GLO and turn SELECT to choose a page; ordinary SELECT still chooses the track.

## Move between sound worlds

- Choose **Now**, **Next Bar** or **Track by Track** for Biome changes. The musical change modes preserve effect tails. If an intermediate combination would be too demanding, tracks can change together at the bar boundary.
- **KEEP SETTING** retains root, scale, transpose, tempo and chords during manual Biome selection. The destination still supplies its voices, rhythms and Space.
- Four new dark Satin-Morph Biomes: **Velour Nightfall, Saw Cathedral, Ember Tape Dusk and Deep Saw Tide**.
- Eight new Satin Tones bring the library to **640**.
- Stopped voices keep their audible release, then stop consuming unnecessary processing time.

## Play inside a dream

The 14 Itinera journeys introduced in 2.1 remain available. In 2.2, their live controls go further:

- Turn K1–K4 during a dream to shape **Density, Motion, Colour and Space** alongside the scene's own movement.
- These macro offsets remain in effect as later scenes arrive.
- See the name of the Biome currently sounding.
- Press **SEL to keep** that sound world and continue from it; use ENV to restore the environment from before the dream.

Journey Arranger remains optional. You can keep creating open-ended generative music without arranging a linear sequence.

## Connect and perform

- **USB MIDI Notes Out:** send a monophonic generated note stream from each synth track, with freely chosen channels or OFF. Drum-kit notes and incoming played notes are not included in this generated output.
- More robust USB MIDI queue handling and note release when stopping or leaving a dream.
- Existing MIDI input, MIDI Learn, Living Links, Drift, USB audio and screen mirroring remain part of the instrument. The new generated-note output does not add MIDI Clock Out, TRS Note Out or BLE Note Out.

## Clearer help and more processing headroom

- Context help now reports **640 Tones and 24 sound models**, explains freely assigned MIDI receive channels and distinguishes Live List references from saved User Biomes.
- Long holds open contextual help without also triggering the short action on release. HOME's dream shortcut remains a special case explained in the manual.
- DSP and effect-loop optimisations preserve audio output against the stated optimisation reference. In the documented dense six-track ECO tests, the lowest measured callback headroom was **12.30%** with effects, MIDI and active USB screen mirroring. The tested cases added no XRuns, overloads, dropped commands or automatic quality reductions. This is a measured test result, not a guarantee for every possible patch.
- The startup version is **2.2**, without DEV.
- The screen-reader word list is updated with the release.

## Your projects and compatibility

Back up User Banks and your edited live Biome before installing. The patch/bank format remains compatible with 2.1. Saving settings in 2.1 can remove the additional 2.2 preference data.

Factory Biomes remain available and can be used as starting points. Save your variations in User slots; the Factory ROM slots are not overwritten. The Live List does not change that behaviour.

The exact final build has been tested on Kid Bunka's existing FM-1. Qualification of this exact 2.2 package on UC25HQ80 hardware remains separate and pending; earlier user reports about 2.0 do not establish that test. Do not install an FM-1 package on an SMK-37.

## Explore the Mutable Dreams family

[Instruments, Web Studio and full guide](https://kidbee85-beep.github.io/mutable-dreams/) · [Mutable Dreams FM radio](https://kidbee85-beep.github.io/mutable-dreams/radio.html) · [Performance Garden for VIBE FM](https://kidbee85-beep.github.io/mutable-dreams/#performance-garden) · [MPC product preview](https://kidbee85-beep.github.io/mutable-dreams/mpc.html)

[YouTube · Kid Bunka](https://www.youtube.com/@kidbunka) · [Instagram · Mutable Dreams](https://www.instagram.com/mutabledreams/) · [Support development](https://www.paypal.com/donate/?business=kidbee85%40gmail.com&item_name=Mutable%20Dreams%20development)

Thank you for every listening session, bug report and idea. Mutable Dreams keeps growing because musicians share how they use it.

Release package: build **23F2FA0E** · SHA-256 `25df8be7b5d87806739e3481f0d8af371762e29051e4153cd86ce5bdae882b68`.

## Direct update and return verification

The exact final package 23F2FA0E passed direct installation from stock V15 and published 2.1, and both return paths on Kid Bunka's FM-1. Both 2.1 transfers preserved all 22 saved objects. Returning through V15 removes Mutable Dreams saved data: a verified saved-bank backup, separate live-project export and physical SEL restore are required. After restoration, all 22 objects and the unsaved live project matched exactly. A further eight-channel USB audio and full-screen-mirror check added no XRuns, overloads, lost commands or quality reductions. UC25HQ80 hardware validation remains pending.
