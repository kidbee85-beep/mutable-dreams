"""Identify and back up one exact Mutable Dreams device before private tests."""
from pathlib import Path
import json,struct,hashlib,time
from device_editor import Editor
out=Path(__file__).resolve().parents[1]/'backups/Before-Journey-2026-11-10'
out.mkdir(parents=True,exist_ok=True)
d=Editor('Mutable Dreams FM-1')
try:
 cap=struct.unpack('<8I',d.call(7));assert cap[:3]==(1,9,2288),cap
 w=struct.unpack('<49I',d.call(1));state=struct.unpack('<46i',d.call(9)[:184]);before=d.snapshot()
 (out/'live-before-stop.raw').write_bytes(before)
 (out/'live-before-stop.json').write_text(json.dumps({'format':'mutable-dreams-patch','version':1,'firmware':'1.5','name':'Before Journey tests','patch':list(before)})+'\n')
 if state[2]:d.command(16,0,0)
 if state[14]:d.command(61,0,0)
 time.sleep(.2);live=d.snapshot();assert live==d.snapshot()
 (out/'live-stopped.raw').write_bytes(live)
 (out/'live-stopped.json').write_text(json.dumps({'format':'mutable-dreams-patch','version':1,'firmware':'1.5','name':'Before Journey tests / stopped','patch':list(live)})+'\n')
 report={'capabilities':cap,'identity_verified_separately':'FM-1_710010500','port':'Mutable Dreams FM-1','was_playing':state[2],'clock_source':state[14],'master':w[39],'telemetry_before':list(w),'stopped_live_sha256':hashlib.sha256(live).hexdigest(),'two_matching_reads':True,'scope':'Live patches only. Saved objects are backed up separately. Granular tape is transient; this is not a full-flash/calibration backup.'}
 (out/'Preflight.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['identity_verified_separately','was_playing','clock_source','master','two_matching_reads','stopped_live_sha256']},indent=2))
finally:d.close()
