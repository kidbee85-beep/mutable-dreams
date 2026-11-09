# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""tools/ram_run.py, the build's check that a function is in the RAM code run (tools/build.py: the own Bluetooth
stack's fm1_rf_burst_run), on nm lines as the build reads them: inside _rt_start.._rt_end wherever the run is (the
fixed RAMTEXT region at 0x01C00000, 32 KiB, or packed in one SRAM and longer), outside it, or a symbol missing."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import ram_run  # the module under test, from tools/ (after the path is set)

FAILS = []


def ok(cond: bool, what: str) -> None:
    print(f"  {'ok  ' if cond else 'FAIL'} {what}")
    if not cond:
        FAILS.append(what)


def nm(rt_start: int, rt_end: int, burst: int | None) -> str:
    lines = [f"{rt_start:08x} T _rt_start", f"{rt_end:08x} T _rt_end", "02000120 T _start"]
    if burst is not None:
        lines.append(f"{burst:08x} t fm1_rf_burst_run")
    return "\n".join(lines) + "\n"


FIXED = (0x01C00000, 0x01C06A40)          # own-bt's RAMTEXT region: the run inside 0x01C00000..0x01C08000
PACKED = (0x01C00000, 0x01C0B200)         # one packed SRAM (the run longer than 32 KiB, no fixed end)
ok(ram_run.in_ram_text(nm(*FIXED, 0x01C01234), "fm1_rf_burst_run"), "in the fixed RAMTEXT run")
ok(ram_run.in_ram_text(nm(*PACKED, 0x01C09F00), "fm1_rf_burst_run"),
   "in a packed run past 0x01C08000 (the old fixed bound refused it)")
ok(not ram_run.in_ram_text(nm(*FIXED, 0x01C07000), "fm1_rf_burst_run"),
   "past _rt_end, though below 0x01C08000 (.data, not RAM code)")
ok(not ram_run.in_ram_text(nm(*FIXED, 0x0201A000), "fm1_rf_burst_run"), "in XIP")
ok(not ram_run.in_ram_text(nm(*FIXED, FIXED[1]), "fm1_rf_burst_run"), "at _rt_end itself (outside)")
ok(ram_run.in_ram_text(nm(*FIXED, FIXED[0]), "fm1_rf_burst_run"), "at _rt_start itself (inside)")
ok(not ram_run.in_ram_text(nm(*FIXED, None), "fm1_rf_burst_run"), "not linked at all")
ok(not ram_run.in_ram_text("01c01234 t fm1_rf_burst_run\n", "fm1_rf_burst_run"), "no _rt_start / _rt_end")
ok(ram_run.symbol(nm(*FIXED, 0x01C01234) + "01c01300 t fm1_rf_burst_run_x\n", "fm1_rf_burst_run") == 0x01C01234,
   "a longer name with the same start is not the symbol")
print("all ok" if not FAILS else f"{len(FAILS)} FAILED")
sys.exit(1 if FAILS else 0)
