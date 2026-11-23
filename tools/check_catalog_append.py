#!/usr/bin/env python3
"""Guard the catalog against the published 2.3 one (docs/checkpoints/2026-11-19-v2.3/catalog.json, generated at the tag v2.3):
every 2.3 tone, Biome, family index and the order of the 2.3 wheel stays; new entries only append. No hardware.
Run after tools/generate_presets.py. (tools/check_palette_20.py guards the 2.1 and 2.2 prefix and is out of date for the later
authors' labels.)"""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
old = json.loads((ROOT / 'docs/checkpoints/2026-11-19-v2.3/catalog.json').read_text())
new = json.loads((ROOT / 'presets/catalog.json').read_text())
assert new['tones'][:len(old['tones'])] == old['tones'], 'a 2.3 tone changed'
assert new['biomes'][:len(old['biomes'])] == old['biomes'], 'a 2.3 Biome changed'
assert new['family_short'][:len(old['family_short'])] == old['family_short'], 'a family index moved'
assert [i for i in new['browse_order'] if i < len(old['biomes'])] == old['browse_order'], 'the order of the 2.3 wheel changed'
assert sorted(new['browse_order']) == list(range(len(new['biomes']))), 'the wheel misses a Biome or repeats one'
assert len({b['name'] for b in new['biomes']}) == len(new['biomes']), 'two Biomes share a name'
added = [t['name'] for t in new['tones'][len(old['tones']):]]
assert len(set(added)) == len(added) and not set(added) & {t['name'] for t in old['tones']}, 'a new tone name is not new'
print(f"PASS: every 2.3 tone, Biome, family index and the 2.3 wheel order is unchanged; {len(new['tones']) - len(old['tones'])} tones and "
      f"{len(new['biomes']) - len(old['biomes'])} Biomes appended with new names.")
