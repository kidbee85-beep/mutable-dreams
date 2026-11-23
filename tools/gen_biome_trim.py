#!/usr/bin/env python3
"""2.4: the loudness trim of every factory Biome, from the harness measurement (tests/audio_qa.cpp, `biomes`).

usage: gen_biome_trim.py BIOMES.tsv OUT.inc [--target LUFS] [--peak-cap DBTP] [--max-gain DB]

Raises only. A Biome below the target is lifted to it, never by more than --max-gain and never so far that its
true peak passes --peak-cap (the loudest true peak any Biome has today, so nothing gets louder than the loudest
Biome already is). Biomes at or above the target, the Blank Biome and User Biomes stay as they were.
The table is in half-dB steps (int8). Regenerate it after any change that moves the sound of the Biomes:
`audioqa-tests trimcheck` fails when the table no longer matches."""
import argparse, csv, sys
ap = argparse.ArgumentParser()
ap.add_argument("tsv"); ap.add_argument("out")
ap.add_argument("--target", type=float, default=-27.0)
ap.add_argument("--peak-cap", type=float, default=-13.7)
ap.add_argument("--max-gain", type=float, default=16.0)
a = ap.parse_args()
rows = list(csv.DictReader(open(a.tsv), delimiter="\t"))
trim = []
for r in rows:
    lufs, tp = float(r["lufs"]), float(r["tp_db"])
    if lufs < -100:                       # silent: the Blank Biome
        trim.append(0); continue
    gain = min(a.target - lufs, a.peak_cap - tp, a.max_gain)
    trim.append(max(0, int(round(gain * 2))))
n = len(trim)
lines = ["/* SPDX-License-Identifier: MIT OR GPL-3.0-only",
         " * 2.4: loudness trim of the factory Biomes, half-dB steps, from tools/gen_biome_trim.py.",
         f" * target {a.target} LUFS, true peak cap {a.peak_cap} dBTP, at most +{a.max_gain} dB, raise only. Generated: do not edit. */",
         f"static const int8_t biome_trim[{n}]={{"]
for i in range(0, n, 24):
    lines.append(" " + ",".join(str(v) for v in trim[i:i+24]) + ("," if i + 24 < n else ""))
lines.append("};")
open(a.out, "w").write("\n".join(lines) + "\n")
nz = [t for t in trim if t]
print(f"{n} Biomes, {len(nz)} raised, max +{max(trim)/2:.1f} dB, mean of raised +{sum(nz)/max(1,len(nz))/2:.1f} dB", file=sys.stderr)
