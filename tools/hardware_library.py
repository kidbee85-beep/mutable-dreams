#!/usr/bin/env python3
"""Real FM-1 factory bank audit, panel hold help, preferences and measured USB audio.
Writes the final stopped factory Biome and appearance preferences via physical SAVE.
Run only on the user's authorised backed-up device.
"""
import json,time,struct,argparse
from pathlib import Path
from hardware_bench import Device,Meter
p=argparse.ArgumentParser();p.add_argument('--port',required=True);p.add_argument('--output',required=True);p.add_argument('--seconds',type=float,default=6);a=p.parse_args()
catalog=json.loads((Path(__file__).resolve().parents[1]/'presets/catalog.json').read_text());d=Device(a.port);meter=Meter();target=Path(a.output);result={'biomes':[],'tones':[],'checks':[]}
def check(name,ok):
 if not ok:raise AssertionError(name)
 result['checks'].append(name);print('PASS',name,flush=True)
def patch():
 data=d.patch();return struct.unpack('<%di'%(len(data)//4),data)
def expected_biome(b):
 fields='model tone harm decay level send hits length rotate prob note pan mute speed attack ratchet accent shape cutoff resonance filter breathe'.split()
 vals=[0x4D445231,5,b['seed'],*b['macro']]+[b[k] for k in ['tempo','locked','freeze','grain_size','grain_pos','feedback','quality']]
 vals += [t[k] for t in b['tracks'] for k in fields]
 vals += [b[k] for k in ['root','scale','transpose','swing','grain_pitch','reverse','duck_source','duck_amount','duck_release']]+[b['id'],*b['tones'],0]
 vals += ([0,0,0,0,0,0,0,250,0,0]+[0,-1,7,-1]*4)*6+[200,300,450,0]
 return tuple(vals)
def turn(role,n):
 while n:
  z=max(-63,min(63,n));d.remote([3,role,z+64]);time.sleep(.12);n-=z
try:
 meter.stream.start();d.command(27,0,0);d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.remote([4,0,16]);d.tap(6);base=d.status();check('patch version 5',base['patch_version']==5)
 for b in range(14):
  before=d.patch();d.remote([1,(1<<b)&127,((1<<b)>>7)&127]);time.sleep(.95);s=d.status();d.remote([1,0,0]);time.sleep(.07)
  check(f'held button {b}: help only',s['help']==b and d.patch()==before and not s['playing']);d.tap(6);check(f'help {b} closes without action',d.status()['help']==-1)
 for b in catalog['biomes']:
  d.command(27,0,0);d.command(20,0,b['id']);w=patch();check('Biome complete factory patch '+b['name'],w==expected_biome(b))
  d.call(3);before=d.status();audio_before=meter.snapshot();d.command(16,0,1);time.sleep(a.seconds);after=d.status();audio_after=meter.snapshot();d.command(27,0,0)
  energy=audio_after['energy']-audio_before['energy'];frames=audio_after['frames']-audio_before['frames'];issues=[]
  for key in ['xruns','overloads','nonfinite','dropped']:
   if after[key]!=before[key]:issues.append(key)
  if frames<44100*a.seconds*.8:issues.append('USB capture incomplete')
  if audio_after['nonfinite']!=audio_before['nonfinite']:issues.append('USB nonfinite')
  if after['frames']<=before['frames'] or not after['playing'] or after['quality']!=0:issues.append('unexpected transport or quality state')
  if b['id']!=64 and energy<=1e-7:issues.append('silent')
  if b['id']==64 and energy>1e-9:issues.append('blank not silent')
  if audio_after['host_input_overflows']!=audio_before['host_input_overflows']:issues.append('host USB overflow')
  cpu=after['callback_peak_us']/after['callback_budget_us']*100
  if cpu>=85:issues.append('less than 15% margin')
  row={'id':b['id'],'name':b['name'],'seconds':a.seconds,'cpu_peak_pct':round(cpu,2),'audio_rms':(energy/(frames*2))**.5 if frames else 0,'issues':issues,'pass_':not issues,'before':before,'after':after};result['biomes'].append(row);target.write_text(json.dumps(result,indent=2));print('BIOME',b['id'],b['name'],round(cpu,2),issues,flush=True)
  if issues:raise AssertionError(row)
 # All 160 recall operations must match the catalogue and leave the musical pattern intact.
 d.command(20,0,1);d.command(16,0,1)
 field_index={'model':0,'tone':1,'harm':2,'decay':3,'attack':14,'shape':17,'cutoff':18,'resonance':19,'filter':20,'breathe':21}
 preserved=[4,5,6,7,8,9,10,11,12,13,15,16]
 for t in catalog['tones']:
  track=t['id']%6;before=patch();d.command(45,track,t['id']);w=patch();off=14+track*22
  ok=all(w[off+j]==before[off+j] for j in preserved) and all(w[off+j]==t[k] for k,j in field_index.items()) and w[156+track]==t['id']
  check('tone '+str(t['id'])+' '+t['name'],ok);result['tones'].append({'id':t['id'],'track':track,'pass_':ok})
 s=d.status();check('all library operations without audio faults',all(s[k]==base[k] for k in ['xruns','overloads','nonfinite','dropped']));
 d.command(27,0,0);d.command(20,0,64);d.tap(6);turn(0,-63);d.tap(4);turn(2,1);check('EDIT PRESET selects tone',patch()[156]==112);d.tap(6);turn(2,-64);check('HOME PRESET selects Biome',patch()[155]==0)
 d.tap(5);d.tap(5);d.tap(5);check('GLO reaches Look',d.status()['page']==11);turn(3,-1);turn(4,-1);check('motion and boot sound disabled',d.status()['motion']==0 and d.status()['boot_sound']==0)
 d.tap(7);result['saved_preferences']={'motion':0,'boot_sound':0};saved_patch=d.patch();target.with_suffix('.raw').write_bytes(saved_patch);result['saved_sha256']=__import__('hashlib').sha256(saved_patch).hexdigest();result['final']=d.status();result['pass_']=all(x['pass_'] for x in result['biomes']+result['tones']);target.write_text(json.dumps(result,indent=2))
finally:
 d.command(27,0,0);d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.remote([4,1,32]);meter.stream.stop();meter.stream.close();d.close()
