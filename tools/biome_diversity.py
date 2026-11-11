#!/usr/bin/env python3
"""Audit factory Biome contrast without changing saved IDs or DSP code.

The distance is a browsing heuristic, not a measurement of musical quality.
Listen to every suggested transition before it becomes an instrument control.
"""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "presets/catalog.json"
OUTPUT = ROOT / "docs/2.0-biome-contrast.json"


def profile(scene: dict) -> dict:
    tracks = [t for t in scene["tracks"] if not t["mute"] and t["level"] > 0]
    by_model = Counter()
    total_level = sum(t["level"] for t in tracks) or 1
    for track in tracks:
        by_model[track["model"]] += track["level"] / total_level
    return {
        "models": by_model,
        "rhythm": sum(t["hits"] / max(1, t["length"]) for t in tracks) / max(1, len(tracks)),
        "brightness": sum(t["cutoff"] * t["level"] for t in tracks) / total_level / 1000,
        "tempo": scene["tempo"],
        "space": scene["macro"][3] / 1000,
        "family": scene["family"],
        "kit": int(bool(scene.get("kit", {}).get("enabled", 0))),
    }


def distance(a: dict, b: dict) -> float:
    models = set(a["models"]) | set(b["models"])
    model_delta = sum(abs(a["models"][m] - b["models"][m]) for m in models) / 2
    rhythm_delta = min(1.0, abs(a["rhythm"] - b["rhythm"]) * 2)
    tempo_delta = min(1.0, abs(a["tempo"] - b["tempo"]) / 80)
    brightness_delta = abs(a["brightness"] - b["brightness"])
    space_delta = abs(a["space"] - b["space"])
    return round(
        0.55 * model_delta
        + 0.15 * rhythm_delta
        + 0.10 * tempo_delta
        + 0.10 * brightness_delta
        + 0.05 * space_delta
        + 0.03 * (a["family"] != b["family"])
        + 0.02 * (a["kit"] != b["kit"]),
        4,
    )


def main() -> None:
    source = json.loads(CATALOG.read_text())
    scenes = source["biomes"]
    order = source["browse_order"]
    assert len(scenes) == len(order) == len(set(order))
    assert sorted(order) == list(range(len(scenes)))
    profiles = [profile(x) for x in scenes]
    current = [
        {"from": a, "to": b, "score": distance(profiles[a], profiles[b])}
        for a, b in zip(order, order[1:])
    ]
    too_similar = sorted(current, key=lambda x: x["score"])[:20]
    # A deliberately different candidate for each Biome. Exclude the Blank
    # slot from destinations and leave existing browse order untouched.
    blank = next((i for i, s in enumerate(scenes) if "blank" in s["name"].lower()), -1)
    contrast = []
    for i, scene in enumerate(scenes):
        choices = [(distance(profiles[i], profiles[j]), j) for j in range(len(scenes)) if j != i and j != blank]
        score, target = max(choices)
        contrast.append({"from": i, "to": target, "score": score})
    payload = {
        "status": "offline candidate; listening review required",
        "source": "presets/catalog.json",
        "count": len(scenes),
        "existing_browse_order_preserved": True,
        "nearest_current_transitions": too_similar,
        "contrast_jump_candidates": contrast,
    }
    OUTPUT.write_text(json.dumps(payload, indent=2) + "\n")
    print(f"{len(scenes)} Biomes measured; {sum(x['score'] < .25 for x in current)} of {len(current)} adjacent transitions score under 0.25")
    for row in too_similar[:5]:
        print(f"  {scenes[row['from']]['name']} -> {scenes[row['to']]['name']}: {row['score']}")
    print(f"Candidate map: {OUTPUT}")


if __name__ == "__main__":
    main()
