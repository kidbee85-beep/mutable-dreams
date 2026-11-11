"""Private creative qualification: one exact FM-1; USB input capture only.
No flash install and no writes to occupied User Biome banks. Always restore live state.
"""
from pathlib import Path
import struct,json,time,wave,threading
import mido,numpy as np,sounddevice as sd
from device_editor import Editor
R=Path(__file__).resolve().parents[1];OUT=(R/'../../outputs/Mutable-Dreams-2.0-Creative-Workbench/Device-Tests').resolve();OUT.mkdir(parents=True,exist_ok=True)
HOME=(R/'backups/Before-Creative-2026-11-10/live-migrated-v11.raw').read_bytes();DEMO=bytes(json.loads((OUT.parent/'Living-Tidelands.json').read_text())['patch'])
class Bench(Editor):
 def remote(self,p):
  self.o.send(mido.Message('sysex',data=[0x7d,0x4c,0x43,*p]));end=time.monotonic()+3
  while time.monotonic()<end:
   for msg in self.i.iter_pending():
    if msg.type=='sysex' and list(msg.data)==[0x7d,0x4c,0x43,p[0]]:time.sleep(.02);return
   time.sleep(.002)
  raise TimeoutError('No remote ACK')
 def stats(self):
  w=struct.unpack('<49I',self.call(1));return dict(ms=w[1],cpu=w[2],xruns=w[4],overloads=w[5],frames=w[6],nonfinite=w[7],queue_drops=w[8],quality=w[9],playing=w[10],events=list(w[11:17]),grain_writes=w[31],callback_peak_us=w[37],callback_budget_us=w[38],quality_reductions=w[40],ui_peak_ms=w[48])
 def creative(self):return list(struct.unpack('<6i',self.call(17)))
 def state(self):return struct.unpack('<46i',self.call(9)[:184])
