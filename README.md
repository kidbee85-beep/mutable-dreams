# Mutable Dreams 1.4

A free, independent instrument for the M-VAVE FM-1. [Play and edit in a browser](https://mutable-dreams.sugarypixie10.chatgpt.site/) or [download the release](https://github.com/kidbee85-beep/mutable-dreams/releases/tag/v1.4). Six freely assigned synth tracks are available, or a Lo-Fi sample kit alongside three synth tracks.

## A wider atlas

Version 1.4 adds **48 distinct factory Biomes** and **16 tone presets**. The totals are **156 musical factory Biomes plus Blank** and **352 tone presets**. Ambient, bass-led, percussive, rhythmic and Lo-Fi worlds are placed near one another in the browse order so the next turn of PRESET can feel genuinely different. Existing factory IDs and saved User Banks stay intact.

## Shaped by the community

- A muted Home track dims across its entire row and meter and is marked **MUTED**. Press SEL on the selected track to toggle it. A separate Solo mode is not included.
- Five new scales: **Ukrainian Dorian, Harmonic Minor, Double Harmonic Minor, Harmonic Major and Double Harmonic Major**. There are thirteen in total for sequences and chord journeys.
- **USB MIDI input per track:** Off or channels 1–16. Tracks may share a channel. The Lo-Fi kit may use channel 10, and the channel-16 macro shortcut may be disabled when that channel is needed for notes. Physical TRS MIDI still needs an external cable test; Bluetooth MIDI is not included.
- More legible labels, contrast and responsive organic details across the 240 × 240 FM-1 display. The existing held-key hints, User Banks, sequencer, chord journeys, two LFOs per synth and edit protection remain available.
- The browser instrument uses the firmware application and DSP sources, with the new Biome atlas, patch import/export and a live USB editor. Browser User Banks are separate from device banks.

Rings no longer clears an audio scratch buffer redundantly. The exact release build passed bounded six-synth and kit-plus-three-synth stress tests with strong granular sends and no new audio faults. See the [qualification report](docs/release-1.4/DEVICE-TEST-REPORT-1.4.txt) for measurements and limits.

## Install and keep your work

Read the [recovery guide](docs/release-1.4/Recovery-1.4.txt) and [illustrated user guide](https://mutable-dreams.sugarypixie10.chatgpt.site/Mutable-Dreams-1.4-Guide.pdf) before updating. Save the Biome, export all saved User Banks as `.mddb`, and export unsaved live changes separately as JSON. The bank file does not contain temporary granular tape audio or calibration. Restoring after a Stock V15 roundtrip requires pressing SEL on the physical FM-1.

The exact package passed 1.4 → original Stock V15 → 1.4 from a device with the official V16 start region, followed by a byte-identical bank restore. The official M-UPGRADE was previously verified for manufacturer V15 → V16, but rejected a custom Mutable Dreams package. Use the [browser installer](https://mutable-dreams.sugarypixie10.chatgpt.site/Mutable-Dreams-1.4-Install.html) or Python installer kit for 1.4. A direct route from every other custom firmware is not claimed.

Firmware identity: `FM-1_710010400`
Release package SHA-256: `d6682e1433f581285333f23629be20b96a595707776c6359d22d14e6542d6bda`

## Source and credits

The complete source is browsable in `app/`, `base/`, `presets/`, `web/`, `tools/` and `tests/`. [BUILDING.md](BUILDING.md) documents toolchains, builds and checks. The versioned source ZIP contains a SHA-256 inventory. Published 1.0–1.3 tags and assets remain available.

With gratitude to **Émilie Gillet and Mutable Instruments** for their openly shared work. Selected Plaits, Rings and Elements algorithms retain their original notices. The platform retains its MPL-2.0 notices. Other module-inspired features are original interpretations, not complete module ports. Mutable Dreams is unaffiliated with and not endorsed by Mutable Instruments or M-VAVE.

The combined distribution is **GPL-3.0-only** with component notices retained; see [LICENSING-GPL.txt](LICENSING-GPL.txt). Proprietary manufacturer firmware and personal User Banks are excluded. Created by **Kid Bunka / Christoph Link**. Forks are welcome; please credit this project and upstream authors and give derivative work its own name and visual identity.
