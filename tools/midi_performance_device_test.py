"""Measure private2.0 fine CCs and RPN pitch on actual FM-1 USB audio; no Mac output."""
from pathlib import Path
import struct,json,time
import mido,numpy as np,sounddevice as sd
from device_editor import Editor
R=Path(__file__).resolve().parents[1];O=R/'../../outputs/Mutable-Dreams-2.0-Journey-Workbench/Device-Tests';HOME=(R/'backups/Before-Journey-2026-11-10/live-migrated-v10.raw').read_bytes();DEMO=bytes(json.loads((O.parent/'Four-Worlds-Journey.json').read_text())['patch'])
class Bench(Editor):
 def remote(self,p):
  self.o.send(mido.Message('sysex',data=[0x7d,0x4c,0x43,*p]));end=time.monotonic()+3
  while time.monotonic()<end:
   for m in self.i.iter_pending():
    if m.type=='sysex' and list(m.data)==[0x7d,0x4c,0x43,p[0]]:time.sleep(.02);return
   time.sleep(.002)
  raise TimeoutError('No panel ACK')
 def state(self):return struct.unpack('<46i',self.call(9)[:184])
 def stats(self):
  w=struct.unpack('<49I',self.call(1));return dict(ms=w[1],xruns=w[4],overloads=w[5],nonfinite=w[7],dropped=w[8],frames=w[6],peak_us=w[37],budget_us=w[38])
d=Bench('Mutable Dreams FM-1');result={'package_sha256':'3b50b89fc1ac72fde609d205fd991ebe6e631dd364c03e3291e9a697a552c613','macro_cases':[],'pitch_cases':[]};stream=None;chunks=[];overflows=[0];oldroute=None;oldmacro=None
try:
 assert struct.unpack('<8I',d.call(7))[:3]==(1,10,2356)
 d.ui(101,0,0);d.ui(100,0,17);s=d.state();oldroute=s[40];oldmacro=s[41];d.command(16,0,0);d.command(125,0,1);d.command(126,0,1)
 d.upload(HOME);before=d.stats()
 def cc(ch,n,v):d.o.send(mido.Message('control_change',channel=ch,control=n,value=v));time.sleep(.015)
 for k in range(4):
  cc(15,16+k,31);time.sleep(.03);coarse=struct.unpack_from('<i',d.snapshot(),12+4*k)[0];assert coarse==31*1000//127,coarse
  cc(15,48+k,127);fine=struct.unpack_from('<i',d.snapshot(),12+4*k)[0];assert fine==4095*1000//16383,fine
  cc(15,16+k,127);cc(15,48+k,127);full=struct.unpack_from('<i',d.snapshot(),12+4*k)[0];assert full==1000
  result['macro_cases'].append(dict(macro=k,legacy=coarse,fine=fine,full=full))
 # A clean existing Satin voice, exactly one audible track, no effects.
 p=bytearray(DEMO);p[2288]=0
 for i in range(6):
  base=56+i*88;struct.pack_into('<i',p,base+48,1);p[1292+i*132]=0
 base=56
 for off,value in [(0,11),(4,0),(8,0),(12,1000),(16,500),(20,0),(24,0),(40,57),(44,500),(48,0),(56,0),(68,0),(80,0),(84,0)]:struct.pack_into('<i',p,base+off,value)
 for i,value in enumerate([500,0,500,0]):struct.pack_into('<i',p,12+4*i,value)
 for off in [676,684,1288,2084,2088]:struct.pack_into('<i',p,off,0)
 struct.pack_into('<H',p,2264,0)
 d.upload(p);devices=sd.query_devices();ids=[i for i,v in enumerate(devices) if v['name']=='Mutable Dreams FM-1' and v['max_input_channels']==2];assert len(ids)==1
 def capture(x,n,t,status):
  if status.input_overflow:overflows[0]+=1
  chunks.append(x.copy())
 stream=sd.InputStream(device=ids[0],samplerate=44100,channels=2,dtype='float32',blocksize=1024,callback=capture);stream.start();d.remote([4,96,18]);d.o.send(mido.Message('note_on',channel=0,note=57,velocity=100))
 def measure(name,expected):
  time.sleep(.2);at=len(chunks);time.sleep(1.2);x=np.concatenate(chunks[at:]).mean(axis=1);assert np.isfinite(x).all() and np.max(np.abs(x))>.0001
  x=x-np.mean(x);v=np.abs(np.fft.rfft(x*np.hanning(len(x))));freq=np.fft.rfftfreq(len(x),1/44100);v[(freq<100)|(freq>800)]=0;i=int(np.argmax(v));delta=.5*(v[i-1]-v[i+1])/(v[i-1]-2*v[i]+v[i+1]);found=(i+delta)*44100/len(x)
  row=dict(name=name,expected_Hz=expected,measured_Hz=float(found));result['pitch_cases'].append(row);print(row,flush=True);assert abs(found-expected)<1.5,row
 measure('centre / A3',220)
 d.o.send(mido.Message('pitchwheel',channel=0,pitch=8191));measure('default two semitones',220*2**((8191/8192*2)/12))
 cc(0,101,0);cc(0,100,0);cc(0,6,12);cc(0,38,0);measure('RPN octave',220*2**((8191/8192*12)/12))
 cc(0,38,50);measure('RPN octave plus50cents',220*2**((8191/8192*12.5)/12))
 cc(0,99,3);cc(0,98,4);cc(0,6,1);measure('NRPN cannot alter bend',220*2**((8191/8192*12.5)/12))
 cc(0,121,0);measure('reset controllers centres wheel',220)
 d.o.send(mido.Message('note_off',channel=0,note=57,velocity=0));after=d.stats()
 for k in ['xruns','overloads','nonfinite','dropped']:assert after[k]==before[k],(k,before,after)
 assert overflows[0]==0
 result.update(before=before,after=after,host_input_overflows=overflows[0],pass_=True)
finally:
 try:
  d.o.send(mido.Message('note_off',channel=0,note=57,velocity=0));cc(0,101,0);cc(0,100,0);cc(0,6,2);cc(0,38,0);cc(0,101,127);cc(0,100,127);d.o.send(mido.Message('pitchwheel',channel=0,pitch=0))
  if oldroute is not None:d.command(125,0,oldroute)
  if oldmacro is not None:d.command(126,0,oldmacro)
  d.upload(HOME);assert d.snapshot()==HOME;result['original_live_restored']=True;d.ui(100,0,0);d.remote([4,1,32])
 except Exception as e:result['restore_error']=str(e)
 if stream is not None:stream.stop();stream.close()
 d.close();(O/'MIDI-Performance-Device.json').write_text(json.dumps(result,indent=2)+'\n');print('COMPLETE',json.dumps(result),flush=True)
