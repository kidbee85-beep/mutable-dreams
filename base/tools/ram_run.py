# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""Where the image's RAM code run is, from its symbols (nm's lines: "ADDRESS TYPE NAME"): device/app.ld puts every
.ram_text function between _rt_start and _rt_end, wherever the linker script places that run (a fixed RAMTEXT region,
or packed at the bottom of one SRAM). tools/build.py asks it whether a function that must run from RAM is there."""
from __future__ import annotations

import re


def symbol(syms: str, name: str) -> int | None:
    """NAME's address in nm's output, None when it is not there"""
    m = re.search(r"^([0-9a-fA-F]+) .*\s" + re.escape(name) + r"$", syms, re.M)
    return int(m.group(1), 16) if m else None


def in_ram_text(syms: str, name: str) -> bool:
    """NAME is linked inside the RAM code run [_rt_start, _rt_end)"""
    lo, hi, at = symbol(syms, "_rt_start"), symbol(syms, "_rt_end"), symbol(syms, name)
    return None not in (lo, hi, at) and lo <= at < hi
