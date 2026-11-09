#!/usr/bin/env python3
"""Mutable Dreams 0.4 bench protocol. Requires mido/python-rtmidi; no flash writes.
Every case verifies the applied patch, six triggered slots, progress, xruns,
CPU load and overload fallback. JSON includes raw readings, not host CPU estimates.
"""
import argparse,json,time,struct,statistics
from pathlib import Path
import mido
import sounddevice as sd
import numpy as np
from midi_target import select_port

class Meter:
 def __init__(self):
  self.frames=0;self.energy=0.;self.peak=0.;self.overflows=0;self.nonfinite=0
  devices=sd.query_devices();matches=[i for i,d in enumerate(devices) if d['name']=='Mutable Dreams FM-1' and d['max_input_channels']==2]
  if len(matches)!=1:raise ValueError('Need one unique Mutable Dreams audio input; no audio device opened.')
  idx=matches[0]
  self.name=devices[idx]['name'];self.stream=sd.InputStream(device=idx,samplerate=44100,channels=2,dtype='float32',blocksize=1024,callback=self.callback)
 def callback(self,x,n,t,status):
  self.frames+=n;self.energy+=float(np.sum(x*x));self.peak=max(self.peak,float(np.max(np.abs(x))));self.overflows+=int(bool(status.input_overflow));self.nonfinite+=int(not np.isfinite(x).all())
 def snapshot(self):return dict(source=self.name,frames=self.frames,energy=self.energy,peak=self.peak,host_input_overflows=self.overflows,nonfinite=self.nonfinite)

