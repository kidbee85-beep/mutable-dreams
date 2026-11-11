"""Private Living Atlas hardware qualification. Exact target; owner state restored.
No occupied User Biome is written, and the original saved project is restored
after the temporary save/read test. USB audio input only, never host playback.
"""
from pathlib import Path
import argparse,json,struct,time,wave
import numpy as np
import sounddevice as sd
from creative_bench import Bench

p=argparse.ArgumentParser();p.add_argument('--short',action='store_true');p.add_argument('--tag',default='14551448');a=p.parse_args()
R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Living-Atlas').resolve()
H=(R/'backups/Before-Living-Atlas-2026-11-10/live-original.raw').read_bytes()
factory={i:bytes(json.loads((O/f'Examples/Biome-{i}.json').read_text())['patch']) for i in range(177,189)}
report={'candidate':a.tag,'cases':[],'errors':[],'usb_input_only':True,'cold_start_tested':False}
out=O/f'Device-Qualification-{a.tag}.json';chunks=[];overflows=[0];d=None;stream=None;stored=None;saved=False
def flush():out.write_text(json.dumps(report,indent=2)+'\n')
def stats():
 w=struct.unpack('<49I',d.call(1))
 return dict(ms=w[1],cpu=w[2],xruns=w[4],overloads=w[5],nonfinite=w[7],drops=w[8],quality=w[9],playing=w[10],events=list(w[11:17]),models=list(w[17:23]),callback_peak_us=w[37],callback_budget_us=w[38],ui_peak_ms=w[48],ui_frames=w[47])
def capture(x,n,t,status):
 if status.input_overflow:overflows[0]+=1
 chunks.append(x.copy())
def exercise(name,patch,seconds,record=False):
 d.command(16,0,0);d.upload(patch);d.ui(100,0,0);d.call(3)
 before=stats();start=time.monotonic();begin=len(chunks);ov=overflows[0];seen=[];d.command(16,0,1)
 try:
  while time.monotonic()-start<seconds:
   time.sleep(.35);s=stats();seen.append(s)
   for k in ('xruns','overloads','nonfinite','drops'):assert s[k]==before[k],(name,k,before,s)
   assert s['playing'],name
   d.ui(100,0,[0,8,12,38,39,19][int((time.monotonic()-start)/2)%6])
 finally:d.command(16,0,0)
 after=stats();audio=np.concatenate(chunks[begin:]);peak=max(x['callback_peak_us']/x['callback_budget_us']*100 for x in seen)
 assert np.isfinite(audio).all() and np.max(np.abs(audio))>1e-6,(name,'silent/nonfinite USB')
 assert overflows[0]==ov,(name,'host USB overflow')
 assert len(audio)>44100*seconds*.8,(name,'incomplete USB')
 assert peak<85,(name,peak)
 row=dict(name=name,seconds=round(time.monotonic()-start,2),before=before,after=after,callback_peak_percent=round(peak,2),usb_rms=float(np.sqrt(np.mean(audio*audio))),usb_peak=float(np.max(np.abs(audio))),host_overflows=overflows[0]-ov,readings=seen)
 report['cases'].append(row);flush();print('PASS',name,round(peak,2),'% deadline',flush=True)
 if record:
  # The same master setting is used for all clips. No individual normalization.
  with wave.open(str(O/'Audio'/f'FM1-USB-{name}.wav'),'wb') as w:
   w.setnchannels(2);w.setsampwidth(2);w.setframerate(44100);w.writeframes((np.clip(audio,-1,1)*32767).astype('<i2').tobytes())
def dense(model):
 b=bytearray(factory[177]);struct.pack_into('<i',b,28,180)
 for t in range(6):
  at=56+t*88
  for off,v in [(0,model),(12,800),(16,380),(20,600),(24,8),(28,16),(32,0),(36,1000),(40,36+t*7),(48,0),(52,1)]:struct.pack_into('<i',b,at+off,v)
  b[1292+t*132]=0
 struct.pack_into('<i',b,40,1000);struct.pack_into('<i',b,48,650);struct.pack_into('<i',b,52,0)
 for j in range(4):
  struct.pack_into('<BBBBhH',b,2356+j*8,j,2,j+1,1,1000 if j%2==0 else -1000,180)
 return b
try:
 d=Bench('Mutable Dreams FM-1');report['caps']=list(struct.unpack('<8I',d.call(7)));assert report['caps']==[1,11,2604,40,16,64,404,189]
 d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.command(16,0,0);d.command(61,0,0);d.upload(H);stored=d.stored(0)
 devices=sd.query_devices();matches=[i for i,v in enumerate(devices) if v['name']=='Mutable Dreams FM-1' and v['max_input_channels']==2];assert len(matches)==1
 stream=sd.InputStream(device=matches[0],samplerate=44100,channels=2,dtype='float32',blocksize=1024,callback=capture);stream.start();d.remote([4,96,18])
 ids=[177,178,182,185] if a.short else range(177,189)
 for i in ids:exercise(f'Biome-{i}',factory[i],10 if a.short else (20 if i in (177,178,182) else 9),record=i in (177,178,182))
 for model in (15,16,9):exercise(f'six-model-{model}-long-grain-four-links',dense(model),20 if a.short else 60)
 d.command(16,0,0);d.upload(factory[177]);d.save();saved=True
 report['cedar_project_saved_read_exact']=d.stored(0)==factory[177];assert report['cedar_project_saved_read_exact']
 d.upload(factory[178]);d.save();report['reed_project_saved_read_exact']=d.stored(0)==factory[178];assert report['reed_project_saved_read_exact']
 report['after_stopped_saves']=stats();report['pass']=True
except BaseException as e:report['errors'].append(repr(e));raise
finally:
 if stream is not None:stream.stop();stream.close()
 if d:
  try:
   d.command(16,0,0);d.command(61,0,0);d.remote([1,0,0]);d.remote([2,0,0,0,0])
   if saved and d.stored(0)!=stored:d.upload(stored);d.save()
   d.upload(H);d.ui(100,0,0)
   report['owner_live_restored']=d.snapshot()==H;report['original_saved_project_restored']=d.stored(0)==stored
   assert report['owner_live_restored'] and report['original_saved_project_restored']
   d.remote([4,1,32])
  except Exception as e:report['cleanup_error']=repr(e)
  d.close()
 report['host_input_overflows']=overflows[0];flush();print(json.dumps({k:v for k,v in report.items() if k!='cases'}),flush=True)
