"""Audit additive catalog, legacy stereo, and measured variation of new audio."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from collections import Counter
import hashlib, json, subprocess
import numpy as np

R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Collection').resolve()
reference=json.loads((O/'Reference-Catalog.json').read_text());new=json.loads((R/'presets/catalog.json').read_text())
assert reference['tones']==new['tones'][:452] and reference['biomes']==new['biomes'][:213]
assert [i for i in new['browse_order'] if i<213]==reference['browse_order']
assert len(new['tones'])==516 and len(new['biomes'])==233
assert set(new['browse_order'])==set(range(233)) and new['browse_order'][-1]==64
added=new['tones'][452:];worlds=new['biomes'][213:]
assert len(set(t['name'] for t in added))==64
assert not set(t['name'] for t in added)&set(t['name'] for t in reference['tones'])
assert len(set(tuple(t[k] for k in new['tone_fields']) for t in added))==64
assert {t for b in worlds for t in b['tones']}==set(range(452,516))
assert len({tuple(t['model'] for t in b['tracks']) for b in worlds})==20
assert all(b['tones']==[new['tones'][t]['id'] for t in b['tones']] for b in worlds)

def read_full(exe,count,tag):
    result=[];measure=[]
    with (O/f'{tag}-Render.log').open('w') as log:
        p=subprocess.Popen([str(exe)],stdout=subprocess.PIPE,stderr=log)
        try:
            for i in range(count):
                buf=p.stdout.read(44100*12*8)
                assert len(buf)==44100*12*8,(tag,i,len(buf))
                result.append(hashlib.sha256(buf).hexdigest())
                if i>=213:
                    x=np.frombuffer(buf,dtype='<f4');assert np.isfinite(x).all()
                    rms=float(np.sqrt(np.mean(x*x)));peak=float(np.max(abs(x)))
                    assert .0003<rms and peak<.75,(i,rms,peak)
                    measure.append(dict(id=i,rms=rms,peak=peak,sha256=result[-1]))
            assert not p.stdout.read(1) and p.wait()==0
        finally:
            if p.poll() is None:p.terminate();p.wait()
    return result,measure

with ThreadPoolExecutor(2) as pool:
    a=pool.submit(read_full,O/'Reference-Stereo.native',213,'Botanical-Atlas')
    b=pool.submit(read_full,R/'app/build/native/fullstereo-tests',233,'Warm-Collection')
    old,_=a.result();now,biome_metrics=b.result()
changed=[i for i in range(213) if old[i]!=now[i]]
assert not changed,changed
assert len(set(now[213:]))==20

# Same played notes and velocities within each model. Compare onset/decay
# contour, spectrum and waveform, rather than counting changed knob values.
metrics={};waves={}
for t in added:
    x=np.fromfile(O/f"Offline-Tones/tone-{t['id']}.f32",dtype='<f4').reshape(-1,2).mean(axis=1)
    assert len(x)==44100*6 and np.isfinite(x).all()
    peak=float(np.max(abs(x)));rms=float(np.sqrt(np.mean(x*x)))
    assert .0001<rms and peak<.75,(t['id'],t['name'],rms,peak)
    a=x[:66150].astype('float64');spec=abs(np.fft.rfft(a*np.hanning(len(a))))**2
    hz=np.fft.rfftfreq(len(a),1/44100);centroid=float((hz*spec).sum()/spec.sum())
    env=np.sqrt(np.mean(a.reshape(15,-1)**2,axis=1));cross=np.flatnonzero(env>.5*env.max())
    metrics[t['id']]=dict(id=t['id'],name=t['name'],model=t['model'],rms=rms,peak=peak,
        centroid_hz=round(centroid,1),half_level_first_100ms_bin=int(cross[0]),envelope_rms=env.tolist())
    waves[t['id']]=x/np.linalg.norm(x)
pairs=[]
for i,t in enumerate(added):
    for u in added[i+1:]:
        if u['model']!=t['model']:continue
        a,b=t['id'],u['id'];difference=float(np.linalg.norm(waves[a]-waves[b]))
        assert difference>.03,(t['name'],u['name'],difference)
        pairs.append(dict(a=t['name'],b=u['name'],normalized_waveform_distance=round(difference,5)))
pairs.sort(key=lambda x:x['normalized_waveform_distance'])
report=dict(legacy_bit_identical=True,legacy_biomes=213,legacy_presets=452,legacy_browse_order_retained=True,
  stereo_frames_compared=44100*12*213,seconds_per_biome=12,new_presets=64,new_biomes=20,
  models=dict(Counter(t['model'] for t in added)),tone_metrics=list(metrics.values()),
  closest_same_model_pairs=pairs[:12],biome_metrics=biome_metrics,sha256_per_biome=now,
  note='Audio metrics measure differences and headroom; they are not a subjective listening verdict.')
(O/'Catalog-Audio-Verification.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS 213 old Biomes bit-identical at 44.1 kHz stereo; all 64 new tones and 20 new Biomes distinct and audible.')
print('Closest isolated same-engine comparisons:',pairs[:5])
