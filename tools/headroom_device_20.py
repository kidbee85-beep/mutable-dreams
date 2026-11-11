"""Private exact-target Botanical Headroom qualification. No saved object is changed."""
from pathlib import Path
import json,struct,time,re,threading,wave
import mido,numpy as np,sounddevice as sd
from creative_bench import Bench
from mirror_watch_20 import MirrorWatch
from dreams_backup import B,KIND
R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Headroom-Lab').resolve();back=R/'backups/Before-Headroom-2026-11-10'
H=(back/'live-original.raw').read_bytes();saved=dict(B.parse((back/'Saved-Before-Headroom.mddb').read_bytes(),KIND)[1]);prefs=saved[2];old_motion=struct.unpack_from('<i',prefs,4)[0];old_glow=struct.unpack_from('<i',prefs,12)[0]
cat=json.loads((R/'presets/catalog.json').read_text());body=re.search(r'enum \{([^}]*MD_COMMAND_COUNT[^}]*)\}',(R/'app/dsp/dreams.h').read_text(),re.S).group(1);cmd={};n=-1
for v in body.split(','):
 v=v.strip()
 if v:a,eq,b=v.partition('=');n=int(b,0) if eq else n+1;cmd[a.strip()]=n
report={'private':True,'checks':[],'errors':[],'cases':[]};d=w=stream=ct=None;run=threading.Event();meter={'frames':0,'energy':0.,'peak':0.,'overflows':0,'nonfinite':0};clips=[];recording=False

def flush():(O/'Device-Qualification.json').write_text(json.dumps(report,indent=2)+'\n')
def check(name,ok):
 assert ok,name
 report['checks'].append(name);flush();print('PASS',name,flush=True)
def setc(name,t,v):d.command(cmd[name],t,v)
def state():
 x=struct.unpack('<49I',d.call(1));return dict(ms=x[1],xruns=x[4],overloads=x[5],nonfinite=x[7],drops=x[8],playing=x[10],peak_us=x[37],budget_us=x[38],quality=x[9],quality_reductions=x[40],ui_frames=x[47],ui_peak_ms=x[48])
def panel(buttons):
 end=time.monotonic()+3
 while time.monotonic()<end:
  if w.panel and w.panel['buttons']==buttons:return
  time.sleep(.025)
 raise AssertionError(('Panel mismatch',buttons,w.panel))
def capture(x,n,t,s):
 meter['frames']+=n;meter['energy']+=float(np.sum(x*x));meter['peak']=max(meter['peak'],float(np.max(np.abs(x))));meter['overflows']+=int(bool(s.input_overflow));meter['nonfinite']+=int(not np.isfinite(x).all())
 if recording:clips.append(x.copy())
def clock():
 t=time.monotonic()
 while run.is_set():
  d.o.send(mido.Message('clock'));t+=60/(108*24);time.sleep(max(0,t-time.monotonic()))
def factory(id):
 d.command(16,0,0);d.command(61,0,0);d.command(20,0,id);d.command(cmd['MD_SCENE_CONFIRM'],0,1)
 p=d.snapshot();assert struct.unpack_from('<i',p,620)[0]==id
 (O/'Examples').mkdir(exist_ok=True);(O/f'Examples/Biome-{id}.json').write_text(json.dumps(dict(format='mutable-dreams-patch',version=1,firmware='2.0-dev',name=cat['biomes'][id]['name'],patch=list(p)),indent=2)+'\n')
 return p
