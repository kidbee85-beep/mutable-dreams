# Mutable Dreams 2.4 — More room to dream

Created by Kid Bunka. Qualified FM-1 application build **8FBE5AF8**, identity **FM-1_710020400**. This release includes the retained 2.3 instrument and the following changes.

## Sound and musical starting points

- 12 new warm Tones and 20 new Biomes. Total: 664 Tones, 300 factory entries including Blank, six freely assignable tracks and 24 sound models.
- 11 additional Dreams, 25 in total. MY DREAM uses at least four playable starred Biomes; Blank is excluded and insufficient stars trigger an explicit fallback.
- Four-character base-32 Dream codes use 0–9 and A–X, without I and O. 0000 selects a fresh code. Matching version, code and settings repeat the musical structure; this is not a bit-identical recording promise.
- Optional AIR, Chorus, Width and Vintage, all off by default. Level Match is on by default for factory Biomes; User Biomes and Blank are excluded.
- GRIDS is an additional Kit generator. Additional density/chaos front-panel controls are not included.

## Playing and navigation

- On the FM-1 panel, recall saved User Biomes while stopped with USB/TRS/BLE clock selected, retaining MIDI clock, inputs, note outputs and mappings. SAVE/CLEAR and current Web Studio bank-protocol operations still require Internal clock.
- Hold SEL and press OCT- or OCT+ for the previous or next page in its family. The sequence wraps and the combo does not also change octave, mute or confirm.
- Hold any function button for contextual help. HOME long hold now opens help; use the HOME tap cycle or GLO to reach Dreams. Long PLAY/SEL during a Dream opens help without its short action; short PLAY stops and short SEL keeps the current Biome.
- Both LFOs can target shared Grain Size and Grain Position. Only new grains receive this movement. The clean synth voice and existing grains are retained.
- NEW, RECENT (last eight), A–Z, letter jump and return to the previous Biome; clearer pan, held peaks and clip feedback.
- LOOK has Startup, Night/Sound and Air/Character pages, cycled with REC. Sleep fades for 30 seconds then stops playback; it does not power off and is not saved. Internal tempo ranges divide by 1/2/4/8 down to 3.75 BPM (display 3.8); external clock is not divided.
- Corrected model-state reset when an edited voice model changes at a staged Biome handover.

## Both cores and memory

The second core performs ordered voice mixing and eligible Satin/Spectral oscillator jobs. Engine states use exclusive shared storage, including complete Elements resonator bodies; separate exciters remain. Six tracks, all 24 models, fixed stacks, audio buffers, tape and sample rate remain available.

Free SRAM: 22,836 bytes, including the mandatory 8,192-byte reserve; 14,644 bytes above it. The last memory step recovered 8,016 bytes versus intermediate build AEC27818. This is not a gain measured from 2.3. Application image: 575,936 / 581,564 bytes. No first-firmware, universal 2× or native per-core-percent claim is made.

## Qualified test scope

One Puya-chip FM-1; exact unchanged build 8FBE5AF8. SYSTEM and 34 load windows across 24 models were carried unchanged from the same-image v3 run with their original hashes and timestamps. The v4 suite passed 96 Elements cases (12 FULL / 84 ECO under existing admission), eight USB stem-isolation windows, six Handover cases, 20 Grain-LFO windows and bank/MIDI/menu/help cases. A complete natural Dream ran for 1,800.822 seconds plus 30 seconds of tail. The highest observed callback was 4,672 / 5,804 microseconds, below the 5,107-microsecond gate. Smallest measured free MAIN / IRQ / second-core stacks: 15,460 / 2,208 / 2,044 bytes. No added audio/USB errors in these qualified cases. Original saved data, live state and UI were restored byte-exact; settings and master were restored.

This is a scoped qualification, not a guarantee for every patch. Physical UC25HQ80 verification, a fresh fully power-disconnected cold start and update routes not explicitly covered on 8FBE5AF8 remain outside the claim. Historical 2.3 installation legs do not qualify every 2.4 route. No installed-flash SHA readback was obtained; the supplied package SHA is verified.

## Update and compatibility

Back up saved User Banks as .mddb, export the edited live Biome separately, and retain your Favorites and Tone stars before updating or downgrading. Patch wire format is version 12, 2,604 bytes. New Grain routes 7/8 and GRIDS generator 4 are rejected by 2.3. Save an older-compatible copy before a downgrade; do not overwrite the only copy. The application package does not install a bootloader. m-bootloader is separate and optional. Follow Recovery-2.4.html for chip notes and stock-return instructions.

## Not included and acknowledgements

No new full Clouds/Warps/Marbles/Tides module, Shimmer reverb, sampling, USB disk mode, external-clock Itinera sync or in-place factory overwrite is claimed. Existing functions retain their previous scope.

With special thanks to **Sarmiento (Funkelectric)**, **Yimiblú**, Screen Reader testers and everyone sharing careful reports, ideas, code and music. Selected DSP by Émilie Gillet / Mutable Instruments; platform and simulator by fm1-base / felucca-bsp. Retain the complete source licence notices. Created by **Kid Bunka**.
