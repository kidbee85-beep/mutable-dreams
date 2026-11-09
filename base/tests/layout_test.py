#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""device/app_packed.ld's packed SRAM (fm1.toml ram_layout = "packed"), as tools/build.py reads it: one region from the
SRAM's bottom up to the stacks' first guard band, RAM code first (the guard's one window outside XIP), then .data, .bss
and the pool, in that order; NOINIT where it was. device/app.ld, the default, keeps its fixed regions (an app's RAM
code and the cache RAM in them too), and app_wifi.ld's are still read for its checks. Run from the repo root:
  python3 tests/layout_test.py"""
import importlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
build = importlib.import_module("build")
APP = (ROOT / "device" / "app_packed.ld").read_text()
FIXED = (ROOT / "device" / "app.ld").read_text()
WIFI = (ROOT / "device" / "app_wifi.ld").read_text()

failed = 0


def ok(cond, what):
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def w_cache(script):
    return re.search(r"\.cache_ram \(NOLOAD\) : \{ _cr_start = \.;[^}]*_cr_end = \.; \} > CACHE", script) is not None


r = build.memory_regions(APP)
guard0 = int(re.search(r"_guard0 = (0x[0-9A-Fa-f]+);", APP).group(1), 16)
ok(sorted(r) == ["CACHE", "NOINIT", "SRAM", "XIP"], f"app_packed.ld: XIP, SRAM, NOINIT and CACHE only ({sorted(r)})")
ok(r["SRAM"][0] == 0x01C00000 and sum(r["SRAM"]) == guard0 == 0x01C74000,
   f"SRAM from 0x01C00000 to the stacks' guard band ({r['SRAM']})")
ok(r["NOINIT"] == (0x01C7C000, 0x3D50) and r["XIP"] == (0x02000120, 0x9DFBC), "NOINIT and XIP unchanged")
ok("_app_xip_end = ORIGIN(XIP) + 0x8DFBC;" in APP, "_app_xip_end is the app slot's end (the boot's bound)")
ok(r["CACHE"] == (0x01F28000, 0x7000) and w_cache(APP), "CACHE: the data-cache ways 0..6 (fm1.toml cache_ram_ways), .cache_ram NOLOAD there")
heads = [(m.start(), m.group(1)) for m in re.finditer(r"^\s*(\.\w+)\s*(?:\(NOLOAD\))?\s*:", APP, re.M)]
order = [[n for at, n in heads if at < m.start()][-1] for m in re.finditer(r"\}\s*>\s*SRAM\b", APP)]
ok(order == [".ram_text", ".data", ".bss", ".pool"], f"packed in order: RAM code, .data, .bss, the pool ({order})")
ok(re.search(r"\.ram_text\s*:\s*\{\s*_rt_start = \.;", APP) and "_rt_end = .; } > SRAM AT > XIP" in APP,
   "the RAM code run is _rt_start.._rt_end (the guard's PC window), loaded from XIP")
ok("ASSERT(_sram_end == _guard0" in APP, "the link fails if SRAM no longer ends at the stacks")

f = build.memory_regions(FIXED)
ok(sorted(f) == ["CACHE", "NOINIT", "POOL", "RAM", "RAMTEXT", "XIP"] and f["RAMTEXT"] == (0x01C00000, 0x8000)
   and f["RAM"] == (0x01C08000, 96 * 1024) and f["POOL"] == (0x01C20000, 0x54000), f"app.ld (the default): its fixed regions ({f})")
ok(f["NOINIT"] == r["NOINIT"] and f["XIP"] == r["XIP"] and f["CACHE"] == r["CACHE"] and w_cache(FIXED),
   "app.ld: the same XIP, NOINIT and CACHE (.cache_ram there) as app_packed.ld")
ok("_ar_start = .; *(.app_ram .app_ram.*) . = ALIGN(4); _ar_end = .;" in FIXED and "_rt_end = .; } > RAMTEXT AT > XIP" in FIXED,
   "app.ld: an app's RAM code (.app_ram) inside .ram_text, in RAMTEXT")
ok(all(re.search(p, FIXED) for p in (r"_guard0 = 0x01C74000;", r"_sstack_top = 0x01C7C000;")), "app.ld: the stacks where they were")

w = build.memory_regions(WIFI)
ok(w["CACHE"] == (0x01F28000, 0x7000) and w_cache(WIFI), "app_wifi.ld: the same CACHE region and .cache_ram")
ok(w["RAMTEXT"] == (0x01C00000, 0x6000) and w["RAM"] == (0x01C08000, 128 * 1024) and w["POOL"] == (0x01C28000, 0x4C000),
   f"app_wifi.ld's fixed regions read ({w})")

print("layout: " + ("FAILED" if failed else "all passed"))
sys.exit(1 if failed else 0)