def exercise(name,p,seconds,external=False,record=False,expected_quality=None):
 global recording,ct
 d.command(16,0,0);d.command(61,0,0);d.upload(p)
 if external:d.command(61,0,1);run.set();ct=threading.Thread(target=clock,daemon=True);ct.start()
 d.call(3);before=state();assert expected_quality is None or before['quality']==expected_quality,(name,before);mb=dict(meter);start=time.monotonic();seen=[];clips.clear();recording=record;d.command(16,0,1);j=0
 try:
  while time.monotonic()-start<seconds:
   time.sleep(.3);s=state();seen.append(s)
   assert expected_quality is None or s['quality']==expected_quality,(name,s)
   for k in ('xruns','overloads','nonfinite','drops'):assert s[k]==before[k],(name,k,s,before)
   if external:
    for k in range(4):
     value=(j*877+k*173)%16384;d.o.send(mido.Message('control_change',channel=15,control=16+k,value=value>>7));d.o.send(mido.Message('control_change',channel=15,control=48+k,value=value&127))
   d.ui(100,0,[0,1,3,11,12,19,31,38][int((time.monotonic()-start)//3)%8]);j+=1
 finally:
  recording=False;d.command(16,0,0)
  if external:run.clear();ct.join(timeout=3);d.command(61,0,0)
 peak=max(s['peak_us']/s['budget_us']*100 for s in seen);frames=meter['frames']-mb['frames'];energy=meter['energy']-mb['energy'];assert peak<85 and frames>44100*seconds*.8 and energy>1e-7
 assert meter['overflows']==mb['overflows'] and meter['nonfinite']==mb['nonfinite']
 after=state();elapsed=time.monotonic()-start;ui_fps=(after['ui_frames']-before['ui_frames'])/elapsed;ui_peak=max(v['ui_peak_ms'] for v in seen)
 assert ui_fps>=6 and ui_peak<=180,(name,ui_fps,ui_peak)
 row=dict(name=name,seconds=round(elapsed,2),before=before,after=after,callback_peak_percent=round(peak,2),rms=(energy/(frames*2))**.5,frames=frames,ui_fps=round(ui_fps,2),ui_peak_ms=ui_peak)
 if record:
  a=np.concatenate(clips);dest=O/'Device-Audio';dest.mkdir(exist_ok=True);fn=name+'.wav'
  with wave.open(str(dest/fn),'wb') as f:f.setparams((2,2,44100,0,'NONE','not compressed'));f.writeframes((np.clip(a,-1,1)*32767).astype('<i2').tobytes())
  row['usb_capture']='Device-Audio/'+fn
 report['cases'].append(row);flush();print('PASS',name,round(peak,2),'% deadline',flush=True)
try:
 d=Bench('Mutable Dreams FM-1');check('updated protocol and stable v11 project schema',struct.unpack('<8I',d.call(7))==(1,11,2604,40,16,64,452,213))
 d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.command(16,0,0);d.command(61,0,0);d.ui(103,0,1);d.ui(108,0,2)
 (O/'Screens').mkdir(exist_ok=True);w=MirrorWatch('Mutable Dreams FM-1',O);check('complete USB mirror on connection',w.full())
 p=factory(197);d.ui(101,0,0);d.ui(100,0,1)
 # Actual firmware encoder input, not the model-lookup helper.
 for model in range(20):
  setc('MD_MODEL',0,model);d.remote([3,2,65]);time.sleep(.05);q=d.snapshot();first=next(t['id'] for t in cat['tones'] if t['model']==model)
  assert struct.unpack_from('<i',q,56)[0]==model and struct.unpack_from('<i',q,624)[0]==first,(model,first)
 check('all twenty engine-to-first-preset paths, including Skin, Bow and Aero',True)
 # A literal upload/export of both new voice types keeps all 2604 bytes.
 p=factory(197);d.upload(p);check('Bow and Aero live editor roundtrip exact',d.snapshot()==p)
 # Long-hold help, release cannot invoke the short action.
 for model in (18,19):
  setc('MD_MODEL',0,model);d.ui(100,0,1);d.remote([1,16,0]);time.sleep(.9);s=struct.unpack('<49I',d.call(1));assert s[44]==4
  w.screen.png(O/f'Screens/Device-Model-{model}-Help.png');d.remote([1,0,0]);s=struct.unpack('<49I',d.call(1));assert s[43]==1;d.ui(100,0,1)
 check('new engine context-help and no short action after release',True)
 # All existing navigation light ownership remains intact.
 p=factory(197);d.upload(p);setc('MD_LOCK',0,0);setc('MD_FREEZE',0,0)
 for button,pages in {6:[0],4:[1,4,6,10,12,24,25],9:[2,7,13,16,19,20,26,27,31,32,33],5:[5,9,11,17,18,21,23,30,34,35],0:[3,8,14,15,22,28,29,36,37,38,39]}.items():
  for page in pages:d.ui(100,0,page);panel(1<<button)
 check('forty real navigation lights retained',True)
 d.ui(100,0,0);d.ui(101,0,0);setc('MD_MUTE',0,1);panel(66);time.sleep(.7);w.screen.png(O/'Screens/Device-Muted.png');setc('MD_MUTE',0,0);panel(64)
 check('real mute light follows selection; captured new OFF row and botanical view',True)
 matches=[i for i,v in enumerate(sd.query_devices()) if v['name']=='Mutable Dreams FM-1' and v['max_input_channels']==2];assert len(matches)==1
 stream=sd.InputStream(device=matches[0],samplerate=44100,channels=2,dtype='float32',blocksize=1024,callback=capture);stream.start();d.remote([4,96,18])
 for id in range(197,213):
  p=factory(id);d.ui(100,0,0);time.sleep(.6);w.screen.png(O/f'Screens/Device-World-{id}.png');exercise('Biome-'+str(id)+'-'+cat['biomes'][id]['name'].replace(' ','-'),p,8)
 base=factory(197)
 for model in (18,19):
  p=bytearray(base)
  for off,v in ((24,1000),(40,1000),(48,850),(52,1)):struct.pack_into('<i',p,off,v)
  for i in range(6):
   at=56+i*88
   for off,v in ((0,model),(16,600),(20,700),(24,16),(28,16),(32,0),(36,1000),(40,36+i*7),(48,0),(52,1),(56,80),(60,4),(76,1000),(80,2)):struct.pack_into('<i',p,at+off,v)
  exercise('six-'+str(model)+'-FULL-LP24-FLOW-clock-FX-CC-mirror',p,60,True,expected_quality=1)
 mixed=bytearray(base)
 for off,v in ((24,1000),(40,1000),(48,850),(52,1)):struct.pack_into('<i',mixed,off,v)
 for i,model in enumerate((18,19,15,16,9,17)):
  at=56+i*88
  for off,v in ((0,model),(16,520),(20,700),(24,16),(28,16),(32,0),(36,1000),(40,36+i*7),(48,0),(52,1),(56,80),(60,4),(76,1000),(80,2)):struct.pack_into('<i',mixed,at+off,v)
 d.command(126,0,1);exercise('six-mixed-voices-FLOW-clock-FX-CC-mirror',mixed,180,True,expected_quality=0)
 for quality in (0,1):
  sample=bytearray(base);struct.pack_into('<i',sample,56+5*88+48,1)
  for off,v in ((24,740),(40,850),(48,350),(52,quality)):struct.pack_into('<i',sample,off,v)
  exercise('Ember-Room-'+('FULL' if quality else 'ECO'),sample,20,record=True,expected_quality=quality)
 report['mirror']=w.summary();report['audio']=dict(meter);check('finite real USB stereo and no input overruns, mirror corruption or sequence loss',meter['peak']>1e-6 and meter['overflows']==meter['nonfinite']==0 and w.gaps==w.screen.bad==0)
 report['pass']=True
except BaseException as e:report['errors'].append(repr(e));raise
finally:
 run.clear()
 if ct:ct.join(timeout=3)
 if stream:stream.stop();stream.close()
 if w:w.close()
 if d:
  try:
   d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.command(16,0,0);d.command(61,0,0);d.command(126,0,prefs[26]);d.ui(103,0,old_motion);d.ui(108,0,old_glow);d.upload(H);d.ui(100,0,0);d.remote([4,1,32]);report['owner_live_exact']=d.snapshot()==H;report['final']=state()
  except Exception as e:report['cleanup_error']=repr(e)
  d.close()
 flush();print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2),flush=True)
