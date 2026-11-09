<!-- SPDX-License-Identifier: MPL-2.0 -->
# 2.4 GHz radio calibration at start-up (AC791N / WL82)

How JieLi's AC79 SDK calibrates the shared Wi-Fi / Bluetooth radio when it starts, and what each byte of the stored trim
records 106, 107, 108 and 187 is. The aim is our own implementation that runs on every boot of an FM-1 whose SDK VM is gone, and
feeds `fm1_ble_rf_init(x106, x107, x108, x187)` (`bsp/chip/fm1_ble.h`). This is a specification in our own words. It holds no
vendor code. Register facts it builds on are in [BLE-HW-FACTS.md](BLE-HW-FACTS.md) ("HW §n").

## 0. Sources and confidence

The source of truth is the official SDK at `~/fw-AC79_AIoT_SDK` (gitee `Jieli-Tech/fw-AC79_AIoT_SDK`, Apache-2.0 at its root). It
has three kinds of input:

- the radio libraries, LLVM bitcode with debug info: `cpu/wl82/liba/wl_rf_common.a` (members `wf_rf_init.c`, `wf_rf_cal.c`,
  `wf_pa_dpd.c`, `wf_cfr_init.c`, `wl_rfd_init.c`, `wl_hw_init.c`, `wf_phy_mac_lib.c`, `wf_phy_mac_init.c`, `wf_soft_adj.c`,
  `wl_usb_to_sdio.c`, `wl_sdio.c`) and `cpu/wl82/liba/btctrler.a` (`RF.c`, `analog.c`, `RF_ble.c`);
- the SDK's application sources: `apps/common/config/user_cfg.c` (the getters for 106 / 107 / 108),
  `apps/common/net/wifi_conf.c` (the 187 getter / setter, the RF-die table, the switches), the boards'
  `wifi_calibration_param` (`apps/demo/demo_ble/board/wl82/board.c` and the others);
- headers: `include_lib/utils/syscfg/syscfg_id.h` (the VM ids), `include_lib/net/wifi/wifi_connect.h`
  (`struct wifi_calibration_param`).

Our unit's log from fm1-base's JieLi build prints exactly the strings of this library version (`wifi_get_xosc xosc_l : %d
xosc_r : %d`, ` %d bank: %x`, `bak: %x %d`, `setbak:%x`, ` PA_C_I: ...`, `wf_rf init end`, `WL82 analog init ok`,
` BT_TX_IQ: ...`, ` BT_OF: ...`). The 187 layout read from the library's debug info matches HW §16.4 byte for byte. So the stock
firmware V15 and our JieLi build run this code. (Libraries disassembled in a scratch directory; nothing of them is in any repository.)

Confidence tags: **H** read directly from the library's code or the SDK source; **M** read from the code, but the meaning of a
register or bit is inferred; **L** inferred, not read. Function names are the library's own.

## 1. Headline: where each record's bytes come from

| Record | Bytes | Kind | Where the value comes from | Needed for BLE? |
|---|---|---|---|---|
| 106 `VM_XOSC_INDEX` | 2: `xosc_l`, `xosc_r` | **constant** | the board's `wifi_calibration_param` (`0x0B`, `0x0B`), copied into the VM at first boot; a production tester may later rewrite it | yes (crystal load, carrier accuracy) |
| 107 `VM_WIFI_PA_DATA` | 7: `PA_C_I` | **constant** | the board's `pa_trim_data` (`1, 7, 4, 7, 11, 1, 7`), copied into the VM at first boot | yes (shared PA bias) |
| 108 `VM_WIFI_PA_MCS_DGAIN` | 20 | **constant** | the board's `mcs_dgain` (all 0 on demo boards and on V15 = "keep the library's defaults") | no (Wi-Fi per-rate TX gains) |
| 187 `VM_WIFI_RF_INIT_INFO` | 64 + CRC | **measured** | the library's own trim (`wf_rf_trim`), every boot that has no valid 187; then saved | partly (§4) |

Nothing is read from efuse, OTP or chip configuration. 106 / 107 / 108 are compile-time values of the firmware that wrote them,
stored so a factory tester could override them per unit (`apps/common/rf_fcc_tool`, `apps/common/net/testbox`). Both V15 and our
unit hold exactly the SDK demo-board values, so M-VAVE never tuned them [H for the source of the values; H that V15 and our unit
match the defaults; one unit seen, HW §14.1].

Three more things are measured every boot but stored in no record: the **VCO bank table** (§3.2), the **BT TX IQ / DC
compensation** (derived from 187 bytes 0-23, §5) and the per-channel VCO banks in the RF-die table (§6).

## 2. The SDK's call order

`bt_rf_init` (`RF.c`) starts the radio. It calls `wf_rf_common_init` first, which runs the Wi-Fi hardware init once per boot
(`wf_phy_init_flag`); a Bluetooth-only firmware cannot skip it (HW §5.2). The order [H]:

