"""Private 2.0 hardware check. One exact FM-1, USB input only, full live/prefs cleanup.
Requires fresh Before-Stability backups. No occupied User Biome is overwritten.
"""
from pathlib import Path
import json,struct,time,subprocess,select,wave,sys
import mido,numpy as np,sounddevice as sd
from mirror_watch_20 import MirrorWatch
import threading
from creative_bench import Bench
from dreams_backup import B,KIND

R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Stability-Lab').resolve();BACK=R/'backups/Before-Stability-2026-11-10'
HOME=(BACK/'live-before-flash.raw').read_bytes()
parts=dict(B.parse((BACK/'Saved-Before-Flash.mddb').read_bytes(),KIND)[1]);prefs=parts[2];assert len(prefs)==156
routes=list(prefs[20:26]);macro=prefs[26];oldmaps=[list(prefs[28+i*8:36+i*8]) for i in range(16)]
assert all(m[3]==0 or (1<=m[3]<120 and m[3] not in (6,64) and not 32<=m[3]<=63 and not 96<=m[3]<=101) for m in oldmaps)
factory={i:bytes(json.loads((O/f'Examples/Biome-{i}.json').read_text())['patch']) for i in range(189,197)}
finish_clock='--finish-clock' in sys.argv
report={'private':True,'cases':[],'checks':[],'errors':[],'cold_start_tested':False,'usb_audio_input_only':True};out=O/('Device-Clock-Finish.json' if finish_clock else 'Device-Qualification.json')
d=None;stream=None;stored=None;changed=False;saved=False;radio=None;probe=None;watch=None;clock_run=None;chunks=[]
meter={'frames':0,'energy':0.,'overflows':0,'nonfinite':0};
def flush():out.write_text(json.dumps(report,indent=2)+'\n')
def check(name,ok):assert ok,name;report['checks'].append(name);print('PASS',name,flush=True);flush()
def stats():
 w=struct.unpack('<49I',d.call(1));return dict(ms=w[1],xruns=w[4],overloads=w[5],nonfinite=w[7],drops=w[8],playing=w[10],events=list(w[11:17]),models=list(w[17:23]),tempo=w[36],quality=w[9],peak_us=w[37],budget_us=w[38],ui_peak_ms=w[48])
def perf(track):return list(struct.unpack('<7i',d.call(18,[track])))
def wait_perf(track,expected,timeout=3):
 end=time.monotonic()+timeout;actual=perf(track)
 while actual!=expected and time.monotonic()<end:time.sleep(.05);actual=perf(track)
 assert actual==expected,('MIDI not yet received',track,expected,actual)
 return actual
def cc(ch,n,v):d.o.send(mido.Message('control_change',channel=ch,control=n,value=v));time.sleep(.035)
def bend(ch,value):d.o.send(mido.Message('pitchwheel',channel=ch,pitch=value));time.sleep(.035)
def note(ch,on,n=60):d.o.send(mido.Message('note_on' if on else 'note_off',channel=ch,note=n,velocity=100 if on else 0));time.sleep(.035)
def panel_radio():d.ui(100,0,30);return struct.unpack('<46i',d.call(9)[:184])[38]
def restore_preferences():
 for i,m in enumerate(oldmaps):
  d.command(139,i,0)
  for c,v in [(140,m[1]),(141,m[2]),(136,m[4]),(137,m[5])]:d.command(c,i,v)
  if m[3]:d.command(142,i,m[3])
  if m[0]:d.command(143,i,1)
 for i,ch in enumerate(routes):d.command(125,i,ch)
 d.command(126,0,macro)
 if radio is not None and panel_radio()!=radio:d.remote([3,3,65 if radio else 63]);time.sleep(.2)
def capture(x,n,t,status):
 meter['frames']+=n;meter['energy']+=float(np.sum(x*x));meter['overflows']+=int(bool(status.input_overflow));meter['nonfinite']+=int(not np.isfinite(x).all());chunks.append(x.copy())
