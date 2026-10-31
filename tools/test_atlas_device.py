#!/usr/bin/env python3
"""Private 1.4 FM-1 atlas qualification; exact MIDI/audio name and live restore.

The caller must make a complete .mddb backup and live JSON export first.
No saved bank is written. The original live patch is restored after the run.
"""
import argparse,hashlib,json,struct,time
from pathlib import Path
from hardware_bench import Device,Meter
from device_editor import Editor

p=argparse.ArgumentParser()
p.add_argument('--port',default='Mutable Dreams FM-1')
p.add_argument('--original',required=True)
p.add_argument('--output',required=True)
p.add_argument('--seconds',type=float,default=1.8)
p.add_argument('--start',type=int,default=109)
p.add_argument('--end',type=int,default=157)
p.add_argument('--ids',help='Comma-separated Biome IDs instead of a contiguous range')
a=p.parse_args()
assert a.port=='Mutable Dreams FM-1' and 0.8<=a.seconds<=20 and 109<=a.start<a.end<=157
ids=[int(x) for x in a.ids.split(',')] if a.ids else list(range(a.start,a.end))
assert ids and len(ids)==len(set(ids)) and all(109<=i<157 for i in ids)
catalog=json.loads((Path(__file__).resolve().parents[1]/'presets/catalog.json').read_text())
original=json.loads(Path(a.original).read_text())
assert original['format']=='mutable-dreams-patch' and len(original['patch'])==2288
original_bytes=bytes(original['patch'])
out=Path(a.output)
report={'package':'1.4.0-dev atlas','original_sha256':hashlib.sha256(original_bytes).hexdigest(),
        'seconds_each':a.seconds,'rows':[],'restore_verified':False,'errors':[]}
d=None;meter=None
try:
 d=Device(a.port);meter=Meter();meter.stream.start()
 # A physical MASTER parked near zero makes USB capture silent. Use a modest
 # temporary remote level and release it unconditionally in finally.
 d.remote([4,0,8]);time.sleep(.08)
 assert d.status()['master']==1024,'temporary test master was not applied'
 base=d.status();assert base['patch_version']==9 and not base['playing']
 for id in ids:
  b=catalog['biomes'][id];d.command(27,0,0);d.command(20,0,id)
  raw=d.patch()
  if struct.unpack_from('<i',raw,155*4)[0]!=id:
   d.command(78,0,1);raw=d.patch()
  got=struct.unpack_from('<i',raw,155*4)[0]
  models=[struct.unpack_from('<i',raw,(14+i*22)*4)[0] for i in range(6)]
  expected=[t['model'] for t in b['tracks']]
  kit=raw[2100]
  want_kit=int((id-109)%8==4)
  assert got==id and models==expected and kit==want_kit,(id,got,models,expected,kit,want_kit)
  before=d.status();audio_before=meter.snapshot();d.call(3)
  d.command(16,0,1);time.sleep(a.seconds)
  after=d.status();audio_after=meter.snapshot();d.command(27,0,0)
  frames=audio_after['frames']-audio_before['frames']
  energy=audio_after['energy']-audio_before['energy']
  rms=(energy/(frames*2))**.5 if frames else 0
  issues=[k for k in ['xruns','overloads','nonfinite','dropped'] if after[k]!=before[k]]
  if frames<44100*a.seconds*.75:issues.append('USB capture incomplete')
  if audio_after['host_input_overflows']!=audio_before['host_input_overflows']:issues.append('host USB overflow')
  if audio_after['nonfinite']!=audio_before['nonfinite']:issues.append('USB nonfinite')
  if rms<=0.000001:issues.append('inaudible on USB capture')
  if after['callback_peak_us']>after['callback_budget_us']*.88:issues.append('audio callback over 88% budget')
  row={'id':id,'name':b['name'],'models':models,'kit':kit,'rms':round(rms,7),
       'peak_callback_us':after['callback_peak_us'],'budget_us':after['callback_budget_us'],
       'quality':after['quality'],'issues':issues}
  report['rows'].append(row);out.write_text(json.dumps(report,indent=2)+'\n')
  print('BIOME',id,b['name'],'RMS',row['rms'],'callback',row['peak_callback_us'],'/',row['budget_us'],issues,flush=True)
 report['final_status']={k:after[k] for k in ['xruns','overloads','nonfinite','dropped','memory','patch_version']}
except BaseException as e:
 report['errors'].append(repr(e))
 raise
finally:
 if d:
  try:d.command(27,0,0)
  except BaseException as e:report['errors'].append('stop: '+repr(e))
  try:d.remote([4,1,32])
  except BaseException as e:report['errors'].append('master release: '+repr(e))
  d.close()
 if meter:
  try:meter.stream.stop();meter.stream.close()
  except BaseException as e:report['errors'].append('audio close: '+repr(e))
 try:
  editor=Editor(a.port)
  try:
   editor.upload(original_bytes)
   restored=editor.snapshot()
   report['restore_verified']=restored==original_bytes
   if not report['restore_verified']:report['errors'].append('live patch restore differs')
  finally:editor.close()
 except BaseException as e:report['errors'].append('live restore: '+repr(e))
 out.write_text(json.dumps(report,indent=2)+'\n')
