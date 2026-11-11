from pathlib import Path
import json,zipfile,sys,collections
R=Path(__file__).resolve().parents[1];O=Path(sys.argv[1]).resolve();O.mkdir(exist_ok=True)
with zipfile.ZipFile(R/'../../outputs/Mutable-Dreams-2.0-Living-Lights/Mutable-Dreams-2.0-Living-Lights-Private-Source.zip') as z:old=json.loads(z.read('mutable-dreams-2.0-dev/presets/catalog.json'))
new=json.loads((R/'presets/catalog.json').read_text());assert new['biomes'][:197]==old['biomes'] and new['tones'][:420]==old['tones']
assert len(new['biomes'])==213 and len(new['tones'])==452
assert len({t['name'] for t in new['tones'][420:]})==32 and not ({t['name'] for t in new['tones'][420:]} & {t['name'] for t in old['tones']})
assert len({b['name'] for b in new['biomes']})==213
family_names=['Floating Cells','Medusae','Deep Roots','Currents','Reed Beds','Orbital Seeds','Membranes','Mineral Facets','Mycelium','Lantern Pods']
def family(b):
 x=(b['seed']^((b['id']+1)*0x9e3779b9))&0xffffffff;x^=x>>16;x=(x*0x85ebca6b)&0xffffffff;return (x^(x>>13))%10
counts=collections.Counter(family(b) for b in new['biomes']);assert len(counts)==10
representatives={f:next(b['id'] for b in new['biomes'] if family(b)==f) for f in range(10)}
assert len({tuple(t['model'] for t in b['tracks']) for b in new['biomes'][197:]})==16
script=['wait 3400','tap HOME']
for f,biome in representatives.items():script+= [f'scene {biome}','wait 800',f'expect world_family {f}',f'shot Family-{f:02}.png','wait 900',f'shot Family-{f:02}-Motion.png']
for id in [197,198,200,204,208,210,211,212]:script+= [f'scene {id}','wait 800',f'shot World-{id}.png']
script+=['journey 0 0 18','page 1','wait 150','expect model 18','shot Bow-Sound.png','press EDIT','wait 900','shot Bow-Help.png','release EDIT','tap HOME','expect help -1','page 1','journey 0 0 19','wait 150','expect model 19','shot Aero-Sound.png','press EDIT','wait 900','shot Aero-Help.png','release EDIT','tap HOME','expect help -1','page 11','spin K1 -10','expect motion 0','page 0','journey 27 0 0','wait 2000','shot Still-A.png','wait 1000','shot Still-B.png','expect invalid 0','expect queue_drops 0']
(O/'Screens').mkdir(exist_ok=True);(O/'Graphics.txt').write_text('\n'.join(script)+'\n')
report={'legacy_biomes_preserved':197,'legacy_tones_preserved':420,'new_biomes':16,'new_tones':32,'visual_families':family_names,'family_population':dict(counts),'representative_ids':representatives,'new_engine_signatures_distinct':16,'browse_first_12':new['browse_order'][:12]}
(O/'Catalog-Verification.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS all197 preceding worlds /420 tones unchanged, 16 new engine signatures, 10 visual families represented.')
