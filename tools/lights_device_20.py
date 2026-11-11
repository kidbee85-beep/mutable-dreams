"""Exact-target private LED qualification. Volatile edits only; owner data restored."""
from pathlib import Path
import json,struct,time,re,threading
import mido,numpy as np,sounddevice as sd
from creative_bench import Bench
from mirror_watch_20 import MirrorWatch
from dreams_backup import B,KIND
R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Living-Lights').resolve();A=(R/'../../outputs/Mutable-Dreams-2.0-Stability-Lab').resolve();back=R/'backups/Before-Lights-2026-11-10'
H=(back/'live-original.raw').read_bytes();saved=dict(B.parse((back/'Saved-Before-Lights.mddb').read_bytes(),KIND)[1]);prefs=saved[2];old_motion=struct.unpack_from('<i',prefs,4)[0];old_glow=struct.unpack_from('<i',prefs,12)[0]
body=re.search(r'enum \{([^}]*MD_COMMAND_COUNT[^}]*)\}',(R/'app/dsp/dreams.h').read_text(),re.S).group(1);cmd={};n=-1
for v in body.split(','):
 v=v.strip()
 if v:a,eq,b=v.partition('=');n=int(b,0) if eq else n+1;cmd[a.strip()]=n
report={'private':True,'checks':[],'errors':[],'cases':[]};d=w=stream=ct=None;run=threading.Event();meter={'frames':0,'energy':0.,'peak':0.,'overflows':0,'nonfinite':0}
def flush():(O/'Device-Qualification.json').write_text(json.dumps(report,indent=2)+'\n')
def check(name,ok):
 assert ok,name
 report['checks'].append(name);flush();print('PASS',name,flush=True)
def setc(name,t,v):d.command(cmd[name],t,v)
def state():
 x=struct.unpack('<49I',d.call(1));return dict(ms=x[1],xruns=x[4],overloads=x[5],nonfinite=x[7],drops=x[8],playing=x[10],peak_us=x[37],budget_us=x[38],ui_peak_ms=x[48])
def panel(buttons,keys=None,dimkeys=None,glow=None):
 end=time.monotonic()+3
 while time.monotonic()<end:
  p=w.panel
  if p and p['buttons']==buttons and (keys is None or p['keys']==keys) and (dimkeys is None or p['dim_keys']==dimkeys) and (glow is None or bool(p['dim_buttons'])==glow):return True
  time.sleep(.03)
 raise AssertionError(('Panel mismatch',buttons,keys,dimkeys,w.panel))
def capture(x,n,t,s):
 meter['frames']+=n;meter['energy']+=float(np.sum(x*x));meter['peak']=max(meter['peak'],float(np.max(np.abs(x))));meter['overflows']+=int(bool(s.input_overflow));meter['nonfinite']+=int(not np.isfinite(x).all())
def clock():
 t=time.monotonic()
 while run.is_set():
  d.o.send(mido.Message('clock'));t+=60/(108*24);time.sleep(max(0,t-time.monotonic()))
