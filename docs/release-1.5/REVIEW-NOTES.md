# Mutable Dreams 1.5 — private review

This is a development preview, not a published update. The previously released versions remain unchanged.

## New in this candidate

- BLE MIDI receive for notes, CC, pitch bend, sustain, transport and clock; USB and TRS remain available. A dedicated Wireless MIDI page offers radio, device link and MIDI thru controls. Physical receiver tests have passed for notes, CC74, Start, Clock and Stop.
- Twelve curated Biomes and twelve tone starting points added after the existing catalog; old factory IDs and user banks are preserved. New factory worlds span deep drone, delicate air, warm beat, dense rhythmic and textural sound families.
- Brighter Biome-specific colour palettes and an organic moving background on the FM-1 display, with native text left readable.
- Echo delay bounds and ring-buffer read protection fixed after a listening test found one invalid long echo setting.
- The build lists both Puya P25Q80 and UC25HQ80 flash IDs. A physical UC25HQ80 unit is still required to verify that newer board variant.

## Evidence and current gate

- All 169 Biome scenes validate in the host simulator. BLE note/CC/clock, link loss and sustain host tests pass. Four 20-second sound captures had zero invalid samples or queue drops.
- Device image: 564,300 bytes. Static RAM: 83,740 of 98,304 bytes. DSP pool: 334,896 of 344,064 bytes; after the established 8,192-byte safety reserve, only 976 bytes remain discretionary in that pool. A large new DSP engine or sample buffer is not implied by these figures.
- The existing FM-1 was backed up and the bank backup was independently repeated byte for byte before installing 1.5.
- The initial 1.5 updater needed a manual power/USB cycle. A revised full-chip reset was then installed twice; the final flash re-enumerated USB MIDI automatically as `FM-1_710010500`. The saved bank backup remained byte-identical before and after the final build, and the pre-update live patch was restored byte for byte after testing.
- Four different new Biomes ran on the physical FM-1 for about 22 seconds each, with zero XRuns, overloads, nonfinite samples or dropped commands. Two further dense tests forced all six tracks on, set Grain Size to 1000, and mixed in tape noise for 14 seconds each. They also had zero XRuns, overloads and dropped commands; one automatic quality reduction occurred. Their measured audio callback peaks were 3,658 and 3,455 µs against a 5,804 µs budget; UI frame peaks were 95 and 83 ms.
- A Mac connected to the FM-1's BLE MIDI service and sent note on/off and CC74. The device's event count increased and the live cutoff changed to the expected value. In a separate run, BLE Start and 48 Clock ticks started playback, and BLE Stop ended it, with zero XRuns. After a full cold restart the Mac connected again and sent note on/off and CC74 successfully; the pre-test live patch was restored byte-for-byte. The final build also allowed the Mac to connect while the Wireless MIDI page stayed open; note and CC74 changed the physical device state, with zero XRuns. The FM-1's central role still needs an external BLE-MIDI peripheral: this Mac could scan as a central but could not advertise as a peripheral.
- The private 1.5 browser page starts the current WebAssembly instrument, lists 169 Biomes and 364 tone presets, plays Neon Moss, and shows the new Wireless MIDI page. The browser's native radio controls remain disabled when no FM-1 is connected. A visible active-tab contrast fault was fixed.
- Actual hardware testing on a newer UC25HQ80 FM-1 is still open. The package includes its flash ID and passes static installer tests; that is not a substitute for a physical installation.

## Video

`Mutable-Dreams-1.5-Update.mp4` is 77 seconds, 1280 × 720, 24 fps. It contains no voice and no stock music. The four audio sections are from Biomes Obsidian Tide, Night Hearth, Neon Moss and Felt Labyrinth rendered by this candidate's native sound engine. The display animation is captured from the same firmware's host renderer. Audio was level adjusted and crossfaded for the film; no third-party samples were added. Four separate physical USB-audio captures are available in the private work folder as listening references. The film describes BLE MIDI only after the receiver hardware test passed.

`Mutable-Dreams-1.5-Short.mp4` is a 40-second vertical cut with the same four sound worlds and native screen frames. `Listening/index.html` offers the four soundtrack excerpts separately. The installer, recovery guide and source archive links were checked locally. The final source archive contains 892 individual files plus its SHA-256 manifest; every entry verifies and a scan found no personal-name strings. The exact package inventory is recorded in `SHA256SUMS.txt`. The final-build eight-second playback had zero XRuns, overloads, nonfinite samples or dropped commands; its callback peak was 4,245 µs against a 5,804 µs budget. User Banks stayed byte-identical after the last flash. Browser and physical-device LED/LFO controls were checked separately.

## Remaining release checks

An external BLE-MIDI peripheral is needed to verify the FM-1 acting as central, and a newer UC25HQ80 FM-1 is needed for its physical update test. TRS MIDI requires a TRS sender. The full-chip reset resolved warm-reboot USB enumeration on the tested FM-1. The exact browser install flow still needs a visible Web MIDI check: the available automated local browsers did not execute even a minimal inline script. The embedded installer passed a JavaScript syntax check and the update protocol's boot, fast/resume, port selection and stock tests. Public website, GitHub release and YouTube have not yet been changed; the published 1.4 remains available. Version 1.4.1 was prepared privately, but is not listed as a public GitHub release.
