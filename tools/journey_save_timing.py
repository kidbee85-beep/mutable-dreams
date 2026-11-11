"""Measure only our own already-created demo slot; never replace another User Biome."""
from pathlib import Path
import struct,json
from device_editor import Editor
R=Path(__file__).resolve().parents[1];O=R/'../../outputs/Mutable-Dreams-2.0-Journey-Workbench/Device-Tests';demo=bytes(json.loads((O.parent/'Four-Worlds-Journey.json').read_text())['patch']);home=(R/'backups/Before-Journey-2026-11-10/live-migrated-v10.raw').read_bytes();d=Editor('Mutable Dreams FM-1');rows=[]
def stats():
 w=struct.unpack('<49I',d.call(1));return {'xruns':w[4],'overloads':w[5],'playing':w[10],'peak_us':w[37]}
try:
 d.command(16,0,0);name=d.bank(0,14,0,0).split(b'\0')[0];assert name==b'FOUR WORLDS JOURNEY',name;d.bank(0,14,0,1);assert d.snapshot()==demo
 for i in range(3):
  before=stats();d.bank(0,14,0,3,'FOUR WORLDS JOURNEY');after=stats();rows.append({'before':before,'after':after,'delta_xruns':after['xruns']-before['xruns']});print(rows[-1],flush=True)
finally:
 d.upload(home);d.close();(O/'Stopped-Save-Timing.json').write_text(json.dumps(rows,indent=2)+'\n')
