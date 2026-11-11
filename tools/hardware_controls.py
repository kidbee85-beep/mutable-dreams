#!/usr/bin/env python3
"""Exercise real panel-input paths over USB-MIDI. Writes one test patch on SAVE.
Use only after backing up the user's project. Ends stopped with Velvet Mist,
with a deliberate 83 BPM persistence marker saved for the restart check.
"""
import argparse,json,struct,time,hashlib
from pathlib import Path
from hardware_bench import Device

def main():
 a=argparse.ArgumentParser();a.add_argument('--port',required=True);a.add_argument('--output',required=True);args=a.parse_args()
 d=Device(args.port);checks=[]
 def check(name,condition):
  assert condition,name;checks.append(name);print('PASS',name,flush=True)
 def patch():
  data=d.patch();return struct.unpack('<%di'%(len(data)//4),data)
 def turn(role,n):d.remote([3,role,n+64]);time.sleep(.18)
 def saved():
  size,magic,version=struct.unpack('<III',d.call(6));return b''.join(d.call(5,[off&127,off>>7]) for off in range(0,size,224))
 try:
  d.command(27,0,0);d.command(20,0,0);d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.remote([4,0,16]);d.tap(6);turn(0,-63)
  before=patch();turn(3,1);check('HOME K1 density',patch()[3]==before[3]+20);turn(3,-1)
  turn(1,-63);check('ALGO freely selects Kick on first slot',patch()[14]==0);turn(1,7);check('ALGO selects Choir on same slot',patch()[14]==7)
  d.tap(4);before=patch()
  for role in [3,4,5,6]:turn(role,1)
  w=patch();check('FILTER four knobs via panel path',[w[14+i]-before[14+i] for i in [18,19,20,21]]==[20,20,1,20])
  d.tap(4);before=patch();turn(3,1);turn(4,1);w=patch();check('ENVELOPE independent attack and decay',w[28]==before[28]+20 and w[17]==before[17]+20)
  d.tap(6);before=d.patch();d.remote([1,0,16]);time.sleep(1.05);d.remote([1,0,0]);time.sleep(.2);check('hold REC does not mutate',before==d.patch());d.tap(6)
  before=d.patch();d.tap(11);check('short REC mutates',before!=d.patch());d.tap(2);check('ENV restores pre-mutation patch',before==d.patch())
  d.tap(1);check('SEL mutes selected slot',patch()[26]==1);d.tap(1);check('SEL unmutes selected slot',patch()[26]==0)
  before=d.status()['events'];d.remote([2,1,0,0,0]);time.sleep(.2);d.remote([2,0,0,0,0]);after=d.status()['events'];check('physical-key input triggers selected voice',after[0]==before[0]+1)
  d.command(27,0,0);d.command(20,0,0);factory=d.patch();d.tap(7);time.sleep(.3);check('SAVE persists exact stopped patch',saved()==factory)
  d.command(15,0,83);d.command(16,0,1);d.tap(7);d.command(27,0,0);check('SAVE refused during playback',saved()==factory)
  d.tap(7);time.sleep(.3);raw=d.patch();check('modified patch saved exactly',saved()==raw)
  target=Path(args.output);target.with_suffix('.raw').write_bytes(raw);target.write_text(json.dumps({'checks':checks,'saved_sha256':hashlib.sha256(raw).hexdigest(),'tempo_marker':83,'status':d.status()},indent=2))
 finally:
  d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.command(27,0,0);d.remote([4,1,32]);d.close()
if __name__=='__main__':main()