| Step | Function | What | HW §16.1 group |
|---|---|---|---|
| 1 | `wf_clk_rst_init` | `0x10010` bits 14-15 cleared; Wi-Fi clock / reset block `0x14040-0x14050` in steps of 1 µs, 50 µs settle; `0x1405C` | 1, 3 |
| 2 | `wdma_init` | Wi-Fi DMA / MAC enables `0x1404C` bit 24, `0x30F00/04` | 3 |
| 3 | `wfhw_init`: `wf_phy_mac_rst`, `wf_sifs_reset`, MAC `0x308` = `0x9407`, `cfr_int(0)` | MAC / PHY reset, TX back-end start values | 3, 4 |
| 4 | `wf_phy_init` | the BBP load (constant, window writes; HW §16.3) | 4 |
| 5 | `wf_rf_init` | crystal trim (VM 106), analog init `0x11900-0x11964`, **VCO bank scan**, PLL at 2442 MHz, then **trim or quick start (VM 187)**, then PA trims (VM 107) | 5-9 |
| 6 | `wf_phy_init` again | reloads the BBP that the trim's loop-back set-up changed; does not touch the trim entries | 10 |
| 7 | `cfr_int(1)`, `rapp_init` | Wi-Fi TX crest-factor and PA predistortion tables (constant; §7) | 10 |
| 8 | `wl_anl_actl_init(0)` | the RF-die table, 2 × 256 words, with the measured patches (§6) | 11 |
| 9 | `wf_rf_bsb_auto_ctl`, `wifi_set_pwr(6)`, `wifi_set_channel(6)` | constant analog and `0x1405C` writes | 12 |
| 10 | `pa_mcs_dgain_init` | VM 108 into window D (§3.4) | 13 |
| 11 | `rf_dc_get_reload` | keeps `0x1191C` / `0x11920` (the TX LO trims) for BT's `bt_reload_trim` | 12 |
| 12 | timer | `wl_temperature_drift_freq_trim` every 3 s, if `wifi_temperature_drift_trim_on` (§7) | |
| 13 | `bt_rf_init` continues | BT domain (HW §5.1), `bt_rf_analog_init`: PLL channel table, AGC, BT block, **BT TX trims from step 5** (§5) | 14 |

Inside step 5, `wf_rf_init` [H]:

1. `wifi_get_xosc` (VM 106, §3.1), print, then a ramp of `0x11930`: bits 0-4 set one by one, [6:5] = 2, bits 7 and 10-12, 17, 18
   cleared, [9:8] = 2, **[16:13] = `xosc_l`, [22:19] = `xosc_r`**, [27:23] = `0x0B` (24 MHz crystal; `0x1A` for 40 MHz).
2. Constant read-modify-writes of `0x11900-0x11964` (the analog init; captured, HW §16.1 group 6).
3. `wf_rf_en_set(5)` (synthesizer only, §3.6), `set_pll_rn_en`, **`wfa_pll_bank_scan(121)`** (§3.2).
4. The fractional PLL for 2442 MHz: with a 24 MHz crystal, 4884 / 48 = 101.75, so `0x11938` [11:0] = 101 and `0x11934` [23:0] =
   0.75 × 2^24; `0x11934` bits 25, 26, 27 cleared; `0x11940` bit 0 set, [7:6] = 0 (24 MHz); `0x1193C` [21:18] = 9, [29:27] = 2;
   `0x11954` = `0x02822520`; `set_pll_rn_en`; `0x11938` bit 18 low for 80 µs, then high. (All constant for one crystal [H].)
5. `wifi_trim_is_already` (§4.5). False: `wf_rf_trim` (the measurement, §4). True: `quick_start_wifi_trim` (apply the stored 187),
   and if that fails, `wf_rf_trim`.
6. `wf_rf_en_set(0)` (normal receive), `pa_config_tune` (VM 107, §3.3), `keep_tx_iq_set` (BT trims, §5),
   `tx_rx_bw_set_save` (read the trims back into the 187 buffer and schedule the save, §4.5).
7. Constant tail: `0x11904` |= `0x30000`, [14:12] = 4; `0x11908` [2:0] = 0; `0x1190C` [7:6] = 1; `0x11910` [11:8] = 9, [6:0] = 14;
   `wfrf_flt_vga_gain(0)`; print `wf_rf init end`.

## 3. The constants and the scan

### 3.1 Record 106: crystal load trims [H]

`wifi_get_xosc` (weak, `user_cfg.c`): start from the board's `xosc_l`, `xosc_r`; if VM 106 reads 2 bytes, use them; otherwise write
the board values to VM 106. Values are 4 bits (0-15; the FCC tool clamps to 0..15). They load the crystal's two capacitor banks:
`0x11930` [16:13] and [22:19] (`wf_write_xosc` / `wf_read_xosc` use the same fields). Nothing measures them on the device: a
factory tester measures the carrier with an instrument and writes 106 (`rf_fcc_main.c`, `testbox/client.c`).

For our code: **106 = `0B 0B`**. A crystal off by a few ppm still meets BLE's ±150 kHz carrier tolerance. The SDK's own BLE
frequency correction (VM 110, `CFG_BT_FRE_OFFSET` → `PLL_COMP` in the link block, `ble_pll_comp_reset`) is read only when
`config_fix_fre_enable` is set and the FCC tool has stored a result; V15 has no 110, so `PLL_COMP` = 0 [H].

### 3.2 The VCO bank scan (every boot, stored nowhere) [H for the steps; M for the comparator's meaning]

Registers: `0x11938` [25:19] the VCO capacitor bank ("cap", 7 bits); `0x1193C` [12:5] the frequency code ("fbds", 8 bits);
`0x11940` bit 24 the control-voltage detector enable; `set_pll_rn_en` restarts the loop: `0x11938` bit 16 low, bit 18 low,
`0x11934` bit 24 low, 5 µs, `0x11938` bit 16 high, 5 µs, `0x11934` bit 24 high, `0x11938` bit 18 high, **100 µs**.
A comparator read is `wlrf_get_(1)`: `0x11968` [31:28] = 1, write `0x11978` = 1 eight times, then 0, read `0x11978`. Its result
`res` = bit 18 as bit 0, bit 17 as bit 1: **1** = the detector's "above" flag only, **2** = "below" only (names [L]).

The scan, `wfa_pll_bank_scan(121)`:

