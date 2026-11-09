#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""The debug stats read as a program reads them: the frames the host simulator sent for tests/dbgstats.txt (its
sysex_log, raw F0 .. F7) through tools/fm1_remote.py's parser (parse_stats_frames, no mido), each record's fields
against what the script did (26 simulated minutes on, a restart, a crash, a power-off); the end frames ("end 0",
"end 6"), the refused clear and the clear; and a power-on with the dumped flash (FM1B_FLASH) keeping the records.
    python3 tests/dbgstats_host_test.py HOST_EXE OUTDIR    (OUTDIR: where tests/dbgstats.txt ran)"""
import importlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
R = importlib.import_module("fm1_remote")
FAILS = []


def check(what, ok):
    print(f"{what:<70} {'ok' if ok else 'FAIL'}")
    if not ok:
        FAILS.append(what)


def frames(raw, head):
    """the texts of the frames F0 <head> .. F7 in a raw SysEx log, in order"""
    out, k = [], 0
    while True:
        a = raw.find(b"\xf0", k)
        if a < 0:
            return out
        b = raw.find(b"\xf7", a)
        if b < 0:
            return out
        if list(raw[a + 1:a + 4]) == head:
            out.append(raw[a + 4:b])
        k = b + 1


def answers(texts):
    """the frames split into answers, each up to its "end N" (or "cleared" / "clear failed")"""
    got, cur = [], []
    for t in texts:
        cur.append(t)
        if t.startswith(b"end ") or t in (b"cleared", b"clear failed"):
            got.append(cur)
            cur = []
    return got


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    exe, out = Path(sys.argv[1]), Path(sys.argv[2])
    raw = (out / "dbgstats_frames.bin").read_bytes()
    ans = answers(frames(raw, R.LS))
    check("five answers: end 0, end 6, clear failed, cleared, end 0",
          [a[-1] for a in ans] == [b"end 0", b"end 6", b"clear failed", b"cleared", b"end 0"])
    recs, end = R.parse_stats_frames(ans[0])
    check("none: no records, end 0 (not None: an answer)", recs == [] and end == 0)
    recs, end = R.parse_stats_frames(ans[1])
    check("six records, end 6, numbered 1/6 .. 6/6, seq 1..6", end == 6 and
          [(r["i"], r["n"], r["seq"]) for r in recs] == [(k, 6, k) for k in range(1, 7)])
    check("... the uptimes: 10, 15, 20, 25 min; after the restart 5 min; the crash's at 5:20",
          [r["up"] for r in recs] == [600, 900, 1200, 1500, 300, 320])
    check("... flags: five periodic with the switch on, the crash's crash + on",
          [r["flags"] for r in recs] == [9, 9, 9, 9, 9, 10])
    check("... boots 1 x4, then 2 (the restart; the crash's snapshot is of boot 2)",
          [r["boots"] for r in recs] == [1, 1, 1, 1, 2, 2])
    check("... crashes: 0 until the crash's own, 1", [r["crashes"] for r in recs] == [0, 0, 0, 0, 0, 1])
    check("... halves: about 172 a second of uptime", all(abs(r["halves"] - r["up"] * 44100 // 256) < 200
                                                          for r in recs))
    check("... one build, the app block raw (the template has none): 7 zero bytes",
          len({r["build"] for r in recs}) == 1 and all(r["app"] == {"raw": bytes(7)} for r in recs))
    check("... every field there", all({"late", "maxus", "gap", "t5late", "lock", "pass", "pstage", "prog", "stage",
                                         "wdt", "cpu2", "bt", "wifi", "nested", "stk", "ever"} <= set(r) for r in recs))
    check("... the stacks: not measured until the script's 900 bytes left, then since the start and the least ever",
          all(r["ever"] == {"main": -1, "irq": -1, "c1": -1} for r in recs[:5]) and
          recs[5]["stk"] == {"main": 900, "irq": 6000} and recs[5]["ever"] == {"main": 900, "irq": 6000, "c1": -1})
    crash, cend = R.parse_crash_frames(frames(raw, R.LH))
    check("the crash history: one crash, its stacks' room (32-byte units) and the guard hit",
          cend == 1 and len(crash) == 1 and crash[0]["stk"] == {"main": 896, "irq": 5984, "c1": -1, "guard": 1})
    check("... format_crash says so", R.format_crash(crash[0])[-1] ==
          "  stk main 896 irq 5984 c1 -1 bytes left, a stack ran into its guard")
    recs2, end2 = R.parse_stats_frames(ans[1][:-1])
    check("no end frame: end None (an answer lost), the records still read", end2 is None and len(recs2) == 6)
    line = R.format_stats(recs[5])
    check("format_stats: a reader's line", line.startswith("stat 6/6 seq 6 up 0:05 [crash,on] boots 2 crashes 1"))
    lay = R.parse_stats_frames([b"stat 1/1 seq 3 up 7", b"app 1/1 c10 41 drops 4660", b"end 1"])[0][0]
    check("an app's layout: its fields by name", lay["app"] == {"c10": 41, "drops": 4660} and lay["seq"] == 3)

    # a power-on with the flash dumped after the six (the records are the flash's, not the simulator's RAM)
    script = out / "dbgstats_boot.txt"
    script.write_text("expect stats 6\nexpect stats_on 0\nexpect stats_pending 0\nexpect stk_ever_main 900\n"
                      "expect stk_ever_irq 6000\n")
    r = subprocess.run([str(exe), str(script), str(out)], capture_output=True, text=True,
                       env={"FM1B_FLASH": str(out / "dbgstats_flash.bin"), "PATH": "/usr/bin:/bin"})
    check("power-on with the dumped flash: the six records there, the least ever from them",
          r.returncode == 0 and "FAIL" not in r.stdout)
    print("all ok" if not FAILS else f"FAIL: {len(FAILS)}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
