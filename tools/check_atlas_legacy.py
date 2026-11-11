"""Compare every preceding private/public Biome at full-rate stereo, not a downsampled proxy."""
from pathlib import Path
import subprocess,hashlib,json
R=Path(__file__).resolve().parents[1]
reference=json.loads((R/'docs/checkpoints/2026-11-10-before-journey/full-stereo-reference.json').read_text())
O=(R/'../../outputs/Mutable-Dreams-2.0-Living-Atlas').resolve()
frames=44100*12;hashes=[]
with (O/'Legacy-Stereo.log').open('w') as err:
    p=subprocess.Popen([str(R/'app/build/native/fullstereo-tests')],stdout=subprocess.PIPE,stderr=err)
    for i in range(189):
        raw=p.stdout.read(frames*8)
        assert len(raw)==frames*8,(i,len(raw))
        hashes.append(hashlib.sha256(raw).hexdigest())
    assert not p.stdout.read(1)
    assert p.wait()==0
different=[i for i,(a,b) in enumerate(zip(hashes[:177],reference['sha256_per_biome'])) if a!=b]
result=dict(previous_biomes=177,new_biomes=12,sample_rate=44100,channels=2,seconds_per_biome=12,
            differing_legacy_ids=different,legacy_bit_identical=not different,sha256_per_biome=hashes)
(O/'Legacy-Stereo-Comparison.json').write_text(json.dumps(result,indent=2)+'\n')
assert not different,different
print('PASS all 177 preceding Biomes retain exact full-rate stereo, including all 169 published 1.5 Biomes.')