1. Detector on (`0x11940` bit 24).
2. Mid bank (`get_wfa_pll_midbank`): fbds = 73. For mid = 7, 23, 39, ... while mid < 120: measure `res` at cap = mid, mid − 7 and
   mid + 8 (each a `set_pll_cap`, `set_pll_rn_en`, read). If all three agree, try the next mid; at the first mid where they
   differ, stop. Print ` 2394 bank: <mid>` and return `top = mid | 15`. (If none differs, the code returns the last `res`, a bug;
   ours should fall back to 63.) Our unit: mid `0x37`, top `0x3F` (log ` 2394 bank: 37`).
3. Bank limits: `limit[47] = top`. Keep `prev_h = 73`, `lo = 73`. For i = 0..46: cap = top − i (print `bak: <cap> <i>`). Step
   fbds up from `lo`: at each fbds, restart the loop and read `res`. On **2**, remember this fbds as the new `lo`; on **1**, stop
   with `h` = this fbds; on 0 or 3, go on. Past 171 (`0xAB`), stop with `h` = 171 (and `lo` = 171 if the last read was 2).
   Then `limit[i] = (prev_h + lo − 3) / 2` (integer division), `prev_h = h`. Each bank's search starts from the previous `lo`,
   so the sweep moves up through fbds 73..171 once, with overlaps. Our unit ends at cap `0x11` (`bak: 11 46`).
4. `bank(f)` (`get_wfa_pll_bank`): the first i in 1..47 with `limit[i] > f` gives `top − (i − 1)`; none gives `top − 47`.
5. Set fbds = 121 and cap = `bank(121)`, print `setbak:<cap>` (our unit `0x33`), detector off.

