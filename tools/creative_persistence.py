"""Prepare/verify a physical cold start for the private creative build.
Requires explicit owner power cycling between prepare and verify. No firmware flash.
"""
from pathlib import Path
import sys,struct,json,time,mido
from creative_bench import Bench
R=Path(__file__).resolve().parents[1];OUT=(R/'../../outputs/Mutable-Dreams-2.0-Creative-Workbench/Device-Tests').resolve();HOME=(R/'backups/Before-Creative-2026-11-10/live-migrated-v11.raw').read_bytes();DEMO=bytes(json.loads((OUT.parent/'Living-Tidelands.json').read_text())['patch'])
d=Bench('Mutable Dreams FM-1');result={};mode=sys.argv[1]
try:
 assert struct.unpack('<8I',d.call(7))[:4]==(1,11,2604,40)
 assert d.snapshot()==HOME,'Owner live state changed; no overwrite'
 if mode=='prepare':
  d.command(16,0,0);d.command(61,0,0);d.upload(HOME)
  d.command(136,15,5);d.command(137,15,5);d.command(138,15,1);d.o.send(mido.Message('control_change',channel=14,control=119,value=127));time.sleep(.1)
  before=d.stats();d.save();after=d.stats();assert d.stored(0)==HOME
  d.ui(100,0,0);result=dict(host_time=time.time(),device_ms=after['ms'],boot_epoch_ms=time.time()*1000-after['ms'],saved_global_mapping=True,saved_owner_home=True,before_save=before,after_save=after,stopped_save_delta={k:after[k]-before[k] for k in ('xruns','overloads','nonfinite','queue_drops')})
  (OUT/'Cold-Start-Prepared.json').write_text(json.dumps(result,indent=2)+'\n');print('Prepared: owner home plus one temporary learned CC saved; stopped and ready for genuine power removal.',flush=True)
 elif mode=='verify':
  result['owner_project_loaded_at_boot']=True
  previous=json.loads((OUT/'Cold-Start-Prepared.json').read_text());now=d.stats();new_epoch=time.time()*1000-now['ms'];result['boot_epoch_advance_ms']=new_epoch-previous['boot_epoch_ms'];assert result['boot_epoch_advance_ms']>5000,'No genuine cold start detected'
  d.ui(110,0,15);d.ui(100,0,35);state=struct.unpack('<46i',d.call(9)[:184]);assert list(state[38:42])==[0,15,119,1]
  d.o.send(mido.Message('control_change',channel=14,control=119,value=0));time.sleep(.08);d.o.send(mido.Message('control_change',channel=14,control=119,value=127));time.sleep(.08);assert struct.unpack_from('<i',d.snapshot(),56+5*88+18*4)[0]==1000;result['learned_mapping_reboot_and_pickup']=True
  device=json.loads((OUT/'Creative-Device-Results.json').read_text());slot=device.get('saved_creative_slot');assert slot
  try:d.bank(0,slot-1,0,1)
  except RuntimeError as e:assert str(e)=='command 15: status 8';result['unsaved_change_guard_verified']=True
  else:raise AssertionError('Dirty MIDI edit should require explicit discard before bank load')
  d.upload(HOME);d.bank(0,slot-1,0,1);assert d.snapshot()==DEMO;result['creative_biome_cold_recall']=True
  d.bank(0,14,0,1);old=(OUT/'Four-Worlds-migrated-v11.raw').read_bytes();assert d.snapshot()==old;result['previous_journey_biome_preserved']=True
  d.command(139,15,0);d.upload(HOME);before=d.stats();d.save();after=d.stats();assert d.stored(0)==HOME;result['test_mapping_cleared_and_owner_home_saved']=True;result['stopped_cleanup_save_delta']={k:after[k]-before[k] for k in ('xruns','overloads','nonfinite','queue_drops')};d.ui(100,0,0)
  result['pass']=True;(OUT/'Cold-Start-Verified.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2),flush=True)
 else:raise ValueError('Use prepare or verify')
finally:
 try:d.remote([1,0,0]);d.remote([2,0,0,0,0]);d.remote([4,1,32])
 finally:d.close()
