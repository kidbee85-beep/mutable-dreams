"""Verify saved Journey after user-confirmed cold start, then clock it over USB."""
from pathlib import Path
import struct,json,time,threading
import mido
from device_editor import Editor
R=Path(__file__).resolve().parents[1];OUT=R/'../../outputs/Mutable-Dreams-2.0-Journey-Workbench/Device-Tests';DEMO=bytes(json.loads((OUT.parent/'Four-Worlds-Journey.json').read_text())['patch']);HOME=(R/'backups/Before-Journey-2026-11-10/live-migrated-v10.raw').read_bytes()
d=Editor('Mutable Dreams FM-1');stop=threading.Event();thread=None;result={'build':'D84A4D5E','cold_start_confirmed_by_owner':True}
def stats():
 w=struct.unpack('<49I',d.call(1));return dict(xruns=w[4],overloads=w[5],nonfinite=w[7],dropped=w[8],playing=w[10],frames=w[6],events=list(w[11:17]),peak_us=w[37],budget_us=w[38])
try:
 assert struct.unpack('<8I',d.call(7))[:3]==(1,10,2356)
 d.command(16,0,0);d.bank(0,14,0,1);assert d.snapshot()==DEMO;result['cold_start_bank_exact']=True
 fast=bytearray(DEMO);fast[2288]=2
 for i in range(4):fast[2294+i*8]=1
 d.upload(fast);d.command(61,0,1);d.call(3);before=stats();biomes=set();samples=[];sent=[0];jitter=[]
 def clock():
  start=time.perf_counter();d.o.send(mido.Message('start'))
  for i in range(768):
   target=start+i*60/(120*24)
   while not stop.is_set():
    left=target-time.perf_counter()
    if left<=0:break
    time.sleep(min(left,.001))
   if stop.is_set():return
   jitter.append(time.perf_counter()-target);d.o.send(mido.Message('clock'));sent[0]+=1
 thread=threading.Thread(target=clock);thread.start()
 while thread.is_alive():
  time.sleep(.3);samples.append(stats());state=struct.unpack('<46i',d.call(9)[:184]);biomes.add(state[6])
 thread.join();time.sleep(2.4);after=stats();assert not after['playing'],'Clock-loss stop failed'
 for k in ['xruns','overloads','nonfinite','dropped']:assert after[k]==before[k],k
 assert set([169,171,174,176])<=biomes,biomes
 assert after['frames']>before['frames'];result.update(clock_pulses=sent[0],biomes=sorted(biomes),clock_loss_stop=True,before=before,after=after,samples=samples,host_send_jitter_peak_ms=max(jitter)*1000,pass_=True)
 print('Cold start / USB clock / clock-loss stop passed',flush=True)
finally:
 stop.set()
 if thread:thread.join()
 try:
  d.command(16,0,0);d.command(61,0,0);d.upload(HOME);assert d.snapshot()==HOME;result['original_live_restored']=True
 finally:d.close();(OUT/'Cold-Start-USB-Clock.json').write_text(json.dumps(result,indent=2)+'\n')