results={'identity':'FM-1_710020000','cases':[]};d=Bench('Mutable Dreams FM-1');stream=None;chunks=[];overflows=[0];halt=threading.Event();traffic=None
try:
 cap=struct.unpack('<8I',d.call(7));assert cap[:4]==(1,11,2604,40),cap
 d.command(16,0,0);d.command(61,0,0);d.upload(HOME);assert d.snapshot()==HOME
 d.command(41,2,600);d.command(136,15,5);d.command(137,15,2);d.command(138,15,1);d.ui(110,0,15)
 def cc(v):d.o.send(mido.Message('control_change',channel=8,control=74,value=v));time.sleep(.06)
 cc(127);assert struct.unpack_from('<i',d.snapshot(),56+2*88+18*4)[0]==600
 cc(0);assert not d.creative()[3];cc(127);assert d.creative()[3] and struct.unpack_from('<i',d.snapshot(),56+2*88+18*4)[0]==1000
 d.command(41,2,600);assert not d.creative()[3];d.ui(100,0,35);assert list(d.state()[38:42])==[0,9,74,1]
 d.command(139,15,0);d.upload(HOME);results['learn_capture_pickup_manual_reset_and_metadata']=True
 def capture(x,n,t,status):
  if status.input_overflow:overflows[0]+=1
  chunks.append(x.copy())
 matches=[i for i,v in enumerate(sd.query_devices()) if v['name']=='Mutable Dreams FM-1' and v['max_input_channels']==2];assert len(matches)==1
 stream=sd.InputStream(device=matches[0],samplerate=44100,channels=2,dtype='float32',blocksize=1024,callback=capture);stream.start();d.remote([4,96,18])
 def exercise(name,patch,seconds,record=False,midi=False):
  global traffic
  d.command(16,0,0);d.upload(patch);d.ui(100,0,38);d.call(3);base=d.stats();off=len(chunks);seen=[];start=time.monotonic();last=0
  if midi:
   d.command(136,15,6);d.command(137,15,2);d.command(138,15,1);cc(0)
   def send():
    j=0
    while not halt.wait(.025):d.o.send(mido.Message('control_change',channel=8,control=74,value=j%128));j+=7
   traffic=threading.Thread(target=send);traffic.start()
  d.command(16,0,1)
  while time.monotonic()-start<seconds:
   time.sleep(.3);seen.append(d.stats())
   for k in ['xruns','overloads','nonfinite','queue_drops']:assert seen[-1][k]==base[k],(name,k,base,seen[-1])
   assert seen[-1]['playing'],name
   if time.monotonic()-start-last>=20:last=time.monotonic()-start;print(name,round(last),seen[-1],flush=True)
  halt.set()
  if traffic:traffic.join();traffic=None
  d.command(16,0,0);after=d.stats();a=np.concatenate(chunks[off:]);assert np.isfinite(a).all() and np.max(np.abs(a))>1e-5
  pct=max(v['callback_peak_us']/v['callback_budget_us']*100 for v in seen);assert pct<85,(name,pct)
  item=dict(name=name,seconds=time.monotonic()-start,baseline=base,final=after,telemetry=seen,peak=float(np.max(np.abs(a))),rms=float(np.sqrt(np.mean(a*a))),callback_peak_percent=pct,pass_=True);results['cases'].append(item)
  if record:
   with wave.open(str(OUT/'Living-Tidelands-FM1-USB.wav'),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(44100);w.writeframes((np.clip(a,-1,1)*32767).astype('<i2').tobytes())
  (OUT/'Creative-Device-Results.json').write_text(json.dumps(results,indent=2)+'\n');print('PASS',name,round(pct,2),'percent callback peak',flush=True)
 exercise('warm-living-tidelands',DEMO,75,True)
 dense=bytearray(DEMO);struct.pack_into('<i',dense,28,180)
 for i in range(6):
  b=56+i*88
  for off,v in [(0,9),(16,380),(20,600),(24,8),(28,16),(32,0),(36,1000),(48,0),(52,1)]:struct.pack_into('<i',dense,b+off,v)
  dense[1292+i*132]=0
 struct.pack_into('<i',dense,40,1000);struct.pack_into('<i',dense,48,650);struct.pack_into('<i',dense,52,0)
 for j in range(4):struct.pack_into('<h',dense,2356+j*8+4,1000 if j%2==0 else -1000)
 exercise('six-elements-grain-four-links-auto-drift-midi',dense,120,midi=True)
 d.command(139,15,0)
 # Save a useful creative example only to a genuinely empty slot.
 d.upload(DEMO);slot=None
 for i in range(15,-1,-1):
  try:d.bank(0,i,0,0)
  except RuntimeError as e:
   if str(e)=='command 15: status 3':slot=i;break
   raise
 if slot is not None:
  before=d.stats();d.bank(0,slot,0,2,'LIVING TIDELANDS');after=d.stats();d.upload(HOME);d.bank(0,slot,0,1);assert d.snapshot()==DEMO
  results['saved_creative_slot']=slot+1;results['saved_recalled_exactly']=True;results['stopped_save_timing_delta']={k:after[k]-before[k] for k in ('xruns','overloads','nonfinite','queue_drops')}
 else:results['demo_not_saved_reason']='No empty slot; no occupied bank modified.'
 results['cold_start_tested']=False
finally:
 halt.set()
 if traffic:traffic.join()
 try:
  d.command(16,0,0);d.command(61,0,0);d.command(139,15,0);d.upload(HOME);assert d.snapshot()==HOME;results['original_live_restored']=True;d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.remote([4,1,32])
 except Exception as e:results['restore_error']=str(e)
 if stream is not None:stream.stop();stream.close()
 d.close();results['host_input_overflows']=overflows[0];(OUT/'Creative-Device-Results.json').write_text(json.dumps(results,indent=2)+'\n');print('COMPLETE',json.dumps({k:v for k,v in results.items() if k!='cases'}),flush=True)
