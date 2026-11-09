#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""fm1.toml cpu2_stack as tools/build.py reads it (cpu2_stack_flags): none leaves the build as it was (felucca-bsp's
2,816 B), a multiple of 8 from 1024 to 16384 becomes FM1_CPU1_USTACK_BYTES, anything else stops the build with a
message; the bsp's own default passes the same check. Run from the repo root:
  python3 tests/cpu2_stack_test.py"""
import importlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
build = importlib.import_module("build")

failed = 0


def ok(cond, what):
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def flags(value):
    """-> the flags for fm1.toml cpu2_stack = value (None: not set), or the build's stop message"""
    build.CONF = {} if value is None else {"cpu2_stack": value}
    try:
        return build.cpu2_stack_flags()
    except SystemExit as e:
        return f"stop: {e}"


ok(flags(None) == [], "no cpu2_stack: no flag (the bsp's default stack)")
for n in (1024, 2816, 4096, 16384):
    ok(flags(n) == [f"-DFM1_CPU1_USTACK_BYTES={n}"], f"cpu2_stack = {n}: FM1_CPU1_USTACK_BYTES={n}")
for bad in (1016, 1020, 4100, 16392, 0, -4096, True, "4096", 4096.0):
    got = flags(bad)
    ok(isinstance(got, str) and "cpu2_stack" in got and "1024..16384" in got,
       f"cpu2_stack = {bad!r}: the build stops and says why ({got})")
bsp = (ROOT / "bsp" / "chip" / "fm1_cpu1.h").read_text()
m = re.search(r"#define FM1_CPU1_USTACK_BYTES (\d+)", bsp)
ok(m is not None and flags(int(m.group(1))) == [f"-DFM1_CPU1_USTACK_BYTES={m.group(1)}"],
   f"felucca-bsp's default ({m.group(1) if m else 'not found'} B) passes the same check")

print("cpu2-stack: " + ("FAILED" if failed else "all passed"))
sys.exit(1 if failed else 0)