def exercise(name,patch,seconds,record=False,traffic=False,loaded=False):
 d.command(16,0,0)
 if not loaded:d.upload(patch)
 d.ui(100,0,1);d.call(3);before=stats();old=dict(meter);off=len(chunks);seen=[];start=time.monotonic();d.command(16,0,1);j=0
 try:
  while time.monotonic()-start<seconds:
   time.sleep(.25);s=stats();seen.append(s)
   for k in ('xruns','overloads','nonfinite','drops'):assert s[k]==before[k],(name,k,before,s)
   assert s['playing'],name
   if traffic:
    for k in range(4):
     value=(j*877+k*173)%16384
     d.o.send(mido.Message('control_change',channel=15,control=16+k,value=value>>7));d.o.send(mido.Message('control_change',channel=15,control=48+k,value=value&127))
   d.ui(100,0,[1,3,6,19,36,38,39,12,0][int((time.monotonic()-start)//3)%9]);j+=1
 finally:d.command(16,0,0)
 after=stats();audio=np.concatenate(chunks[off:]);pct=max(s['peak_us']/s['budget_us']*100 for s in seen)
 assert pct<85,(name,pct);assert meter['overflows']==old['overflows'] and meter['nonfinite']==old['nonfinite']
 assert len(audio)>44100*seconds*.8 and np.max(np.abs(audio))>1e-6
 row=dict(name=name,seconds=round(time.monotonic()-start,2),before=before,after=after,callback_peak_percent=round(pct,2),usb_peak=float(np.max(np.abs(audio))),usb_rms=float(np.sqrt(np.mean(audio*audio))),readings=seen)
 report['cases'].append(row);print('PASS',name,round(pct,2),'% callback',flush=True);flush()
 if record:
  with wave.open(str(O/'Audio'/f'FM1-{name}.wav'),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(44100);w.writeframes((np.clip(audio,-1,1)*32767).astype('<i2').tobytes())
 # Captures are already saved/summarized; keep the host's own memory bounded.
 chunks.clear()
try:
 d=Bench('Mutable Dreams FM-1');check('exact v11 FM-1 / 420 tones / 197 catalog entries',struct.unpack('<8I',d.call(7))==(1,11,2604,40,16,64,420,197))
 d.command(16,0,0);d.command(61,0,0);d.upload(HOME);stored=d.stored(0);d.remote([4,0,0]);changed=True
 for i in range(16):d.command(143,i,0)
 d.command(125,0,9);d.command(125,1,10);d.command(126,0,1)
 if not finish_clock:
  # Real USB channel controller state, including a sustained released key.
  bend(8,-8192);cc(8,11,0);cc(8,64,127);note(8,True);note(8,False)
  p=perf(0);check('USB bend / expression / pedal / held note reach the DSP',p[:4]==[0,-2000,0,1] and p[4:]==[0,0,0])
  cc(8,121,0);check('USB Reset Controllers centers, restores expression and releases pedal',perf(0)==[-1,0,1000,0,-1,-1,-1])
  # MIDI Learn must be picked up, then reset only by the matching reset message.
  for c,v in [(136,5),(137,2),(140,0),(141,9),(142,74),(143,1)]:d.command(c,15,v)
  d.ui(110,0,15);d.command(41,2,600);cc(8,74,0);check('USB learned knob waits for pickup',struct.unpack_from('<i',d.snapshot(),56+2*88+72)[0]==600)
  cc(8,74,127);check('USB learned knob crosses and applies',struct.unpack_from('<i',d.snapshot(),56+2*88+72)[0]==1000)
  cc(7,121,0);check('unrelated channel reset keeps pickup',struct.unpack('<6i',d.call(17))[3]==1)
  cc(8,121,0);check('matching channel reset rearms pickup',struct.unpack('<6i',d.call(17))[3]==0);d.command(143,15,0)
  for k in range(4):
   for value in (0,8192,16383):
    cc(15,16+k,value>>7);cc(15,48+k,value&127);check(f'USB 14-bit macro {k+1} value {value}',struct.unpack_from('<i',d.snapshot(),12+4*k)[0]==value*1000//16383)
  # Independent BLE removal with USB still holding another channel.
  radio=panel_radio()
  if not radio:d.remote([3,3,65]);time.sleep(.5)
  bend(9,4096);cc(9,11,64);cc(9,64,127);note(9,True,62);usb=wait_perf(1,[0,1000,503,1,0,0,0]);check('concurrent USB controller is physically received before BLE comparison',True)
  probe=subprocess.Popen([str(O/'ble-disconnect-probe')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
  deadline=time.monotonic()+24;lines=[];staged=False
  while time.monotonic()<deadline and probe.poll() is None:
   ready,_,_=select.select([probe.stdout],[],[],.25)
   if ready:
    line=probe.stdout.readline();lines.append(line.rstrip())
    if 'staged=1' in line:
     time.sleep(.2);state=perf(0);staged=state[0]==1 and state[1]==-2000 and state[2:]==[0,1,1,1,1];report['ble_staged_state']=state
  if probe.poll() is None:probe.terminate();probe.wait(timeout=3)
  lines.extend(probe.stdout.read().splitlines());(O/'BLE-Disconnect.log').write_text('\n'.join(lines)+'\n');report['ble_probe_returncode']=probe.returncode
  if staged:
   time.sleep(.5);report['usb_before_ble']=usb;report['usb_after_ble']=perf(1);report['ble_released_state']=perf(0);check('physical BLE disconnect clears its held note and transient controls',report['ble_released_state']==[-1,0,1000,0,-1,-1,-1]);check('physical BLE disconnect preserves concurrent USB controller state',perf(1)==usb);report['physical_ble_disconnect_tested']=True
  else:report['physical_ble_disconnect_tested']=False;report['ble_pending_reason']='No confirmed staged BLE controller; see BLE-Disconnect.log. No success inferred from packet submission.'
  for ch in (8,9):cc(ch,123,0);cc(ch,121,0)
 # Read-only mirroring is active during every audio case, with a second input
 # listening to the same exact device; no firmware image or bank is written.
 watch=MirrorWatch('Mutable Dreams FM-1',O);started=time.monotonic()
 full=watch.full();report['mirror_initial']=watch.summary();check('USB mirror sends the entire native 240 x 240 screen after connect',full)
 report['mirror_initial_seconds']=round(time.monotonic()-started,3)
 watch.screen.png(O/'Screens/FM1-Mirror-Initial.png',2)
 # All new worlds plus sustained dense configurations, with real USB capture.
 matches=[i for i,v in enumerate(sd.query_devices()) if v['name']=='Mutable Dreams FM-1' and v['max_input_channels']==2];assert len(matches)==1
 stream=sd.InputStream(device=matches[0],samplerate=44100,channels=2,dtype='float32',blocksize=1024,callback=capture);stream.start();d.remote([4,96,18])
 if not finish_clock:
  for i in range(189,197):exercise(f'Biome-{i}',factory[i],20 if i in (189,190,195) else 8,record=i in (189,190,195))
  dense=bytearray(factory[196]);struct.pack_into('<i',dense,28,220);struct.pack_into('<i',dense,40,1000);struct.pack_into('<i',dense,48,850)
  for i in range(6):
   at=56+i*88
   for off,v in [(16,500),(20,750),(24,16),(28,16),(32,0),(36,1000),(48,0),(52,1),(60,4),(76,1000),(80,2)]:struct.pack_into('<i',dense,at+off,v)
  exercise('six-skins-max-repeats-long-cloud',dense,60)
  mixed=bytearray(dense)
  for i,model in enumerate((17,15,16,9,8,14)):struct.pack_into('<i',mixed,56+i*88,model)
  exercise('mixed-18-engine-rack-cc-and-pages-soak',mixed,180,traffic=True)
 # External USB-MIDI clock, mirror, audio and effects at once.
 d.upload(factory[191]);d.command(61,0,1);clock_run=threading.Event();clock_run.set()
 def clock_sender():
  at=time.monotonic();period=60/(108*24)
  while clock_run.is_set():
   d.o.send(mido.Message('clock'));at+=period;time.sleep(max(0,at-time.monotonic()))
 ct=threading.Thread(target=clock_sender,daemon=True);ct.start()
 exercise('usb-clock-mirror-fx-108bpm',factory[191],120,traffic=True,loaded=True)
 check('external USB clock drives tempo and sequencer events',abs(stats()['tempo']-108)<=3 and any(a>b for a,b in zip(report['cases'][-1]['after']['events'],report['cases'][-1]['before']['events'])))
 clock_run.clear();ct.join(timeout=2);clock_run=None;d.command(61,0,0)
 # Real saved-bank export must pre-empt screen traffic without data loss.
 backup_file=O/'Mirror-Saved-Export.mddb'
 with (O/'Mirror-Backup.log').open('w') as log:
  subprocess.run([sys.executable,str(R/'tools/dreams_backup.py'),'--port','Mutable Dreams FM-1','backup',str(backup_file)],stdout=log,stderr=subprocess.STDOUT,check=True)
 check('all 22 saved objects export exactly while mirror is active',dict(B.parse(backup_file.read_bytes(),KIND)[1])==parts)
 report['mirror_active']=watch.summary();check('USB mirror rects and full pixel coverage remain valid',watch.frames>100 and watch.screen.bad==0 and sum(watch.cover)==57600)
 watch.screen.png(O/'Screens/FM1-Mirror-Final.png',2)
 # A viewer that stops asking must cause transmission to time out, no audio reset.
 watch.keep=False;time.sleep(7);f=watch.frames;time.sleep(1)
 check('mirror times out when viewer keep-alives stop',watch.frames==f)
 watch.close();watch=None
 # Restore globals before any saved write, then qualify new-model project save.
 restore_preferences();changed=False
 d.upload(factory[189]);saved=True;d.save();check('Skin Biome saved and read back exactly',d.stored(0)==factory[189]);report['after_stopped_save']=stats();report['pass']=True
except BaseException as e:report['errors'].append(repr(e));raise
finally:
 if clock_run is not None:clock_run.clear()
 if watch is not None:watch.close()
 if probe is not None and probe.poll() is None:probe.terminate();probe.wait(timeout=3)
 if stream is not None:stream.stop();stream.close()
 if d:
  try:
   d.command(16,0,0);d.command(61,0,0);d.remote([1,0,0]);d.remote([2,0,0,0,0])
   if changed:restore_preferences()
   if saved and stored is not None and d.stored(0)!=stored:d.upload(stored);d.save()
   d.upload(HOME);d.ui(100,0,0);report['owner_live_restored']=d.snapshot()==HOME;report['saved_project_restored']=d.stored(0)==stored;d.remote([4,1,32])
  except Exception as e:report['cleanup_error']=repr(e)
  d.close()
 report['host_overflows']=meter['overflows'];flush();print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2),flush=True)
