from pathlib import Path
import json,struct,time
from creative_bench import Bench
from dreams_backup import B,KIND
from mirror_watch_20 import MirrorWatch
R=Path.cwd();O=(R/'../../outputs/Mutable-Dreams-2.0-Stability-Lab').resolve();P=O/'Mirror-Coldstart';P.mkdir(exist_ok=True);back=R/'backups/Before-Stability-2026-11-10'
a=dict(B.parse((back/'Saved-Before-Flash.mddb').read_bytes(),KIND)[1]);b=dict(B.parse((back/'Saved-After-Mirror-Coldstart.mddb').read_bytes(),KIND)[1]);home=(back/'live-before-flash.raw').read_bytes();prior=json.loads((O/'Device-Final-State.json').read_text());report={'user_confirmed_all_power_removed':True,'all_22_saved_objects_exact':a==b,'pass':False};d=Bench('Mutable Dreams FM-1');w=None
try:
 report['boot']=d.stats();assert report['boot']['ms']<prior['state']['ms'] and report['boot']['ms']<300000,('No confirmed new uptime',report['boot'])
 assert a==b and len(a)==22
 report['capabilities']=list(struct.unpack('<8I',d.call(7)));assert report['capabilities']==[1,11,2604,40,16,64,420,197]
 report['boot_saved_patch_exact']=d.snapshot()==a[0];assert report['boot_saved_patch_exact']
 start=time.monotonic();w=MirrorWatch('Mutable Dreams FM-1',P);assert w.full();report['full_screen_seconds']=round(time.monotonic()-start,3)
 d.command(16,0,0);d.command(61,0,0);d.upload(bytes(json.loads((O/'Examples/Biome-189.json').read_text())['patch']));d.remote([4,96,18]);before=d.stats();d.command(16,0,1);seen=[];start=time.monotonic()
 while time.monotonic()-start<12:
  time.sleep(.5);s=d.stats();seen.append(s)
  for key in ('xruns','overloads','nonfinite','queue_drops'):assert s[key]==before[key],s
 report['post_boot_playback_seconds']=round(time.monotonic()-start,2);report['playback_before']=before;report['playback_after']=d.stats();report['mirror']=w.summary();assert report['mirror']['bad_frames']==report['mirror']['sequence_gaps']==0
 report['pass']=True
finally:
 if w:w.close()
 d.command(16,0,0);d.upload(home);d.ui(100,0,0);d.remote([4,1,32]);report['owner_live_exact']=d.snapshot()==home;report['final']=d.stats();d.close()
 (O/'Device-Coldstart.json').write_text(json.dumps(report,indent=2)+'\n')
 (O/'Device-Post-Coldstart-State.json').write_text(json.dumps({'all_22_saved_objects_exact':a==b,'owner_live_exact':report['owner_live_exact'],'capabilities':report['capabilities'],'state':report['final']},indent=2)+'\n')
 print(json.dumps(report,indent=2))
