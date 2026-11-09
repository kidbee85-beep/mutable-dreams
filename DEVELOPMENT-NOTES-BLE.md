# Mutable Dreams 1.5 BLE MIDI — private development branch (2026-11-09)

This directory is an isolated experiment based on the private 1.4.1 development source. It does not replace the published 1.4 or the prepared 1.4.1 release candidate. The base is `fm1-base` commit `b0976f295ccaeaf77a8053a5a785a9cc21f5345f` and BSP submodule `5b0879bfadc69a17dbebb917fb6b86d516f19c44`.

## Implemented in this branch

- Open BLE MIDI stack, peripheral and central roles, pairing, platform-paced MIDI clock delivery and a dedicated saved radio record. One BLE link can be active at a time; USB MIDI and TRS MIDI input stay available.
- GLO > WIRELESS MIDI: radio toggle, found device selection, connect/disconnect, MIDI thru, connection state and pairing code. Long-press contextual help. The advertised name is Mutable Dreams plus a device suffix.
- BLE input uses existing track channel routing for notes, note off, velocity, CC, pitch bend, program changes, sustain, transport and clock. Clock source can be Internal, USB, TRS or BLE.
- MIDI thru can route USB/TRS input to BLE and BLE input to USB. This is an option and is saved with preferences. It does not yet transmit the FM-1's own keyboard/sequencer as MIDI notes.
- Link-loss handling releases only notes held by the BLE transport, preserving a note held over USB.
- Existing factory Biome and tone IDs, user banks and preference format are retained. Twelve curated Biomes and twelve voices follow the old catalog, for 169 Biomes including Blank and 364 tone starting points. The radio's pairing record occupies its own two-sector store object and is excluded from user-bank backup.
- Four new saturated, organic colour palettes follow the new Biomes on the Live screen. Text is still painted above the moving field and the GUI only redraws at its adaptive frame pace.
- The new factory scene delays are clamped to the verified 40–350 ms echo window; DSP echo indexing has a second ring-size guard.
- Lossless trimmed/packed font data preserves the existing glyphs while reducing the font set from about 79.7 KB to 23.2 KB. Display and audio load were measured in short physical runs below.

## Verification

- `app/tests/ble_midi_host.txt`: scanning, connection, BLE note/CC, USB and TRS MIDI thru, BLE clock Start/Stop, preference save/reboot all pass in the host simulator.
- `app/tests/ble_disconnect_host.txt`: BLE note releases on disconnect while USB note stays held; passes in the host simulator.
- The 1.5 device build passed: image 564,300 B; RAM static 83,740/98,304 B; DSP pool 334,896/344,064 B. An 8,192 B reserve leaves 976 B discretionary DSP pool, so a large new DSP engine or long PCM buffer cannot be assumed safe. Package identity: `FM-1_710010500`, SHA-256 `ee5d410fc13b9861baa2336303821ccf66ec8c7143c167649886fad368198b52`.
- All 169 factory scenes passed patch validation. Four 20-second native host sound renders reported zero invalid samples and zero queue drops. BLE MIDI, disconnect and sustain host tests pass. The installed 1.4 user's bank backup restored and imported in the host simulator, with a valid patch.
- A representative three-second Biome rendering with no incoming MIDI produced a byte-identical WAV to the pre-BLE 1.4.1 development build (SHA-256 `e52e6dc3a1d3635214c577af546e187e9b919842063034ab0084360d342317b3`).
- One connected `Mutable Dreams FM-1` was identified as running 1.4 (`FM-1_710010400`). Before updating, two bank backups matched byte for byte (SHA-256 `bd6bb4cf51511e7e848f347d7001fae876ef739892c1d9c0da02c1450464c872`). The initial 1.5 updater needed a manual power/USB cycle. After changing the updater to reset the whole chip, the final build wrote 100%, restarted, and USB MIDI re-enumerated automatically as `FM-1_710010500`. The saved banks matched the backup byte for byte, including the backup after the final build (SHA-256 `bd6bb4cf51511e7e848f347d7001fae876ef739892c1d9c0da02c1450464c872`). The prior live patch was restored byte for byte after every destructive test and the Bluetooth CC test.
- A macOS BLE central connected to the FM-1's standard BLE MIDI characteristic and delivered note on/off and CC74; the FM-1's event count and cutoff responded. A separate Start / 48 Clock ticks / Stop run started and stopped transport with zero XRuns. This confirms the use case where an external controller connects to the FM-1 as a BLE central. The reverse role, where the FM-1 finds an external BLE peripheral, has not had a physical sender test. The final on-demand scan let a Mac connect while the Wireless MIDI page remained open; note and CC74 changed the physical device state, and the live patch was restored exactly afterward.
- Four new Biomes were captured from FM-1 USB audio for about 22 seconds each. Two dense tests ran six unmuted voices with Grain Size 1000 and tape noise for 14 seconds each. All six physical runs had zero XRuns, overloads, nonfinite samples or dropped commands. Dense CPU peaks were 61% and 57%; audio callback peaks were 3658 and 3455 µs against a 5804 µs budget. One adaptive quality reduction occurred in the second dense run. These are short-run observations, not a long soak test.
- The packaged browser version starts its WebAssembly instrument, lists 169 Biomes and 364 tones, plays a new Biome, and shows the Wireless MIDI page. The active tab contrast was corrected after visual inspection. The update film is 77 seconds with four different native-engine sound sections and no narration.

