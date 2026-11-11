"""Private Journey test; one verified FM-1, bounded audio capture, no overwrite of occupied banks."""
from pathlib import Path
import struct,json,time,hashlib,wave
import mido,numpy as np,sounddevice as sd
from device_editor import Editor
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'../../outputs/Mutable-Dreams-2.0-Journey-Workbench/Device-Tests';OUT.mkdir(exist_ok=True,parents=True)
HOME=(ROOT/'backups/Before-Journey-2026-11-10/live-migrated-v10.raw').read_bytes()
DEMO=bytes(json.loads((OUT.parent/'Four-Worlds-Journey.json').read_text())['patch'])
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
 def state(self):return struct.unpack('<46i',self.call(9)[:184])
results={'build':'D84A4D5E','identity':'FM-1_710020000','cases':[]};d=Bench('Mutable Dreams FM-1');stream=None;chunks=[];overflows=[0]
try:
 cap=struct.unpack('<8I',d.call(7));assert cap[:3]==(1,10,2356),cap
 devices=sd.query_devices();matches=[i for i,v in enumerate(devices) if v['name']=='Mutable Dreams FM-1' and v['max_input_channels']==2];assert len(matches)==1
 def capture(x,n,t,status):
  if status.input_overflow:overflows[0]+=1
  chunks.append(x.copy())
 stream=sd.InputStream(device=matches[0],samplerate=44100,channels=2,dtype='float32',blocksize=1024,callback=capture);stream.start()
 d.remote([4,96,18]);d.command(16,0,0);d.upload(DEMO);assert d.snapshot()==DEMO
 d.command(131,7,32);d.command(133,7,-24);read=d.snapshot();assert read[2350]==32 and read[2353]==232;d.upload(DEMO)
 results['extended_commands_verified']=True
 def exercise(name,seconds,once=False):
  d.call(3);base=d.stats();before=len(chunks);d.command(16,0,1);start=time.monotonic();last_report=0;samples=[];biomes=set();problem=[]
  while time.monotonic()-start<seconds:
   time.sleep(.25);s=d.stats();state=d.state();samples.append(s);biomes.add(state[6]);elapsed=time.monotonic()-start
   for k in ['xruns','overloads','nonfinite','queue_drops']:
    if s[k]!=base[k]:problem.append(k)
   if not once and not s['playing']:problem.append('transport stopped')
   if elapsed-last_report>=15:print(name,round(elapsed),s,flush=True);last_report=elapsed
   if problem:break
  d.command(16,0,0);end=d.stats();audio=np.concatenate(chunks[before:]) if len(chunks)>before else np.zeros((0,2));peak=float(np.max(np.abs(audio))) if len(audio) else 0
  if not np.isfinite(audio).all():problem.append('nonfinite captured audio')
  if peak<1e-5:problem.append('silent USB capture')
  if once and samples[-1]['playing']:problem.append('Once did not finish')
  peak_pct=max(v['callback_peak_us']/v['callback_budget_us']*100 for v in samples)
  if peak_pct>=85:problem.append('less than 15% callback margin')
  result=dict(name=name,seconds=round(time.monotonic()-start,2),base=base,final=end,samples=samples,biomes=sorted(biomes),captured_peak=peak,callback_peak_percent=peak_pct,issues=sorted(set(problem)),pass_=not problem)
  results['cases'].append(result);(OUT/'Journey-Device-Results.json').write_text(json.dumps(results,indent=2)+'\n');print('RESULT',name,result['pass_'],round(peak_pct,2),result['issues'],flush=True)
  if name=='four-worlds-once' and len(audio):
   with wave.open(str(OUT/'Four-Worlds-FM1-USB.wav'),'wb') as w:w.setnchannels(2);w.setsampwidth(2);w.setframerate(44100);w.writeframes((np.clip(audio,-1,1)*32767).astype('<i2').tobytes())
  assert not problem,result
 exercise('four-worlds-once',46,True);assert d.snapshot()==DEMO
 # Frequent factory/rack transitions at the highest clock, with the Journey display animating.
 fast=bytearray(DEMO);struct.pack_into('<i',fast,28,220);fast[2288]=2
 for i in range(4):fast[2292+i*8+2]=1
 d.upload(fast);d.ui(100,0,31);exercise('fast-four-section-transitions',62)
 assert d.snapshot()==bytes(fast)
 # Dense six-Elements home rack; no additional voices or granular buffers.
 dense=bytearray(DEMO);struct.pack_into('<i',dense,28,160);dense[2288]=2;dense[2289]=1;dense[2294]=1;dense[2296]=63
 for i in range(6):
  base=56+i*88
  for off,value in [(0,9),(16,450),(20,600),(24,8),(28,16),(32,0),(36,1000),(48,0),(52,1)]:struct.pack_into('<i',dense,base+off,value)
  dense[1292+i*132]=0
 struct.pack_into('<i',dense,40,1000);struct.pack_into('<i',dense,24,800);struct.pack_into('<i',dense,48,650);struct.pack_into('<i',dense,52,0)
 d.upload(dense);exercise('six-elements-grain-home-loop',32)
 d.command(16,0,0);d.upload(DEMO)
 # Use only a genuinely empty User Biome slot; leave a useful named demo for the owner.
 slot=None
 for i in range(15,-1,-1):
  try:d.bank(0,i,0,0)
  except RuntimeError as e:
   if str(e)=='command 15: status 3':slot=i;break
   raise
 assert slot is not None,'No empty slot; no occupied bank was changed'
 d.bank(0,slot,0,2,'FOUR WORLDS JOURNEY');d.upload(HOME);d.bank(0,slot,0,1);assert d.snapshot()==DEMO
 results['saved_demo_slot']=slot+1;results['saved_recalled_exactly']=True
finally:
 try:
  d.command(16,0,0);d.upload(HOME);assert d.snapshot()==HOME;results['original_live_restored']=True
  d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.remote([4,1,32])
 except Exception as e:results['restore_error']=str(e)
 if stream is not None:stream.stop();stream.close()
 d.close();results['host_input_overflows']=overflows[0];results['hardware_cold_start_tested']=False
 (OUT/'Journey-Device-Results.json').write_text(json.dumps(results,indent=2)+'\n')
 print('COMPLETE',json.dumps({k:v for k,v in results.items() if k!='cases'}),flush=True)
