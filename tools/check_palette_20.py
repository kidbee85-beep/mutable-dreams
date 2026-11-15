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
assert all(b['intent'] and b['introduced'] in ('2.0','2.2') for b in new['biomes'][169:])
# 2.2 development: the published 2.1 atlas (272 Biomes / 632 tones) stays byte for byte.
v21=json.loads((ROOT/'docs/checkpoints/2026-11-13-v2.1/catalog.json').read_text())
assert new['tones'][:632]==v21['tones'], 'A 2.1 sound preset changed.'
assert new['biomes'][:272]==v21['biomes'], 'A 2.1 Biome changed.'
assert new['family_short'][:len(v21['family_short'])]==v21['family_short'], 'A 2.1 family index moved.'
assert [i for i in new['browse_order'] if i<272]==v21['browse_order'], '2.1 wheel order changed.'
assert all(b['introduced']=='2.2' and all(t['model']==11 for t in new['tones'][632:]) for b in new['biomes'][272:])
print(f"PASS: 2.1 atlas unchanged (272 Biomes / 632 tones / families / wheel order); {len(new['biomes'])-272} new Biomes / {len(new['tones'])-632} new Satin tones.")
print(f"PASS: 169 original Biomes / 364 presets unchanged; {len(new['biomes'])-169} new ensembles / {len(new['tones'])-364} new tones; unique IDs and browse order.")
