#!/usr/bin/env python3
"""Record ONLY the Mutable Dreams USB input; never uses the default microphone."""
import argparse,json,time,wave
from pathlib import Path
import numpy as np
import sounddevice as sd
from hardware_bench import Device
p=argparse.ArgumentParser();p.add_argument('--port',required=True);p.add_argument('--directory',required=True);p.add_argument('--seconds',type=int,default=24);a=p.parse_args()
d=Device(a.port);out=Path(a.directory);out.mkdir(parents=True,exist_ok=True);rows=[]
try:
 devices=sd.query_devices();idx=next(i for i,x in enumerate(devices) if 'Mutable Dreams' in x['name'] and x['max_input_channels']==2)
 for scene,name in [(0,'Ambient'),(1,'Techno')]:
  d.command(27,0,0);d.command(20,0,scene);d.remote([4,0,16]);time.sleep(.1);d.call(3)
  take=sd.rec(a.seconds*44100,samplerate=44100,channels=2,dtype='float32',device=idx);d.command(16,0,1);sd.wait();s=d.status();d.command(27,0,0)
  assert np.isfinite(take).all() and np.max(abs(take))>1e-4
  path=out/f'Mutable-Dreams-FM1-{name}.wav'
  with wave.open(str(path),'wb') as f:f.setnchannels(2);f.setsampwidth(2);f.setframerate(44100);f.writeframes((np.clip(take,-1,.999969)*32768).astype('<i2').tobytes())
  rows.append({'file':path.name,'source':devices[idx]['name'],'seconds':a.seconds,'rms':float(np.sqrt(np.mean(take*take))),'peak':float(np.max(abs(take))),'device':s});print(name,rows[-1]['rms'],flush=True)
 (out/'Mutable-Dreams-FM1-Aufnahmen.json').write_text(json.dumps(rows,indent=2))
finally:
 d.command(27,0,0);d.command(20,0,0);d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.tap(6);d.remote([4,1,32]);d.close()
