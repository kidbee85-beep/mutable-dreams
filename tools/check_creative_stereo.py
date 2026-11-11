"""Verify full-rate stereo against independently retained golden renders."""
from pathlib import Path
import subprocess,json,hashlib
r=Path(__file__).resolve().parents[1];ref=json.loads((r/'docs/checkpoints/2026-11-10-before-journey/full-stereo-reference.json').read_text());pub=json.loads((r/'docs/checkpoints/2026-11-10-before-journey/full-stereo-published-1.5.json').read_text());got=[];n=44100*12*2*4
with open('/private/tmp/md-creative-fullstereo.log','w') as log:
 p=subprocess.Popen([str(r/'app/build/native/fullstereo-tests')],stdout=subprocess.PIPE,stderr=log)
 for i in range(177):
  raw=p.stdout.read(n);assert len(raw)==n,(i,len(raw));got.append(hashlib.sha256(raw).hexdigest())
 assert not p.stdout.read(1);assert p.wait()==0
report={**ref,'reference':'pre-Journey private palette and original published 1.5 golden stereo','sha256_per_biome':got,'different_ids':[i for i,(a,b) in enumerate(zip(got,ref['sha256_per_biome'])) if a!=b]};report['bit_identical']=not report['different_ids'];report['published_1_5_different_ids']=[i for i,(a,b) in enumerate(zip(got,pub['legacy_sha256'])) if a!=b];out=r/'docs/development/creative/Full-Stereo-Comparison.json';out.write_text(json.dumps(report,indent=2)+'\n');print('Full stereo 44.1 kHz:',len(got),'Biomes;',report['full_stereo_sample_frames_compared'],'sample frames; differences',report['different_ids'],'vs 1.5',report['published_1_5_different_ids']);assert report['bit_identical'] and not report['published_1_5_different_ids']