The table `limit[0..47]` is used again for the RF-die table's 128 frequency entries (§6). The fbds code is close to the RF
frequency in MHz minus 2320 (the table's entry 0, about 2400 MHz, has fbds 80, one more per MHz) [M].

Cost: one loop restart is about 110 µs plus register time; V15 took about 380 steps in the emulator (HW §16.1), so about 45 ms.
The worst case (every bank from 73 to 171) is about 0.5 s.

Our replay differs here today. Its group 7 bisects over bands 0-63 for a "neither" reading and then writes stock's captured band,
and the RF-die word-1 entries 0x00-0x7F carry the banks of the **emulator's** scan (BLE-STACK.md §8.1). A unit whose VCO
sits elsewhere would get wrong per-channel banks. Doing step 3-5 and §6's patch fixes both.

### 3.3 Record 107: PA bias trims (`PA_C_I`) [H]

`wifi_get_pa_trim_data` (weak, `user_cfg.c`): board `pa_trim_data`, VM 107 if present, else written to VM 107; it returns 1, which
`pa_config_tune` (`wf_pa_dpd.c`) takes as "apply them" (the comment says the automatic tune is not used; with 0 the library applies
nothing, there is no measuring code in this version). `pa_config_set(i, v)`: byte 0 → `0x11924` [26:24]; 1 → `0x11928` [2:0];
2 → [11:9]; 3 → [17:15]; 4 → [25:22] (4 bits); 5 → `0x1192C` [2:0]; 6 → no register. Print ` PA_C_I: ...`. The RF-die table
also carries them (§6): bytes 0-4 in word 1 of entries 224-255, bytes 5-6 in word 0. This matches HW §14.5.

For our code: **107 = `01 07 04 07 0B 01 07`**.

### 3.4 Record 108: Wi-Fi per-rate TX gains [H]

`wifi_get_mcs_dgain` (weak): board `mcs_dgain`, VM 108 if present, else written. `pa_mcs_dgain_init` starts from built-in
defaults and lets every **non-zero** byte replace its default, then writes window D (the TX back-end, §3.5) entries:

| Byte | 0 | 1 | 2 | 3 | 5 | 7 | 9 | 11 | 12 | 14 | 16 | 19 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Window-D entry | `0x0B` | `0x0D` | `0x0F` | `0x11` | `0x1B` | `0x1D` | `0x1F` | `0x21` | `0x13` | `0x15` | `0x17` | `0x19` |
| Default | `0x20` | `0x20` | `0x20` | `0x20` | `0x20` | `0x2B` | `0x26` | `0x20` | `0x20` | `0x2B` | `0x26` | `0x20` |

Bytes 4, 6, 8, 10, 13, 15, 17, 18 are not used. This is HW §16.4's table exactly. The entries are digital gains of the Wi-Fi
OFDM / CCK transmitter [M]; BLE transmits through the BT modem (`0x2FC00`), not this path. For our code: **108 = 20 × `00`**.

### 3.5 The register paths the calibration uses [H]

- **BBP port** `0x3101C` (MAC register `0x101C`; MAC registers are at `0x30000` + offset): a write waits for bit 17 to clear, then
  writes `(reg << 8) | data | 0x80000`, then the same with `0xA0000` (start). A read writes `(reg << 8) | 0x90000`, then
  `| 0xB0000`, and reads the port's [7:0] right away (the SDK does not poll; its call overhead is the wait). HW §5.2.
- **Windows** (HW §16.3), by the library's names: C = "cca" (`0xC8`-`0xCB`), C' = "rx11n" (`0xCC`-`0xCF`), **D = "tx_backend"**
  (`0xD0`-`0xD3`), **D' = "rx_frontend"** (`0xD4`-`0xD7`). Write: entry, data, commit 1. Read: entry, commit 2, read the
  read-back register. A read-back returns the addressed entry: the library depends on it everywhere (this settles U17) [H].
- **PHY reset** `wf_phy_rst`: MAC `0x1004` = 12; BBP `0x16` = 0, `0x15` = `0x31` then `0x30`, `0x19` = 0. **Loop-back**
  `wf_phy_loop_always_test`: PHY reset, MAC `0x1004` = 12, BBP `0x19` = `0x18` (TX looped into RX) [M for "loop-back"].
- **Analog read port** `wlrf_get_(sel)`: §3.2.
- **RF-die SPI** `0x14028`-`0x14034`: HW §5.2 (commands `0xF` write, `0x6` read, `0xE` word 0, `0xD` word 1).

### 3.6 Analog modes and helpers used by the trim [H for the writes; M for names]

`wf_rf_en_set(mode)` sets a group of enables from five flags. Mode 0 = receive, 1 = transmit, 2 = TX calibration, 3 = RX
calibration, 4 = RX DC calibration, 5 = none (synthesizer only). Each bit below is written to the value of its flag, where
A = receive | RX-cal | TX-cal | RX-DC-cal (not transmit), R = receive | RX-cal | RX-DC-cal, T = transmit | RX-cal | TX-cal:

- A: `0x11900` bit 23, `0x11910` bits 12 and 27, `0x11914` bits 11, 18 and 26, `0x11918` bits 0 and 1;
- receive alone: `0x11904` bit 15;
- R: `0x1190C` bits 4, 5 and 20, `0x11938` bit 26;
- T: `0x11908` bit 6, `0x11918` bit 19, `0x1191C` bits 0 and 4, `0x11920` bit 9, `0x11924` bits 5, 7, 8 and 27, `0x11928` bits 12
  and 26, `0x11938` bit 27;
- TX-cal alone: `0x11910` bit 13, `0x1192C` bit 14; RX-cal alone: `0x1192C` bit 11, `0x11908` bit 29.

`rx_tx_cal_attenuation(t)`: `0x11920` bit 20 = 0, bit 21 = 1; t = 1: bit 22 = 0, bit 23 = 1, `0x11928` [28:27] = 3; t = 0: bit 22 = 1,
bit 23 = 0, [28:27] = 1; both: `0x11924` [30:28] = 5, `0x11928` bits 13-14 set, `0x11910` [11:8] = 9.
`wfrf_hp_open` / `_close`: `0x11914` [2:0] = 1 and bits 3, 4 set / all cleared.
`wfrf_flt_vga_gain(g)`: entry g of a 42 × 4-byte constant table (`rx_flt_vga_gain` in `wf_rf_init.c`) into `0x11910` bits 22-26
and `0x11914` [17:15], bits 12-14 and [7:5]. Our code needs this table (168 bytes) for §4.2's gain search and §4.3's gain 40.

## 4. Record 187: the measured trims

### 4.1 Layout (`struct txrx_iq_trim_info`, 68 bytes, little-endian) [H]

| Bytes | Field | Type | Measured by | Applied to | BLE? |
|---|---|---|---|---|---|
| 0-3 | `dig_tx_i_gain` | u32, 10 bits used | TX IQ (§4.2 step 5) | D[`0x04`] = v >> 2, D[`0x61`] [1:0] = v & 3 | via BT trims (§5) |
| 4-7 | `dig_tx_i_phase` | s32, 10 bits | TX IQ (step 6) | D[`0x05`], D[`0x61`] [3:2] | via §5 |
| 8-11 | `dig_tx_q_gain` | u32, 10 bits | TX IQ (step 5) | D[`0x06`], D[`0x61`] [5:4] | via §5 |
| 12-15 | `dig_tx_q_phase` | s32, 10 bits | TX IQ (step 6) | D[`0x07`], D[`0x61`] [7:6] | via §5 |
| 16-19 | `dig_tx_i_dc` | s32, 10 bits | TX DC (step 4) | D[`0x08`], D[`0x62`] [1:0] | via §5 |
| 20-23 | `dig_tx_q_dc` | s32, 10 bits | TX DC (step 4) | D[`0x09`], D[`0x62`] [3:2] | via §5 |
| 24 | `txlo_cali` | u8, 7 bits | TX LO (step 3) | `0x1191C` [14:8] | yes (shared mixer) [M] |
| 25 | `txlo_calq` | u8, 7 bits | TX LO (step 3) | `0x11920` [6:0] | yes [M] |
| 26 | `dig_rx_i_gain` | u8 | RX IQ (§4.4) | D'[`0x09`] | no (Wi-Fi demodulator) [M] |
| 27 | `dig_rx_q_gain` | u8 | RX IQ | D'[`0x0B`] | no [M] |
| 28 | `dig_rx_i_phase` | s8 | RX IQ | D'[`0x0A`] | no [M] |
| 29 | `dig_rx_q_phase` | s8 | RX IQ | D'[`0x0C`] | no [M] |
| 30-31 | padding | | | | |
| 32-35 | `anl_rxdc_i` | s32 | RX DC (§4.3), last gain step | `0x11908` [15:7] = \|v\|, bit 16 = (v ≥ 0) | yes (shared RX) [M] |
| 36-39 | `anl_rxdc_q` | s32 | RX DC, last gain step | `0x11908` [25:17] = \|v\|, bit 26 = (v ≥ 0) | yes [M] |
| 40-43 | `flt_bw_1` | u32 | RX filter (§4.1b) | `0x1195C`; byte 2 also `0x11910` [21:14] and `0x11920` [17:10] | yes [M] |
| 44-47 | `flt_bw_2` | u32 | RX filter | `0x11960` | yes [M] |
| 48-63 | `rx_dc_tune[4][2]` | u16 × 8 | RX DC, one I / Q pair per gain step | RF-die word 0, entries 128-223 (§6) | yes [M] |
| 64-67 | `trim_mage` | u32 | CRC-16 of bytes 0-63 | check only; 0 means invalid | |

This agrees with HW §16.4 and sharpens it: the 1- and 2-bit "fields" found by perturbation are the low bits of these wider fields
(byte 24 is a 7-bit field at `0x1191C` [14:8]; byte 42 an 8-bit field at `0x11910` [21:14] and `0x11920` [17:10]; bytes 32-39 9-bit
magnitudes plus a sign flag). The bytes HW §16.4 lists as driving nothing are the upper bytes of these 32-bit fields. The
check is CRC-16/XMODEM, little-endian in bytes 64-65, `00 00` in 66-67 (HW §14.5); a stored check of 0 is refused.

`rx_dc_tune` entries and `0x11908` use one encoding: bit 9 = "non-negative", [8:0] = magnitude (`set_anl_rxdc_i/_q` return it).
`anl_rxdc_i/_q` in the record are signed integers rebuilt from `0x11908` by `rd_rx_iq_set`.

### 4.1b RX filter bandwidth (`wf_rx_tune_test`) [H for the steps; M for the meaning]

A hardware RC-tuning engine runs three times. Set `0x11904` [11:8] = 8 and `0x11950` bit 27. For k = 0, 1, 2 (named 6, 10, 20 MHz):
write `0x11968` bits [9:1] so that [9:0] = `0x1FC`, `0x122`, `0x0A0`; [17:11] = 15; [22:18] = 0; bit 23 = 0; bit 0 = 1 (start).
Poll `wlrf_get_(1)` until bit 5 is set (the SDK has no timeout: ours needs one), take [15:8] of that word as the result, clear bit 0.
Then clear `0x11950` bit 27 and write: `0x11920` [17:10] = r10, `0x11910` [21:14] = r10, `0x1195C` = `0xFF` | r6 << 8 | r10 << 16 |
r20 << 24, `0x11960` the same. So `flt_bw_1 == flt_bw_2` and byte 0 is always `0xFF`. (The emulator's 187 has `0xFF` in all
eight bytes: its model returns `0xFF`.) Three engine runs: well under a millisecond [L].

### 4.2 TX LO leakage, TX DC and TX IQ (`txiq_tune_soft`) [H]

`wf_rf_trim` (after `enter_wifi_rf_trim_region`, an empty weak hook): `wf_rx_tune_test`, then `wf_rf_en_set(2)`,
`rx_tx_cal_attenuation(1)`, then this routine.

The **metric** `dem_filter_dc(sel)` (§4.6) is the power the receiver sees at the frequency the step targets, through the
loop-back. Every search below is "set the value, measure, keep the smallest; the starting best is the metric at the start
value; only a strictly smaller metric wins".

1. Set-up: PHY reset; C[`0x15`] = 0, C[`0x14`] = 0; `confg_cca_for_trim` (C[`0x40`] and C[`0x41`] bit 7 cleared, C[`0x28`] = 0,
   C[`0x27`] = 5, BBP `0x04` bits 3-4 cleared); loop-back on; D[`0x00`] = 3, D[`0x5F`] = 16, D[`0x01`] = 20, D[`0x02`] = 0,
   D[`0x03`] = 0 (a test tone [M]); D'[`0x00`] = 62, D'[`0x13`] = 30, D'[`0x14`] = 56 then 52. TX gains 1023 / 1023, phases 0,
   DCs 0, `txlo_cali` = `txlo_calq` = 64.
2. `adc_pp_tune` (§4.6): raise the RX gain until the ADC peak exceeds 47.
3. TX LO: `txlo_cali` = 64, metric(0) as the start; sweep `txlo_cali` 0..127 with metric(0); set the best. Then the same for
   `txlo_calq` (with `cali` at its best). 2 × 128 measurements.
4. TX DC: DCs 0, `adc_pp_tune` again, then sweep I DC over the counter −200..200 with metric(0), set the best; then Q DC the same.
   The library passes the counter cut to a signed byte, so the values visited are −128..127 (some twice) and the best is kept as
   a signed byte. 2 × 401 measurements.
5. TX gain balance: D'[`0x13`] = 60 (the image [M]); gains 1023 / 1023; sweep x = −423..423: x < 0 → I gain = 1023 + x, Q = 1023;
   x ≥ 0 → I = 1023, Q gain = 1023 − x; metric(0). The best x is **kept as a signed byte** (the library truncates; an x beyond
   ±127 wraps, a latent bug a faithful copy may fix by keeping 16 bits). Set it. 847 measurements.
6. TX phase: phases 0; sweep p = −200..200 on the Q phase if the best x ≥ 0, else on the I phase; metric(0); best kept as a signed
   byte (same truncation); set it. 401 measurements.

Then `wfrf_hp_close`, `wf_rf_en_set(0)`, `0x11904` bit 15 cleared, and §4.3.

On our unit (from the BT trims in the log, §5): I gain ≥ 1016, Q gain 984-991, so the best x was about +32..39 and the Q phase
was swept; I DC in −3..3, Q DC in −7..−4.

### 4.3 RX DC (`rxdc_tune_soft`) [H]

Set-up: PHY reset, C[`0x15`] = 0, C[`0x14`] = 0, `confg_cca_for_trim`, loop-back; D[`0x00`] = 3, D[`0x5F`] = 16; D'[`0x00`] = 62,
D'[`0x1B`] = 56, D'[`0x08`] = `0xC0`, D'[`0x13`] = 0, D'[`0x14`] = 56 then 52; `wfrf_flt_vga_gain(40)`.

For gain step g = 0..3 (front-end settings: `0x1190C` [7:6], `0x11910` [11:8], `0x11910` [6:0]):
g0 = 0, 0, 127; g1 = 1, 0, 127; g2 = 1, 3, 69; g3 = 1, 9, 14.

1. I and Q settings 0. Step s = 256.
2. Twelve rounds: measure the I mean (metric sel 1); if it is > 0 and the I setting > −510, subtract s; if < 0 and < 510, add s;
   write the I setting. Then the same for Q with sel 2. Then s = s / 2 (s stays 1 from then on): 256, 128, ..., 2, 1, 1, 1, 1.
   (A signed successive approximation; the guard is checked before the step, so a setting can pass ±510; the register keeps 9
   magnitude bits.)
3. Write the final I and Q again and keep their encoded values (bit 9 = non-negative, [8:0] magnitude) as `rx_dc_tune[g]`.

96 measurements. The settings of g3 stay in `0x11908` and become `anl_rxdc_i/_q`. Then `wfrf_hp_open`, `wf_rf_en_set(3)`,
`rx_tx_cal_attenuation(0)`.

### 4.4 RX IQ (`rxiq_tune_soft`) [H; Wi-Fi only, M]

Set-up as §4.2 step 1 but D[`0x03`] = 17; RX gains 255 / 255, phases 0; D'[`0x00`] = 62, D'[`0x14`] = 56 then 4, D'[`0x13`] = 30;
`adc_pp_tune`; RX gains 255 / 255 again. Sweep x = −105..105: x < 0 → I gain = 255 + x, else Q gain = 255 − x; metric sel 3;
keep the best. Set it, then sweep the phase −50..50 (Q if the best x ≥ 0, else I), metric sel 3; set the best. 312 measurements.
Then `exit_wifi_rf_trim_region` (empty hook).

### 4.5 Read-back, check, save [H]

After the trim (or quick start), `tx_rx_bw_set_save` fills the 187 buffer from the hardware: `rd_tx_iq_set` (window D entries
`0x04`-`0x09`, `0x61`, `0x62`, `0x1191C`, `0x11920`), `rd_rx_iq_set` (D' `0x09`-`0x0C`, `0x11908`), `rd_bw_set` (`0x1195C`,
`0x11960`); `rx_dc_tune` was copied in by §4.3. One second later (`RFIinitUseTrimValue` × 1000 ms, a system timeout) it sets
`trim_mage` = CRC16(bytes 0-63) and writes VM 187 (`wifi_set_rf_trim_data`).

`wifi_trim_is_already`: only if `RFIinitUseTrimValue` is non-zero (SDK default 1; fm1-base's shim 1), read 68 bytes of VM 187;
valid when `trim_mage` is non-zero and equals CRC16(bytes 0-63); then `rx_dc_tune` is loaded for §6. `quick_start_wifi_trim`
applies the record: `txlo_cali/_q`, the six TX values (each a window-D write plus a read-modify-write of `0x61` / `0x62`),
`anl_rxdc_i/_q`, the four RX values, `0x1195C` / `0x11960`, `0x11904` [11:8] = 8, `0x11920` [17:10] and `0x11910` [21:14] from
`0x1195C` byte 2; it prints ` wifi quick_start_trim ok.` A missing or bad record prints `FAIL!!!` and runs the full trim.

fm1-base's shim note (`ble/shim/misc.c`): a 187 kept from a Bluetooth start made Wi-Fi deaf to access points, and a fresh trim
fixed it (2026-10-16). Something in the trim depends on state that differs between the two starts [L]; one more reason to
measure at every boot rather than keep a record.

### 4.6 The measurement primitives [H]

`dem_filter_dc(sel)`: D'[`0x15`] = 1, D'[`0x16`] = 1, D'[`0x15`] = 3, D'[`0x16`] = 3 (start the estimator [M]); read D'[`0x8D`]
until bits [1:0] are 0 (no timeout in the SDK); then:

| sel | Result |
|---|---|
| 0, 3 | I² + Q², I = signed 16 bits from D'[`0x81`]:D'[`0x82`], Q = D'[`0x84`]:D'[`0x85`] (high byte first) |
| 1 | the I mean: signed 24 bits D'[`0x81`]:D'[`0x82`]:D'[`0x83`] |
| 2 | the Q mean: signed 24 bits D'[`0x8A`]:D'[`0x8B`]:D'[`0x8C`] |
| other | I² + Q² from D'[`0x87`]:D'[`0x88`] and D'[`0x8A`]:D'[`0x8B`] |

`rd_adc_peak`: D'[`0x17`] = `0x82`, wait **1 ms**, read D'[`0x80`], D'[`0x17`] = 2.
`adc_pp_tune(0)`: g = 0, `wfrf_flt_vga_gain(g)`, read the peak twice (the second counts); while it is ≤ 47 and g ≤ 35: g + 1, apply,
read twice. Apply the final g. 2 ms to 74 ms per call; called three times.

## 5. The BT TX trims (derived, not stored) [H]

`keep_tx_iq_set` (`wf_rf_cal.c`) reads window D back right after the trim or quick start and derives the BT modem's TX
compensation; `bt_rf_analog_init` → `update_mdm_txiq_cmp` (`analog.c`) writes it and prints it. With the 187 fields of §4.1:

- `CMPUK1` = `dig_tx_i_gain` >> 3 (0..127); `CMPUK2` = `dig_tx_q_gain` >> 3;
- `CMPUK3` = (`dig_tx_i_phase` + `dig_tx_q_phase`) / 8, signed, rounded toward zero;
- `OFSI` = `dig_tx_i_dc` / 4, `OFSQ` = `dig_tx_q_dc` / 4, signed, rounded toward zero.

Writes, after `bt_rf_analog_init`'s base values (`0x2FC08` = `0x03100000`, `0x2FC0C` = `0x80808080`, `0x2FC10` = `0x80008080`):
`0x2FC10` [7:0] = `CMPUK1`, [15:8] = `CMPUK2`, [23:16] = (−`CMPUK3`) & `0xFF`; `0x2FC08` [9:0] = `OFSI` & `0x3FF`, [19:10] =
`OFSQ` & `0x3FF`. Log: ` BT_TX_IQ: CMPUK1 CMPUK2 CMPUK3`, ` BT_OF: OFSI OFSQ`.

Our unit (` BT_TX_IQ: 127 123 -3`, ` BT_OF: 0 -1`): `0x2FC10` = `0x80037B7F`, `0x2FC08` = `0x031FFC00`. `fm1_ble.h` writes
`0x80008080` and `0x03100000` (neutral: 128, 128, 0 and 0, 0), close to these, which is why BLE works there without them.
These are the "BT TX trims, live from BBP read-backs" of HW §16.2: in V15's boot 2 they come from the read-back of the 187 values
just written, which the emulator answers with 0.

## 6. The RF-die table (`wl_anl_actl_init`) [H]

`wl_rfd_actl_ram_init(0)` writes 256 entries, each word 0 (SPI command `0xE`) then word 1 (`0xD`), from the constant table
`wl_rfd_ram_lut` (in the application, `apps/common/net/wifi_conf.c`; its second variant, "stronger against interference", is the
one built; a separate `_40M` table is used with a 40 MHz crystal). The SDK documents the index ranges: 0-127 frequency, 128-223
RX gain (AGC index 0-95), 224-255 TX power (APC index 0-31). Patches:

| Entries | Word | Patch |
|---|---|---|
| 0-127 | 1 | OR in `bank(f) << 9`, f = word 1 bits [28:21] (the entry's fbds code; §3.2); bits [15:9] are zero in the table |
| 128-132 | 0 | keep [11:0]; [21:12] = `rx_dc_tune[0][0]`, [31:22] = `rx_dc_tune[0][1]` |
| 133-179 | 0 | the same with `rx_dc_tune[1]` |
| 180-185 | 0 | `rx_dc_tune[2]` |
| 186-223 | 0 | `rx_dc_tune[3]` |
| 224-255 | 0 | clear [5:0]; [2:0] = PA byte 5, [5:3] = PA byte 6 |
| 224-255 | 1 | clear bits [11:9], [20:15], [29:23]; [11:9] = PA byte 0, [17:15] = byte 1, [20:18] = byte 2, [25:23] = byte 3, [29:26] = byte 4 |

The group sizes 5 / 47 / 6 / 38 are HW §16.4's "5, 47, 6, 38 words per group". With `init_sel` 1 (the temperature retune) only
entries 0-127 are rewritten. Our replay already carries the base table (captured); what changes per unit is the bank patch, the
RX DC patch and nothing else (PA bytes are constants).

## 7. What is not calibration (and can stay as replayed or be left out)

- **The window-D read-modify-write block** that the replay leaves out (HW §16.2, 3,537 transactions, "a 128-step loop") is
  `rapp_init` (`wf_cfr_init.c`): a 128-entry constant table written into a TX back-end RAM through D[`0x5C`]-[`0x5E`] (write
  pulses and address are read-modify-writes, hence the read-backs), plus threshold settings at D[`0x51`], [`0x58`]. No
  measurement. It is the Wi-Fi transmitter's PA predistortion [M]; BLE does not use it (the FM-1 connects without it). Its
  read-backs also include the quick start's `0x61` / `0x62` writes and `keep_tx_iq_set`'s reads.
- `cfr_int` (crest-factor reduction) and VM 108's gains: Wi-Fi transmitter only [M].
- **Temperature retune**: every 3 s (`wl_temperature_drift_freq_trim`, when `wifi_temperature_drift_trim_on` = 1, the SDK
  default) the chip temperature ADC channel `0x3000F` × 90 is compared with the last retune's; after 8 samples, a change above
  2,200 runs `rf_pll_retune`: receive mode, `0x11950` = 0, `0x11940` bit 0 cleared and [7:6] = 2, `0x1193C` [21:18] = 14,
  [29:27] = 7, `wfa_pll_bank_scan(119)`, RF-die entries 0-127 again, `wf_rf_bsb_auto_ctl`. It moves the shared synthesizer's banks,
  so it matters to BLE over large temperature swings [M]. It is not HW §12 U13's 2 ms `0x11900` bit 14 routine; that one is not in
  these libraries.
- `bt_reload_trim` (BT single-carrier test mode only) rewrites §5's values and restores `0x1191C` / `0x11920` from
  `rf_dc_get_reload`.

## 8. Prerequisites, order, time, interrupts

The calibration needs, before it starts (all already in the replay before group 7) [H]:

1. Clocks and the Wi-Fi block out of reset (HW §16.1 groups 1-3; `wf_clk_rst_init`, `wdma_init`).
2. The first BBP load (group 4, `wf_phy_init`): the loop-back and the estimators are configured by it.
3. The crystal ramp with 106 (group 5), the analog init (group 6).
4. The VCO scan and the PLL at 2442 MHz (group 7 and the start of group 8).

It must finish before: the second BBP load (group 10, which also undoes the trim's BBP changes), the PA trims (group 9; the SDK
applies them after the trim), the RF-die table (group 11), and the BT block (group 14), which takes §5's values.

Interrupts: the SDK keeps them **on** throughout and runs from flash (`.rf.text.cache.L2.rf_trim` stays in flash unless
`CONFIG_RF_TRIM_CODE_AT_RAM`; `local_irq_disable` appears nowhere in these paths) [H]. Its loops have no timeouts; ours should.
Our replay's crystal-ramp burst with interrupts off (BLE-STACK.md §8.1) is our own precaution, not the SDK's. Nothing else
may drive the radio meanwhile (the trim transmits into its own receiver).

Time, estimated [L]: VCO scan about 45 ms (§3.2); the trim makes about 2,700 metric reads plus three `adc_pp_tune` (6-220 ms).
At roughly 30 BBP transactions a read (about 90,000 in all, as HW §5.2's "~96,000 BBP writes") and an estimator wait of unknown
length, expect **0.1 to 0.5 s** for a full start, against about 17 ms for V15's quick start. Measure it (§10).

Wi-Fi-only, not needed for BLE [M]: RX IQ (§4.4, record bytes 26-29), VM 108, `cfr_int`, `rapp_init`, the MAC set-up, the
temperature timer's Wi-Fi side. TX IQ / DC (§4.2 steps 4-6) feed BLE only through §5's compensation.

## 9. Feeding `fm1_ble_rf_init(x106, x107, x108, x187)`

The replay applies record bytes as fields of the writes that carry them (HW §16.4). So our code can measure, build a 187 record in
§4.1's layout (CRC included), and pass it in with the constants:

- `x106` = `0B 0B`; `x107` = `01 07 04 07 0B 01 07`; `x108` = 20 × `00`;
- `x187` = the measured record.

What has to change around the replay:

1. A measurement point. The capture marks no section where stock calls `wifi_trim_is_already`; it is right after the PLL writes
   and the 80 µs `0x11938` bit 18 pulse, before the first quick-start write (`0x1191C`, `txlo_cali_set`). Run the replay to there
   with a neutral 187, measure, build `x187`, then run the rest of the program with it. The quick-start writes then re-apply the
   measured values (they are what the trim left in the registers anyway).
2. Group 7: replace our bisection by §3.2 and keep its `limit[]` table; then patch RF-die word-1 entries 0-127 with `bank()`
   instead of the captured (emulator) banks; set fbds = 121 and its bank instead of the captured constants.
3. Group 11: the `rx_dc_tune` patch comes from `x187` bytes 48-63 already (trim fields); check it against §6's encoding.
4. `fm1_ble.h`: compute `0x2FC08` / `0x2FC10` from `x187` bytes 0-23 by §5 instead of the fixed first-written values.
5. A neutral 187 for step 1 and for anything left unmeasured: TX gains 1023 / 1023 (bytes 0-3, 8-11 = `FF 03 00 00`), phases and
   DCs 0, `txlo` 64 / 64, RX gains 255 / 255 and phases 0, `anl_rxdc` 0, `flt_bw` from the measurement (never the emulator's
   `0xFF`s), `rx_dc_tune` = `0x0200` × 8, the CRC. (Not the emulator's V15 187: its zeros are read-back artefacts.)

## 10. Open questions and the experiments that settle them

| # | Question | Experiment on a device |
|---|---|---|
| R1 | Does our implementation reproduce stock's numbers? | On our unit (VM intact): log our scan (`top`, `limit[]`, bank at 121), `x187` and the BT trims; compare with the JieLi build's log (` bank: 37`, `setbak:33`, `127 123 -3`, `0 -1`) and with the unit's VM 187 (`flr 0xe8015 68`). Small differences in TX IQ are normal (noise); the scan and the filter codes should match exactly |
| R2 | How long does each part take on hardware? | TIMER4 around the scan, each trim routine and `dem_filter_dc`'s estimator poll |
| R3 | Which parts does BLE need? | Run with a neutral 187 except one measured part at a time; compare the phone's RSSI of our advertising, the RSSI we report, connection drops and MIDI round trips over a few minutes, near and at 10 m |
| R4 | Are 106 / 107 the same on other units? | `flr 0xe8000 256` on a second unit with its VM (HW §12 U2) |
| R5 | The comparator's meaning (which of bits 17 / 18 is "above") | Not needed to copy the algorithm; reading `wlrf_get_(1)` across one bank's fbds sweep shows it |
| R6 | Does the 3 s temperature retune matter to BLE? | Heat / cool a connected unit (hand, fridge) without the retune; watch for drops |
| R7 | Whether the trim needs the replay's BBP state exactly as stock's first load leaves it | Expected yes; if a metric read never completes (D'[`0x8D`] stays busy), the first load or the loop-back is missing |

## 11. The smallest BLE radio on a unit with no VM, and the size of our code

Smallest set of steps (in replay order):

1. Constants for 106 / 107 / 108 (§3.1, §3.3, §3.4).
2. The VCO bank scan done as stock does it, with its `limit[]` table, fbds 121 and its bank (§3.2).
3. The RX filter calibration (§4.1b; three engine runs).
4. RX DC (§4.3; 96 reads) for `0x11908` and `rx_dc_tune`.
5. TX LO leakage (§4.2 steps 1-3; 256 reads), recommended: the shared mixer's carrier leak lands inside a BLE channel.
6. Neutral values for the rest of 187 (§9 item 5), the CRC, then the replay with this `x187`, the RF-die patches of §6 and BT
   trims from §5 (neutral values give stock's first-written ones).

Steps 4-6 of §4.2 (TX DC / gain / phase, 2,000 reads) and §4.4 are optional for BLE: add them if R3 shows a weak transmitter.

Size estimate [L]: about 600-800 lines of C (helpers exist in `fm1_ble_rf.h`: BBP windows, the analog read port), about 3-5 KB of
code, plus the 168-byte VGA gain table (a vendor constant: extract it at build time from the SDK library, as
`tools/ble_rf_capture.py` does for the other tables, or commit it under the owner's decision in `LICENSING.md`) and about 0.5 KB
of RAM during the scan and trim (the 48-byte bank table, a 68-byte record). The full trim adds about 200 lines.
