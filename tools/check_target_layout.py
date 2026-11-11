#!/usr/bin/env python3
"""Offline store-map gate for the proposed SMK-37 Elite port.

This checks address arithmetic only. It never opens a USB device and does not
qualify a firmware package for installation.
"""
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROFILE = json.loads((ROOT / "docs/2.0-target-profile.json").read_text())
HEADER = (ROOT / "app/target_storage.h").read_text()


def intersects(a: tuple[int, int], b: tuple[int, int]) -> bool:
    return a[0] < b[1] and b[0] < a[1]


def check_map(addresses: list[int], *, expect_safe: bool) -> None:
    pair = PROFILE["store_pair_bytes"]
    assert len(addresses) == PROFILE["object_count"]
    extents = [(start, start + pair) for start in addresses]
    assert all(start % 4096 == 0 and end <= PROFILE["flash_bytes"] for start, end in extents)
    assert all(not intersects(a, b) for i, a in enumerate(extents) for b in extents[i + 1 :])
    protected = [(w["start"], w["end"]) for w in PROFILE["protected_windows"]]
    safe = all(not intersects(e, p) for e in extents for p in protected)
    assert safe is expect_safe
    if expect_safe:
        allowed = [(w["start"], w["end"]) for w in PROFILE["store_windows"]]
        assert all(any(a <= e[0] and e[1] <= b for a, b in allowed) for e in extents)


def main() -> None:
    raw = re.search(r"#define MD_TARGET_STORE_MAP\s*\{([^}]+)\}", HEADER)
    assert raw, "FM-1 storage map not found"
    fm1 = [int(token.rstrip("uU"), 0) for token in raw.group(1).split(",")]
    check_map(fm1, expect_safe=False)

    proposed = [0] * PROFILE["object_count"]
    for window in PROFILE["store_windows"]:
        for i in range(window["object_count"]):
            proposed[window["first_object"] + i] = window["start"] + i * PROFILE["store_pair_bytes"]
    check_map(proposed, expect_safe=True)

    used = len(proposed) * PROFILE["store_pair_bytes"]
    available = sum(w["end"] - w["start"] for w in PROFILE["store_windows"])
    print("FM-1 object map: unsafe on SMK-37 Elite (expected)")
    print(f"Proposed Elite object map: 24 isolated pairs, {used} / {available} bytes")
    print("This is an offline gate only; no Elite package or device write was made.")


if __name__ == "__main__":
    main()
