<!-- SPDX-License-Identifier: MPL-2.0 -->
# The own Bluetooth stack

fm1-base can run Bluetooth on its own stack instead of JieLi's libraries: `ble_stack = "own"` in fm1.toml, with
`ble = true`. The stack is Henri David's, from [Optimist](https://github.com/w0ts/optimist), offered under
GPL-3.0-only or MPL-2.0 ([w0ts/optimist#55](https://github.com/w0ts/optimist/issues/55)) and used here under
MPL-2.0. It was written from the Bluetooth specifications and from measurements of the FM-1's radio. No JieLi
library is linked. For what it gives a firmware, its options and its limits, see the README's "Bluetooth and WiFi".

## The documents here

They come from Optimist with the stack (`3274ce5`) and describe it as Optimist has it, so their paths are
Optimist's: `firmware/src/ble/` is `bt/ble/` here, `firmware/src/bt/` is `bt/br/`, and `firmware/hal/` is
felucca-bsp's `chip/`. Optimist's UI pages, its MIDI router (`io/midi/`) and its builder items are not part of
fm1-base; here the platform's glue and fm1.toml take their place (below).

| File | What it is |
| --- | --- |
| [BLE-STACK.md](BLE-STACK.md) | The BLE stack: link layer, host and GATT, security (SMP), BLE-MIDI, the radio driver, the radio's start-up, the central role, the tests |
| [BLE-HW-FACTS.md](BLE-HW-FACTS.md) | The BLE radio's hardware as it was read and measured: registers, the per-link control block, the start-up sequence, the stored trims. The code cites its sections as `HW §n` |
| [BLE-DEVICES-DESIGN.md](BLE-DEVICES-DESIGN.md) | Connecting out to Bluetooth MIDI devices, remembering them, and the pairing a phone or Mac asks for, as Optimist's pages do it |
| [BT-CLASSIC.md](BT-CLASSIC.md) | Bluetooth Classic (BR/EDR) and the A2DP source: the hardware, hopping and AFH, pairing, the codecs and latency. The code cites its sections as `BR §n` |
| [RF-CALIBRATION.md](RF-CALIBRATION.md) | How JieLi's SDK calibrates the radio at start-up, in our own words; the specification of felucca-bsp's `chip/fm1_rf_cal.h` |
| [ble-traces/](ble-traces/) | Register writes and stored trims recorded in an emulator from stock V15 and the SDK's `demo_ble`, for reference |

## How fm1-base uses it

The platform's glue is in `device/` (MPL-2.0, Charles Vestal), built into `radio.c` in place of JieLi's shim:

- `device/bt_own.c`: Bluetooth MIDI with a computer or phone. It starts the radio, calibrates it (felucca-bsp's
  `chip/fm1_rf_cal.h`, at every start, so nothing stored is needed), keeps the address and the bond in `OBJ_BLE`,
  and gives `radio.c` the same calls JieLi's build does (`plat_ble_*`). The stack runs in the radio's two interrupts
  and talks to the main loop through rings; there are no tasks.
- `device/bt_own_central.c` (`ble_central = true`): scanning for Bluetooth MIDI devices and connecting to one, the
  FM-1 as the central. One link at a time: the computer's or the device's.
- `device/bt_own_audio.c` (`bt_audio = true`): the A2DP source to headphones and speakers, with the remembered
  devices (`plat_bta_*` in `include/plat.h`).

The radio's drivers (`chip/fm1_ble.h`, `fm1_ble_rf.h`, `fm1_bredr.h`) and its start-up tables
(`chip/ble_rf_tables_v15.h`) are in felucca-bsp. `tools/build.py` adds `bt/ble` and `bt/br` to the build and
checks that nothing of JieLi's is linked. `tools/fm1_remote.py log` reads the radio's log, and `bt_log = true`
puts the audio set-up's steps on it. The stack's host tests are `tests/bt/run.sh`, run by `tests/run_tests.sh`.
`examples/ble_own` is the template built with every option.

For the licences of the stack and the parts it carries (AOSP's SBC encoder, micro-ecc, JieLi's tables and
calibration), see [LICENSING.md](../../LICENSING.md).
