# Mutable Dreams 1.5 - New worlds, a living signal

Free, independent open-source firmware for the M-VAVE FM-1 by Kid Bunka.

## Wireless MIDI reception

Connect a phone, tablet or computer to the FM-1 BLE MIDI service and send notes, velocity, CC, pitch bend, sustain, transport and clock. GLO > WIRELESS MIDI adds radio, device-link and MIDI-thru controls. One BLE connection is available at a time; USB and TRS inputs remain available. Physical FM-1 tests verified notes, CC74 and Start/Clock/Stop from a Mac, including reconnection after a cold start.

## Twelve new places to wander

12 new Biomes and 12 new tone starting points bring the catalog to **168 musical Biomes plus Blank and 364 presets**. Explore deep drones, warm percussion, mossy rhythms, delicate air and softer meditation worlds. Existing factory IDs and saved User Banks are preserved. The browser instrument and live device editor carry the same new catalog.

## A brighter living display and a safer echo

Four new organic colour families and stronger accents move with the sound while keeping text readable. Echo delay bounds and ring-buffer indexing were corrected. The revised updater restarts the FM-1 and returned USB MIDI automatically in the final device tests. Bank backups remained byte-identical and the prior live patch was restored after testing.

Four new Biomes and two dense six-track/granular cases ran on the development FM-1 without observed XRuns, overloads, invalid samples or dropped commands in the measured short runs. One dense case used the intended adaptive quality protection. Final-build callback peak: 4,245 of 5,804 microseconds. These measurements describe the recorded tests, not a guarantee for every possible patch.

## Install and keep your work

Save your Biome, back up saved User Banks and preferences as .mddb, and export unsaved live edits separately as JSON. Use the browser or Python installer; official M-UPGRADE has rejected custom Mutable Dreams packages. The browser installer includes the original V15 return route and verifies its file hash. Returning to stock erases Mutable Dreams banks and preferences; reinstall a compatible version before restoring .mddb, with physical SEL confirmation. The earlier 1.4 stock roundtrip was tested; a complete new 1.5 stock roundtrip is not recorded. Keep older backups for downgrades.

## Compatibility limits

The FM-1 acting as BLE central and linking to a separate BLE peripheral is implemented but awaits a physical controller test; treat that direction as experimental. A newer UC25HQ80 FM-1 was not available: its JEDEC ID is accepted by static checks, but physical installation on that board is not yet verified. Physical TRS MIDI input and BLE clock-jitter performance remain unmeasured in this cycle. Keyboard/sequencer MIDI output and arbitrary CC Learn are not added in 1.5. This package is for the FM-1 only, not SMK-37.

Thank you for the feedback and careful listening that keep shaping this instrument. With gratitude to Émilie Gillet, Mutable Instruments and the open-source contributors. Mutable Dreams is independent, unaffiliated with and not endorsed by Mutable Instruments or M-VAVE. Original algorithms retain their notices and licenses.

Firmware identity: FM-1_710010500
SHA-256: ee5d410fc13b9861baa2336303821ccf66ec8c7143c167649886fad368198b52

## Browser installer correction — 10 November 2026

The standalone installer now includes its shared MIDI-port detection library, fixing `PORT_NAMES is not defined` before installation. MIDI permission denial no longer blocks retry. The firmware remains byte-identical to the original 1.5 release. See [installer correction notes](Installer-Fix-2026-11-10.txt).

The optional Python installer kit also omitted its local port/resource/backup helper modules. All four required Python modules are now included. Isolated ZIP extraction, CLI --help and optional-resource imports pass without creating a MIDI client.