## MIDI input guide for a natural-signal controller

Set the controller to a track's routed MIDI channel (defaults: tracks 1–6 on channels 1–6). Track CCs: 7 level, 10 pan, 74 cutoff, 71 resonance, 91 reverb send, 12 delay send, 13 granular send, 5 glide; CC64 sustain and CC11 expression also work. On channel 16, CC16–19 control the four live macros when CH16 MACROS is on. The controller can act as a BLE central and connect to Mutable Dreams, or as a BLE peripheral that the FM-1 finds from its WIRELESS MIDI page. Arbitrary CC Learn is not implemented yet.

## Remaining work before a public firmware

1. The full-chip reset fixed USB MIDI re-enumeration on the existing FM-1 with a CLI installer. Qualify the exact browser installer before public release. Do not touch a Syntherella device.
2. Verify the FM-1's BLE central role, pairing/recovery and concurrent USB/clock operation with an external BLE-MIDI peripheral; the available Mac cannot advertise as a peripheral. Test iOS and an external controller when available. The peripheral role already passed actual note/CC/clock input tests.
3. Verify an actual newer UC25HQ80 FM-1 separately. Both old and new flash IDs are accepted in the static build, but the new board is not physically available. TRS MIDI likewise requires an external sender.
4. Measure BLE clock jitter and latency with a timestamp-aware instrument before claiming timing precision. The platform currently paces delivery because the engine does not consume sender timestamps (`ble_midi_time = false`).
5. For arbitrary natural-signal CCs, add persistent CC Learn or let the external controller remap to the fixed CCs. Full bidirectional MIDI output from the FM-1 keyboard/sequencer remains outside this build.
6. Prepare release metadata, installer and source notices (MPL-2.0, Apache-2.0, BSD-2-Clause and the combined distribution terms). Do not advertise the untested roles or board as qualified.

## Environmental sound and JIANT comparison

JIANT's PROX/AIR feature modulates sound from Bluetooth signal strength; AIR scans nearby devices and is explicitly experimental. This is environmental radio sensing, not microphone audio recording. JIANT also offers USB audio *from* a computer/phone into its mixer. The FM-1 lacks a built-in microphone or analog audio input. For Mutable Dreams, immediate environmental sonification can use the user's natural-signal BLE-MIDI device. Actual field recordings would need a microphone on a phone/computer and a separate audio import or USB-audio-return path, then a carefully bounded route into the existing granular tape or sample-kit storage. It cannot be presented as direct recording by the standalone FM-1.

Sources: https://github.com/charlesvestal/fm1-base , https://github.com/juanjiant-bit/JIANT/blob/main/docs/MANUAL.es.md , https://github.com/zisonzishen/crate-fm1
