#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""fm1.toml lcd_panel and device/lcd.c's LCD_SEQ as lcd_init walks it (command, count, that many bytes; command 0 a
wait of one byte): with and without FM1B_LCD_PANEL the walk ends exactly at the table's end, every count fits
lcd_small, a wait has one byte; without it the table holds no panel register (the init as before); with it, the
panel's registers come after NORON, once each, at the values device/lcd.c names (VCOMS 0x18, GCTRL 0x56, VRHS 0x1F,
FRCTRL2 0x0F, both gammas 14 bytes). tools/build.py's opt_flags gives FM1B_LCD_PANEL=1 for lcd_panel = true only, and
FM1B_LCD_PANEL=0 in the environment wins. What the panel shows with them is only seen on an FM-1. Run from the repo
root:
  python3 tests/lcd_panel_test.py"""
import importlib
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
build = importlib.import_module("build")
SRC = (ROOT / "device" / "lcd.c").read_text()
PANEL = {0xB2, 0xB7, 0xBB, 0xC0, 0xC2, 0xC3, 0xC4, 0xC6, 0xD0, 0xE0, 0xE1}

failed = 0


def ok(cond, what):
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def table(panel):
    """-> LCD_SEQ's bytes as the C preprocessor leaves them with FM1B_LCD_PANEL = panel"""
    body = re.search(r"static const uint8_t LCD_SEQ\[\] = \{(.*?)\n\};", SRC, re.S).group(1)
    out, keep = [], [True]
    for line in body.split("\n"):
        s = line.strip()
        if s.startswith("#if"):
            keep.append(keep[-1] and (panel if s == "#if FM1B_LCD_PANEL" else None))
            if keep[-1] is None:
                raise SystemExit(f"lcd_panel_test: an #if it does not know in LCD_SEQ: {s}")
        elif s.startswith("#endif"):
            keep.pop()
        elif keep[-1]:
            out.append(line)
    text = re.sub(r"/\*.*?\*/", " ", "\n".join(out), flags=re.S)
    return [int(t, 0) for t in re.findall(r"0x[0-9A-Fa-f]+|\d+", text)]


def walk(seq):
    """-> the records (command, data) as lcd_init reads them, or None when the walk does not end at the table's end"""
    recs, r = [], 0
    while r < len(seq):
        if r + 1 >= len(seq) or r + 2 + seq[r + 1] > len(seq):
            return None
        recs.append((seq[r], seq[r + 2:r + 2 + seq[r + 1]]))
        r += 2 + seq[r + 1]
    return recs


small = int(re.search(r"static uint8_t lcd_small\[(\d+)\];", SRC).group(1))
for panel in (False, True):
    what = "with lcd_panel" if panel else "without lcd_panel"
    recs = walk(table(panel))
    ok(recs is not None, f"{what}: the walk ends exactly at the table's end")
    recs = recs or []
    ok(all(len(d) <= small for _, d in recs), f"{what}: every count fits lcd_small ({small} B)")
    ok(all(len(d) == 1 for c, d in recs if c == 0), f"{what}: every wait has its one byte")
    cmds = [c for c, _ in recs]
    if not panel:
        off = cmds
        ok(not PANEL & set(cmds), f"{what}: no panel register (the init as before)")
    else:
        regs = dict(recs)
        ok(cmds[:len(off)] == off and sorted(cmds[len(off):]) == sorted(PANEL),
           f"{what}: the panel's registers after the rest (NORON last of it), once each")
        ok(regs.get(0xBB) == [0x18] and regs.get(0xB7) == [0x56] and regs.get(0xC3) == [0x1F] and
           regs.get(0xC6) == [0x0F], f"{what}: VCOMS 0x18, GCTRL 0x56, VRHS 0x1F, FRCTRL2 0x0F (60 Hz)")
        ok(len(regs.get(0xE0, [])) == 14 and len(regs.get(0xE1, [])) == 14 and len(regs.get(0xB2, [])) == 5 and
           len(regs.get(0xD0, [])) == 2, f"{what}: both gammas 14 bytes, PORCTRL 5, PWCTRL1 2")

for conf, env, want in (({"lcd_panel": True}, None, True), ({}, None, False), ({"lcd_panel": False}, None, False),
                        ({"lcd_panel": True}, "0", False), ({}, "1", True)):
    build.CONF = conf
    saved = os.environ.pop("FM1B_LCD_PANEL", None)
    if env is not None:
        os.environ["FM1B_LCD_PANEL"] = env
    got = "-DFM1B_LCD_PANEL=1" in build.opt_flags()
    os.environ.pop("FM1B_LCD_PANEL", None)
    if saved is not None:
        os.environ["FM1B_LCD_PANEL"] = saved
    ok(got == want, f"opt_flags with {conf or 'no lcd_panel'}{f', FM1B_LCD_PANEL={env}' if env else ''}: "
                    f"FM1B_LCD_PANEL {'on' if want else 'off'}")

print("lcd-panel: " + ("FAILED" if failed else "all passed"))
sys.exit(1 if failed else 0)
