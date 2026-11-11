#!/usr/bin/env python3
"""Guard additive palette changes against the frozen 1.5 catalog. No hardware."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
old=json.loads((ROOT/'docs/checkpoints/2026-11-10-before-palette/catalog.json').read_text())
new=json.loads((ROOT/'presets/catalog.json').read_text())
assert new['tones'][:364]==old['tones'], 'A published sound preset changed.'
assert new['biomes'][:169]==old['biomes'], 'A published Biome changed.'
assert len(new['biomes'])>=213 and len(new['tones'])>=452
assert sorted(new['browse_order'])==list(range(len(new['biomes'])))
assert len({b['name'] for b in new['biomes']})==len(new['biomes'])
assert new['biomes'][64]['name']=='Blank Biome'
assert len({tuple(t['model'] for t in b['tracks']) for b in new['biomes'][169:177]})==8
assert all(b['intent'] and b['introduced']=='2.0' for b in new['biomes'][169:])
print(f"PASS: 169 original Biomes / 364 presets unchanged; {len(new['biomes'])-169} new ensembles / {len(new['tones'])-364} new tones; unique IDs and browse order.")
