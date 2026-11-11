#!/usr/bin/env python3
"""Reproducible, coarse sound audit of all rendered factory Biomes.

These descriptors flag review targets. They are not a perceptual similarity
model and cannot replace listening, especially for slow evolving scenes.
"""
from __future__ import annotations

import array
import csv
import json
import math
import statistics
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "presets/catalog.json"
RESULT = ROOT / "docs/2.0-biome-audio-audit.json"
RATE = 11025
WINDOW = RATE // 4


def describe(path: Path) -> dict:
    values = array.array("f")
    with path.open("rb") as handle:
        values.frombytes(handle.read())
    windows = []
    last = 0.0
    for start in range(0, len(values) - WINDOW + 1, WINDOW):
        group = values[start : start + WINDOW]
        square = diff = crossings = peak = 0.0
        for sample in group:
            square += sample * sample
            diff += abs(sample - last)
            crossings += (sample >= 0) != (last >= 0)
            peak = max(peak, abs(sample))
            last = sample
        rms = math.sqrt(square / WINDOW)
        windows.append((rms, peak, diff / WINDOW, crossings / WINDOW))
    rms = [x[0] for x in windows]
    peak = [x[1] for x in windows]
    moving = [x[2] / max(x[0], 0.00001) for x in windows]
    zcr = [x[3] for x in windows]
    active = [i for i, x in enumerate(rms) if x > 0.01]
    edges = sum(rms[i] > max(0.018, rms[i - 1] * 1.9) for i in range(1, len(rms)))
    return {
        "rms": round(math.sqrt(sum(x * x for x in rms) / len(rms)), 5),
        "peak": round(max(peak), 5),
        "crest": round(max(peak) / max(0.001, statistics.mean(rms)), 3),
        "roughness": round(statistics.median(moving), 4),
        "zero_cross": round(statistics.median(zcr), 4),
        "activity": round(len(active) / len(rms), 3),
        "envelope_spread": round(statistics.pstdev(rms), 5),
        "event_edges": edges,
        "start_rms": round(statistics.mean(rms[: len(rms) // 2]), 5),
        "end_rms": round(statistics.mean(rms[len(rms) // 2 :]), 5),
    }


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: analyze_biome_audio.py RENDER_DIR")
    directory = Path(sys.argv[1])
    scenes = json.loads(CATALOG.read_text())["biomes"]
    source = {int(row["id"]): row for row in csv.DictReader((directory / "manifest.tsv").open(), delimiter="\t")}
    assert len(source) == len(scenes)
    rows = []
    for i, scene in enumerate(scenes):
        stats = describe(directory / ("%03d.f32" % i))
        rows.append({"id": i, "name": scene["name"], "family": scene["family"], **stats,
                     "quality_reductions": int(source[i]["quality_reductions"]),
                     "nonfinite": int(source[i]["nonfinite"])})
    # Use interpretable fixed feature scales; report a small distance only as
    # a review cue, never as a statement that two Biomes sound identical.
    keys = ("rms", "crest", "roughness", "zero_cross", "activity", "envelope_spread", "event_edges")
    scales = (0.10, 12.0, 0.18, 0.18, 1.0, 0.08, 8.0)
    pairs = []
    for i, a in enumerate(rows):
        for b in rows[i + 1 :]:
            score = math.sqrt(sum(((a[k] - b[k]) / scale) ** 2 for k, scale in zip(keys, scales)))
            pairs.append({"a": a["id"], "b": b["id"], "distance": round(score, 4)})
    pairs.sort(key=lambda row: row["distance"])
    result = {
        "status": "offline review cues, not listening verdicts",
        "rendered_seconds_per_biome": (directory / "000.f32").stat().st_size // 4 // RATE,
        "sample_rate_after_downmix": RATE,
        "count": len(rows),
        "nonfinite_total": sum(row["nonfinite"] for row in rows),
        "quality_reductions_total": sum(row["quality_reductions"] for row in rows),
        "rows": rows,
        "nearest_audio_descriptor_pairs": pairs[:30],
    }
    RESULT.write_text(json.dumps(result, indent=2) + "\n")
    print("Biomes:", len(rows), "nonfinite:", result["nonfinite_total"],
          "quality reductions:", result["quality_reductions_total"])
    print("Near descriptor pairs for listening:")
    for pair in pairs[:10]:
        print("  %.3f  %s / %s" % (pair["distance"], rows[pair["a"]]["name"], rows[pair["b"]]["name"]))
    print("Analysis:", RESULT)


if __name__ == "__main__":
    main()
