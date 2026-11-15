#!/usr/bin/env python3
"""Private FM-1 test: MIDI gates, six voices, USB audio and mirror; never flashes or saves banks."""
import argparse, hashlib, json, struct, threading, time
from pathlib import Path
import mido
import numpy as np
import sounddevice as sd
from creative_bench import Bench
from hardware_bench import Device
from mirror_watch_20 import MirrorWatch
from dreams_backup import B, KIND

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('build','backup','home','midi-patch','output'):p.add_argument('--'+name,required=True)
 p.add_argument('--seconds',type=int,default=180)
 p.add_argument('--profiles',choices=('both','full','eco'),default='both')
 p.add_argument('--continue-eco-headroom-warning',action='store_true',help='Measure audio after a 90%% warning; strict headroom acceptance still fails.')
 p.add_argument('--skip-midi',action='store_true');a=p.parse_args()
 assert a.seconds>=180 and Path(a.backup).is_file()
 saved=dict(B.parse(Path(a.backup).read_bytes(),KIND)[1])
 assert len(saved[2])==156,'This bench restoration expects the original 156-byte preferences (MIDI-out/monitor defaults).'
 home=Path(a.home).read_bytes();fixture=Path(a.midi_patch).read_bytes()
 assert len(home)==len(fixture)==2604
 out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
 report=dict(build=a.build,pass_=False,strict_headroom_pass=True,midi_cases=[],readings=[],errors=[])
 d=watch=audio=listener=None;clocking=threading.Event();clock=None
 meter=dict(frames=0,energy=0.,peak=0.,overflows=0,nonfinite=0,channel_energy=[0.]*8,channel_peaks=[0.]*8)
 messages=[];quit_listener=threading.Event();note_lock=threading.Lock();thread=None
 before_ui=None;changed=False
 def flush():(out/'Qualification.json').write_text(json.dumps(report,indent=2)+'\n')
 def turn(enc,n):d.remote([3,enc,64+n])
 def tap(n):
  mask=1<<n;d.remote([1,mask&127,(mask>>7)&127]);time.sleep(.05);d.remote([1,0,0]);time.sleep(.05)
 def channels(on):
  d.ui(100,0,17)
  for i in range(6):d.ui(101,0,i);turn(2,-16);turn(2,i+1 if on else 0)
 def collect():
  while not quit_listener.is_set():
   for m in listener.iter_pending():
    if m.type in ('note_on','note_off'):
     with note_lock:messages.append((time.monotonic(),m.type,m.channel,m.note,m.velocity))
   time.sleep(.001)
 def wait_notes(start,kind,count,timeout=.3):
  end=time.monotonic()+timeout
  while time.monotonic()<end:
   with note_lock:v=[m for m in messages[start:] if m[1]==kind]
   if len(v)>=count:return v
   time.sleep(.002)
  raise AssertionError(f'{kind}: expected {count}, got {len(v)}')
 def mark():
  with note_lock:return len(messages)
 def capture(x,n,t,status):
  meter['frames']+=n;meter['energy']+=float(np.sum(x*x));meter['peak']=max(meter['peak'],float(np.max(np.abs(x))))
  meter['channel_energy']=[a+float(b) for a,b in zip(meter['channel_energy'],np.sum(x*x,axis=0))]
  meter['channel_peaks']=[max(a,float(b)) for a,b in zip(meter['channel_peaks'],np.max(np.abs(x),axis=0))]
  meter['overflows']+=int(bool(status.input_overflow));meter['nonfinite']+=int(not np.isfinite(x).all())
 def send_clock():
  at=time.monotonic()
  while clocking.is_set():
   d.o.send(mido.Message('clock'));at+=60/(108*24);time.sleep(max(0,at-time.monotonic()))
 def setup(external=False):
  d.command(16,0,0);d.command(61,0,0);d.upload(fixture)
  if external:d.command(61,0,1)
  start=mark()
  if external:d.o.send(mido.Message('start'));d.o.send(mido.Message('clock'))
  else:d.command(16,0,1)
  wait_notes(start,'note_on',6);return start
 try:
  d=Bench('Mutable Dreams FM-1')
  cap=struct.unpack('<8I',d.call(7));assert cap[1:3]==(12,2604) and cap[6:]==(640,276),cap
  before_ui=struct.unpack('<16i',d.call(9)[:64]);report['before_ui']=list(before_ui)
  assert not before_ui[2],'Stop playback first'
  assert d.snapshot() in (home,saved[0]),'Live project changed since the verified backup/upgrade; do not start.'
  report['before']=Device.status(d);report['home_sha256']=hashlib.sha256(home).hexdigest()
  # Speaker/headphone monitor muted for the bench; USB master/stems remain available.
  changed=True;d.ui(100,0,43);turn(3,-1);d.command(61,0,0);d.upload(fixture)
  channels(True);listener=mido.open_input('Mutable Dreams FM-1')
  thread=threading.Thread(target=collect,daemon=True);thread.start()
  if not a.skip_midi:
   for name,action,offs in [('mute',lambda:d.command(12,2,1),1),
                             ('midi_stop',lambda:d.o.send(mido.Message('stop')),6),
                             ('clock_change',lambda:d.command(61,0,0),6),
                             ('kit_change',lambda:d.command(79,0,1),3)]:
    setup(name in ('midi_stop','clock_change'));start=mark();at=time.monotonic();action()
    found=wait_notes(start,'note_off',offs);latency=max(m[0] for m in found[:offs])-at
    assert latency<.3
    report['midi_cases'].append(dict(name=name,note_offs=offs,max_latency_ms=round(latency*1000,2)));d.command(16,0,0)
   setup(True);start=mark();at=time.monotonic();found=wait_notes(start,'note_off',6,2.6)
   report['midi_cases'].append(dict(name='clock_timeout',note_offs=6,max_latency_ms=round((max(m[0]for m in found)-at)*1000,2)))
  d.command(16,0,0);d.command(61,0,0);d.upload(home)
  # Dense mixed patch uses existing engines and the normal quality, not reduced reserves.
  patch=bytearray(home);struct.pack_into('<i',patch,28,108);struct.pack_into('<i',patch,52,0)
  for i,model in enumerate((17,15,16,9,8,14)):
   offset=56+i*88
   for field,value in ((0,model),(16,500),(20,750),(24,16),(28,16),(32,0),(36,1000),(48,0),(52,1),(60,4),(76,1000),(80,2)):
    struct.pack_into('<i',patch,offset+field,value)
  # Disable captured sequencers/Journey/kit without depending on private struct offsets.
  d.upload(bytes(patch));d.command(128,0,0);d.command(79,0,0)
  for i in range(6):d.command(64,i,0)
  for i in range(6):d.command(46,i,700);d.command(47,i,700)
  for i in range(6):d.command(58,i,0)
  d.command(14,3,700);d.command(22,0,850);d.command(24,0,700);d.command(51,0,700)
  d.command(61,0,1);d.ui(103,0,1);d.ui(100,0,0)
  watch=MirrorWatch('Mutable Dreams FM-1',out);assert watch.full(),'Missing initial mirror pixels'
  devices=[i for i,v in enumerate(sd.query_devices())if v['name']=='Mutable Dreams FM-1' and v['max_input_channels']==8]
  assert len(devices)==1
  audio=sd.InputStream(device=devices[0],samplerate=44100,channels=8,dtype='float32',blocksize=1024,callback=capture);audio.start()
  # FULL is quality=1. A lighter six-engine ensemble fits the admission guard
  # with effects. The heavier ensemble is a separate, explicitly selected ECO test.
  clocking.set();clock=threading.Thread(target=send_clock,daemon=True);clock.start()
  report['profiles']=[]
  for label,models,quality,seconds in [('FULL-six-native-voices',(21,10,11,15,14,12),1,180),
                                       ('ECO-dense-six-voices',(17,15,16,9,8,14),0,a.seconds)]:
   if a.profiles!='both' and (quality==1)!=(a.profiles=='full'):continue
   d.command(16,0,0);d.command(25,0,0)
   for i,model in enumerate(models):d.command(0,i,model)
   # Separate measured envelopes: FULL with LP12/room/echo, dense ECO with
   # LP24 plus grain bus/room/echo. Prior FULL+LP24+grain limits remain logged.
   for i in range(6):d.command(43,i,1 if quality else 2)
   d.command(14,3,0 if quality else 700)
   d.command(25,0,quality);time.sleep(.1);base=Device.status(d)
   assert base['quality']==quality,('Admission guard rejected requested profile',label,base)
   d.call(3);base=Device.status(d)
   audio_start=dict(frames=meter['frames'],energy=meter['channel_energy'][:]);last_energy=meter['energy'];quiet=0
   d.o.send(mido.Message('start'));began=time.monotonic();readings=[]
   while time.monotonic()-began<seconds:
    time.sleep(1);s=Device.status(d);readings.append(s);report['readings'].append(s)
    assert s['playing'] and s['events']!=base['events']
    for key in ('xruns','nonfinite','dropped','quality_reductions'):assert s[key]==base[key],key
    if s['overloads']!=base['overloads']:
     report['strict_headroom_pass']=False
     assert not quality and a.continue_eco_headroom_warning,'overloads'
    assert s['quality']==quality
    quiet=quiet+1 if meter['energy']-last_energy<1e-9 else 0;last_energy=meter['energy']
    assert quiet<3,'USB audio silent for three consecutive readings'
    if len(readings)%30==0:print(label,len(readings),s['callback_peak_us'],s['callback_budget_us'],flush=True);flush()
   assert all(s['events'][i]>base['events'][i] for i in range(6)),'Not all six voices triggered'
   elapsed=time.monotonic()-began;frames=meter['frames']-audio_start['frames']
   energies=[b-a for a,b in zip(audio_start['energy'],meter['channel_energy'])]
   assert frames>=44100*elapsed*.95,'Missing USB audio frames during this profile'
   assert all(v>1e-9 for v in energies),'Silent USB master/stem during this profile'
   report['profiles'].append(dict(name=label,quality=quality,filters=1 if quality else 2,grain_macro=0 if quality else 700,before=base,after=s,readings=readings,audio_frames=frames,seconds=elapsed,channel_energy=energies));flush()
  # Four new Biomes, still using active mirror/audio/clock. Verify every switch.
  for biome in range(272,276):
   d.command(20,0,biome);d.command(78,0,1);time.sleep(10)
   live=d.snapshot();assert struct.unpack_from('<i',live,620)[0]==biome
   s=Device.status(d);report['readings'].append(s)
   for key in ('xruns','nonfinite','dropped','quality_reductions'):assert s[key]==base[key],key
   if s['overloads']!=base['overloads']:
    report['strict_headroom_pass']=False
    assert a.continue_eco_headroom_warning,'overloads'
  clocking.clear();clock.join(timeout=2);d.o.send(mido.Message('stop'));d.command(16,0,0)
  report['audio']=dict(meter);report['mirror']=watch.summary();assert meter['frames']>44100*a.seconds*.9 and meter['energy']>1e-6
  assert all(v>1e-9 for v in meter['channel_energy']),'A USB master/stem channel was silent throughout'
  assert meter['overflows']==meter['nonfinite']==watch.screen.bad==watch.gaps==0
  d.command(61,0,0);d.upload(home)
  for slot in (0,13,14,15):
   try:d.bank(0,slot,0,1)
   except RuntimeError as e:
    if str(e)!='command 15: status 8':raise
    tap(1)
   assert struct.unpack_from('<I',d.snapshot(),4)[0]==12
  report['legacy_user_slots_loaded']=[1,14,15,16];report['pass_']=True
  report['hardware_acceptance_pass']=report['strict_headroom_pass']
 except BaseException as e:
  report['errors'].append(repr(e));raise
 finally:
  clocking.clear()
  def cleanup(name,fn):
   try:fn()
   except BaseException as e:report['errors'].append(name+': '+repr(e));report['pass_']=False
  if clock:cleanup('clock.join',lambda:clock.join(timeout=3))
  if audio:cleanup('audio.stop',audio.stop);cleanup('audio.close',audio.close)
  if watch:
   cleanup('mirror.summary',lambda:report.__setitem__('mirror',watch.summary()))
   cleanup('mirror.close',watch.close)
  if d and changed:
   try:
    d.command(16,0,0);d.command(61,0,0);d.command(79,0,0);channels(False);d.upload(home)
    d.ui(100,0,43);turn(3,1) # original 156-byte preferences have no monitor-mute trailer
    d.ui(103,0,before_ui[5]);d.command(61,0,before_ui[14]);d.ui(101,0,before_ui[1]);d.ui(100,0,before_ui[0])
    report['home_restored_byte_identical']=d.snapshot()==home
   except BaseException as e:report['errors'].append('RESTORE: '+repr(e));report['pass_']=False
  quit_listener.set()
  if thread:cleanup('listener.join',lambda:thread.join(timeout=2))
  if listener:cleanup('listener.close',listener.close)
  if d:cleanup('editor.close',d.close)
  held={};on_count=off_count=0;orphan=duplicate=0
  for _,kind,ch,n,velocity in messages:
   key=(ch,n)
   if kind=='note_on' and velocity:
    duplicate+=key in held;held[key]=True;on_count+=1
   else:
    orphan+=key not in held;held.pop(key,None);off_count+=1
  report['midi_output']=dict(note_ons=on_count,note_offs=off_count,remaining=len(held),orphans=orphan,duplicates=duplicate)
  if held or orphan or duplicate or on_count!=off_count:
   report['pass_']=False;report['errors'].append('Unbalanced MIDI ledger')
  flush()
 assert report['pass_'] and report.get('home_restored_byte_identical'),report['errors']
 print('FUNCTIONAL_PASS',a.build,'HEADROOM_PASS',report['strict_headroom_pass'],report['midi_cases'],report['mirror'],flush=True)

if __name__=='__main__':main()