NAMES=['Kick','Snare','Hat','FM','VA','Rings','Harmonic','Choir']
class Device:
 def __init__(self,port=None):
  self.name=select_port(mido,port);self.i=mido.open_input(self.name)
  try:self.o=mido.open_output(self.name)
  except BaseException:self.i.close();raise
  self.seq=0;self.retries=0;self.read_retries=0
  time.sleep(.35) # CoreMIDI may discard the first packet immediately after opening.
 def close(self):self.i.close();self.o.close()
 def call(self,c,p=()):
  self.seq=(self.seq+1)%128;head=[0x7d,0x4d,0x44,c,self.seq]
  self.o.send(mido.Message('sysex',data=head+list(p)));deadline=time.monotonic()+3
  while time.monotonic()<deadline:
   for msg in self.i.iter_pending():
    if msg.type!='sysex':continue
    d=list(msg.data)
    if d[:5]!=head:continue
    if d[5]:raise RuntimeError(f'command {c}: status {d[5]}')
    raw=[];at=6
    while at<len(d):
     mask=d[at];at+=1
     for bit in range(min(7,len(d)-at)):raw.append(d[at]|((mask>>bit&1)<<7));at+=1
    # A reply may leave USB before the main loop releases its single RX slot.
    time.sleep(.015);return bytes(raw)
   time.sleep(.002)
  raise TimeoutError(f'No response to command {c}')
 def command(self,c,t,v):
  assert -8192<=v<=8191
  z=v+8192
  try:self.call(2,[c,t,z&127,z>>7])
  except TimeoutError:
   if c in [18,26]:raise
   self.retries+=1;print('RETRY absolute command',c,t,v,flush=True);time.sleep(.1);self.call(2,[c,t,z&127,z>>7])
  time.sleep(.03)
 def status(self):
  try:raw=self.call(1)
  except TimeoutError:
   self.read_retries+=1;print('RETRY read-only telemetry',flush=True);raw=self.call(1)
  w=struct.unpack('<%dI'%(len(raw)//4),raw);assert w[0] in [1,2]
  self.protocol=w[0]
  return dict(ui_frames=w[47] if len(w)>47 else None,ui_frame_peak_ms=w[48] if len(w)>48 else None,element_modes=w[46] if len(w)>46 else None,motion=w[41] if len(w)>41 else None,boot_sound=w[42] if len(w)>42 else None,page=w[43] if len(w)>43 else None,help=(w[44] if w[44]<0x80000000 else w[44]-0x100000000) if len(w)>44 else None,intro=w[45] if len(w)>45 else None,quality_reductions=w[40] if len(w)>40 else 0,master=w[39] if len(w)>39 else None,protocol=w[0],ms=w[1],cpu=w[2],cpu_peak=w[3],xruns=w[4],overloads=w[5],frames=w[6],nonfinite=w[7],dropped=w[8],quality=w[9],playing=w[10],events=list(w[11:17]),models=list(w[17:23]),filters=list(w[23:29]),memory=w[29],patch_version=w[30],grain_writes=w[31],freeze=w[32],peak=struct.unpack('<f',struct.pack('<I',w[33]))[0],voice_quanta=w[34],skipped_quanta=w[35],tempo=w[36],callback_peak_us=w[37],callback_budget_us=w[38])
 def command_many(self,items):
  if getattr(self,'protocol',1)<2:
   for c,t,v in items:self.command(c,t,v)
   return
  for off in range(0,len(items),16):
   payload=[]
   for c,t,v in items[off:off+16]:
    assert -8192<=v<=8191;z=v+8192;payload += [c,t,z&127,z>>7]
   try:self.call(2,payload)
   except TimeoutError:
    self.retries+=1;print('RETRY absolute command batch',flush=True);self.call(2,payload)
   time.sleep(.04)
 def patch(self):
  first=self.call(4,[0,0]);magic,version=struct.unpack_from('<II',first);assert magic==0x4D445231
  size={1:392,2:500,3:620,4:652,5:1292,6:1292,7:2100,8:2220,9:2288}[version]
  data=first+b''.join(self.call(4,[off&127,off>>7]) for off in range(224,size,224));assert len(data)==size
  return data
 def remote(self,p):
  # The board exposes one pending SysEx frame. Wait for its ACK before sending
  # a telemetry request or another panel event, instead of bursting two frames.
  self.o.send(mido.Message('sysex',data=[0x7d,0x4c,0x43,*p]));deadline=time.monotonic()+3
  while time.monotonic()<deadline:
   for msg in self.i.iter_pending():
    if msg.type=='sysex' and list(msg.data)==[0x7d,0x4c,0x43,p[0]]:time.sleep(.015);return
   time.sleep(.002)
  raise TimeoutError(f'No panel ACK for operation {p[0]}')
 def tap(self,b):
  self.remote([1,(1<<b)&127,((1<<b)>>7)&127]);time.sleep(.09);self.remote([1,0,0]);time.sleep(.06)

def configure(d,models,quality,filter_mode,stress=True,active=6):
 d.status();d.remote([4,0,16]);time.sleep(.05);d.command(27,0,0);d.command(20,0,1)
 for t,m in enumerate(models):
  d.command_many([(c,t,v) for c,v in [(12,int(t>=active)),(0,m),(7,16),(6,16 if stress else 4+t),(9,1000),(13,1),(3,1000),(28,0),(5,1000),(10,36+t*7),(30,4 if stress else 1),(40,500 if m==3 else 300),(41,700),(42,700),(43,filter_mode),(44,1000),(4,600)]])
 pre_admission=d.status()['quality_reductions']
 for c,t,v in [(25,0,quality),(24,0,850),(14,3,1000),(14,0,500),(15,0,220 if stress else 126),(29,0,600),(34,0,24),(35,0,1)]:d.command(c,t,v)
 time.sleep(.15)
 data=d.patch();w=struct.unpack('<%di'%(len(data)//4),data);assert [w[14+i*22] for i in range(6)]==models
 assert [w[14+i*22+20] for i in range(6)]==[filter_mode]*6
 assert w[13] in (quality,0)
 d.call(3);base=d.status();base['admission_guarded']=quality==1 and base['quality']==0 and base['quality_reductions']>pre_admission;d.command(16,0,1);return base

def case(d,name,models,q,flt,seconds,stress=True,active=6):
 base=configure(d,models,q,flt,stress,active);start=time.monotonic();samples=[];problem=[];guarded=False
 audio_before=d.meter.snapshot()
 last_energy=audio_before['energy'];quiet=0;last_motion=last_progress=0
 print('START',name,'seconds',seconds,flush=True)
 while time.monotonic()-start<seconds:
  time.sleep(min(1,max(.01,seconds-(time.monotonic()-start))));s=d.status();samples.append(s)
  audio_now=d.meter.snapshot();quiet=quiet+1 if audio_now['energy']<=last_energy+1e-12 else 0;last_energy=audio_now['energy']
  if quiet>=3:problem.append('USB silent for 3 readings')
  if audio_now['host_input_overflows']!=audio_before['host_input_overflows']:problem.append('host capture overflow')
  if audio_now['nonfinite']!=audio_before['nonfinite']:problem.append('USB nonfinite')
  elapsed=time.monotonic()-start
  if 'soak' in name and elapsed-last_motion>=10:
   last_motion=elapsed;j=int(elapsed//10)
   d.command_many([(22,0,(j*173)%1001),(23,0,(j*211)%1001),(24,0,(j*113)%851),(40,j%6,(j*137)%1001),(41,(j+1)%6,200+(j*97)%801),(42,j%6,(j*71)%1001),(14,2,(j*131)%1001),(34,0,24 if j%2 else -24),(35,0,j%2),(21,0,int(j%6==0)),(36,0,j%6),(37,0,(j*173)%1001)])
   d.tap([6,4,4,4,4,9,9,0,0,5,5,5][j%12])
  if elapsed-last_progress>=30:
   last_progress=elapsed;progress={'name':name,'elapsed':round(elapsed),'cpu_max_pct':round(s['callback_peak_us']/s['callback_budget_us']*100,2),'xruns_delta':s['xruns']-base['xruns'],'quality':s['quality'],'issues':problem}
   print('PROGRESS',json.dumps(progress),flush=True)
   if hasattr(d,'progress_path'):d.progress_path.write_text(json.dumps({**progress,'device':s,'audio':audio_now,'telemetry_retries':d.read_retries},indent=2))
  for key in ['xruns','overloads','nonfinite','dropped']:
   if s[key]!=base[key]:problem.append(key)
  if s['quality']!=q:
   if q==1 and s['quality']==0 and (s['quality_reductions']>base['quality_reductions'] or base.get('admission_guarded',False)) and s['overloads']==base['overloads']:guarded=True
   else:problem.append('quality fallback')
  if s['models']!=models:problem.append('models changed')
  if not s['playing']:problem.append('transport stopped')
  if s['frames']<=base['frames']:problem.append('audio not advancing')
  if problem:break
 end=d.status();elapsed=time.monotonic()-start;d.command(27,0,0)
 if any(b<=a for a,b in zip(base['events'][:active],end['events'][:active])):problem.append('not all voices triggered')
 if end['grain_writes']<=base['grain_writes']:problem.append('tape not recording')
 maximum=round(max(s['callback_peak_us']/s['callback_budget_us']*100 for s in samples),2)
 if maximum>=85:problem.append('less than 15% deadline margin')
 audio_after=d.meter.snapshot();af=audio_after['frames']-audio_before['frames'];ae=audio_after['energy']-audio_before['energy']
 if af<44100*elapsed*.8:problem.append('USB capture incomplete')
 if ae<=1e-9:problem.append('USB audio silent')
 result=dict(midi_retries=d.retries,active=active,audio_rms=(ae/(af*2))**.5 if af else 0,audio_before=audio_before,audio_after=audio_after,name=name,models=models,quality=q,filter=flt,requested_seconds=seconds,elapsed_seconds=round(elapsed,3),cpu_max=maximum,cpu_mean=round(statistics.mean(s['cpu'] for s in samples),2),pass_=not problem,issues=sorted(set(problem)),baseline=base,final=end,samples=samples)
 result['guarded_eco']=guarded;result['actual_quality']=end['quality'];result['telemetry_retries']=d.read_retries
 print('RESULT',name,('ECO GUARD' if guarded else 'PASS') if result['pass_'] else 'LIMIT',maximum,'%',result['issues'],flush=True);return result

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--port');ap.add_argument('--mode',choices=['smoke','preflight','matrix','soak','status'],default='status');ap.add_argument('--seconds',type=int);ap.add_argument('--output',default='work/hardware-v03.json');args=ap.parse_args()
 d=Device(args.port)
 try:
  if args.mode=='status':print(json.dumps(d.status(),indent=2));return
  d.meter=Meter();d.meter.stream.start()
  result={'port':d.name,'started':time.strftime('%Y-%m-%dT%H:%M:%S%z'),'mode':args.mode,'cases':[]};target=Path(args.output);d.progress_path=Path(str(target)+'.progress.json')
  if args.mode=='preflight':
   for m in range(8):
    for active in [1,2,3,4,5,6]:
     row=case(d,f'preflight-{NAMES[m]}-{active}',[m]*6,1,2,args.seconds or 4,active=active);result['cases'].append(row);target.write_text(json.dumps(result,indent=2))
     if not row['pass_'] or row['cpu_max']>70:break
   return
  if args.mode=='smoke':cases=[('six-choir-eco',[7]*6,0,2,args.seconds or 10)]
  elif args.mode=='soak':cases=[('mixed-ambient-soak',[7,6,5,4,3,7],0,2,args.seconds or 1800)]
  else:
   cases=[(f'six-{NAMES[m]}-{["ECO","FULL"][q]}',[m]*6,q,2,args.seconds or 60) for q in [0,1] for m in range(8)]
   cases += [('ambient-mixed',[7,6,5,4,3,7],1,1,args.seconds or 60),('techno-mixed',[0,2,1,3,5,0],1,2,args.seconds or 60),('lpg-mixed',[7,6,5,4,3,7],1,3,args.seconds or 60)]
  for c in cases:
   result['cases'].append(case(d,*c));target.write_text(json.dumps(result,indent=2))
  # A stopped factory scene is left ready for the next test; nothing is saved.
  d.command(20,0,0)
  if not all(c['pass_'] for c in result['cases']):raise SystemExit(2)
 finally:
  try:
   d.command(27,0,0)
   d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.remote([4,1,32])
  finally:
   if hasattr(d,'meter'):d.meter.stream.stop();d.meter.stream.close()
   d.close()
if __name__=='__main__':main()