def exercise(name,patch,seconds,external=False):
 d.command(16,0,0);d.command(61,0,0);d.upload(patch)
 if external:
  d.command(61,0,1);run.set()
  global ct;ct=threading.Thread(target=clock,daemon=True);ct.start()
 d.call(3);before=state();start=time.monotonic();seen=[];d.command(16,0,1);j=0
 try:
  while time.monotonic()-start<seconds:
   time.sleep(.3);s=state();seen.append(s)
   for k in ('xruns','overloads','nonfinite','drops'):assert s[k]==before[k],(name,k,s)
   if external:
    for k in range(4):
     value=(j*877+k*173)%16384
     d.o.send(mido.Message('control_change',channel=15,control=16+k,value=value>>7));d.o.send(mido.Message('control_change',channel=15,control=48+k,value=value&127))
   d.ui(100,0,[0,1,3,11,12,19,31,38][int((time.monotonic()-start)//3)%8]);j+=1
 finally:
  d.command(16,0,0)
  if external:run.clear();ct.join(timeout=3);d.command(61,0,0)
 peak=max(s['peak_us']/s['budget_us']*100 for s in seen);assert peak<85
 report['cases'].append(dict(name=name,seconds=round(time.monotonic()-start,2),before=before,after=state(),callback_peak_percent=round(peak,2)));flush()
 print('PASS',name,round(peak,2),'% deadline',flush=True)
try:
 d=Bench('Mutable Dreams FM-1');assert struct.unpack('<8I',d.call(7))==(1,11,2604,40,16,64,420,197)
 d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.command(16,0,0);d.command(61,0,0)
 seed=bytes(json.loads((A/'Examples/Biome-189.json').read_text())['patch']);d.upload(seed)
 for c in ('MD_LOCK','MD_FREEZE','MD_RECORD'):setc(c,0,0)
 for i in range(6):setc('MD_MUTE',i,0)
 d.ui(101,0,0);d.ui(103,0,1);d.ui(108,0,2)
 w=MirrorWatch('Mutable Dreams FM-1',O);check('complete USB mirror seed',w.full())
 groups={6:[0],4:[1,4,6,10,12,24,25],9:[2,7,13,16,19,20,26,27,31,32,33],5:[5,9,11,17,18,21,23,30,34,35],0:[3,8,14,15,22,28,29,36,37,38,39]}
 for button,pages in groups.items():
  for page in pages:d.ui(100,0,page);panel(1<<button)
 check('all 40 pages have the correct actual panel LED; HOME is exclusive to Live',True)
 d.ui(100,0,0);setc('MD_MUTE',0,1);panel(66);d.ui(101,0,1);panel(64);d.ui(101,0,0);panel(66);setc('MD_MUTE',0,0);panel(64)
 check('SEL follows the selected muted track, not an unrelated voice',True)
 setc('MD_LOCK',0,1);setc('MD_FREEZE',0,1);panel(328);setc('MD_LOCK',0,0);setc('MD_FREEZE',0,0)
 check('seed lock and granular freeze have persistent status LEDs',True)
 d.command(16,0,1);panel(1088);d.ui(100,0,19);setc('MD_RECORD',0,2);panel(512|1024|2048|32768);setc('MD_RECORD',0,0);panel(512|1024);d.command(16,0,0)
 check('green playback and red recording/REC states follow the actual transport',True)
 d.ui(100,0,0);d.remote([1,16,0]);time.sleep(.9);panel(80);d.remote([1,0,0]);time.sleep(.1);panel(80);d.ui(100,0,0);panel(64)
 check('held-help light persists without performing the short EDIT action',True)
 setc('MD_TRIGGER',0,60);panel(64,0,128);setc('MD_MUTE',0,1);panel(66,0,0);setc('MD_MUTE',0,0);time.sleep(1.9)
 check('musical key tail obeys note range, selected track, mute and release',True)
 d.ui(108,0,0);panel(64,0,0,False);check('OFF removes the decorative dim layer',w.panel['dim_buttons']==0)
 d.ui(108,0,1);panel(64,glow=True);check('DIM retains readable inactive labels',w.panel['dim_buttons']!=0)
 d.ui(108,0,2)
 matches=[i for i,v in enumerate(sd.query_devices()) if v['name']=='Mutable Dreams FM-1' and v['max_input_channels']==2];assert len(matches)==1
 stream=sd.InputStream(device=matches[0],samplerate=44100,channels=2,dtype='float32',blocksize=1024,callback=capture);stream.start();d.remote([4,96,18])
 for biome in (189,190,195):exercise('warm-Biome-'+str(biome),bytes(json.loads((A/f'Examples/Biome-{biome}.json').read_text())['patch']),8)
 mixed=bytearray(json.loads((A/'Examples/Biome-196.json').read_text())['patch'])
 for off,v in ((40,1000),(48,850)):struct.pack_into('<i',mixed,off,v)
 for i,model in enumerate((17,15,16,9,8,14)):
  at=56+i*88;struct.pack_into('<i',mixed,at,model)
  for off,v in ((24,16),(28,16),(36,1000),(52,1),(60,4),(76,1000)):struct.pack_into('<i',mixed,at+off,v)
 d.command(126,0,1);exercise('six-voices-FLOW-clock-FX-CC-mirror',mixed,120,True)
 report['mirror']=w.summary();report['audio']=dict(meter);check('finite real USB stereo, no input overflows or mirror sequence loss',meter['peak']>1e-6 and meter['overflows']==meter['nonfinite']==0 and w.gaps==w.screen.bad==0)
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
