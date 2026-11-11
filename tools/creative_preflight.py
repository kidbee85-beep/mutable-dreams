"""Identify one exact Mutable Dreams FM-1 and preserve its current live state."""
from pathlib import Path
import json,struct,hashlib,time
from device_editor import Editor
out=Path(__file__).resolve().parents[1]/'backups/Before-Creative-2026-11-10';out.mkdir(parents=True,exist_ok=True)
d=Editor('Mutable Dreams FM-1')
try:
 cap=struct.unpack('<8I',d.call(7));assert cap[:3]==(1,10,2356),cap
 w=struct.unpack('<49I',d.call(1));state=struct.unpack('<46i',d.call(9)[:184]);live=d.snapshot();assert live==d.snapshot()
 (out/'live-before-stop.raw').write_bytes(live)
 if state[2]:d.command(16,0,0)
 if state[14]:d.command(61,0,0)
 time.sleep(.2);live=d.snapshot();assert live==d.snapshot()
 (out/'live-stopped.raw').write_bytes(live)
 (out/'live-stopped.json').write_text(json.dumps({'format':'mutable-dreams-patch','version':1,'firmware':'2.0-dev-v10','name':'Before creative controls','patch':list(live)})+'\n')
 report={'capabilities':cap,'port':'Mutable Dreams FM-1','was_playing':state[2],'clock_source':state[14],'master':w[39],'telemetry_before':list(w),'live_sha256':hashlib.sha256(live).hexdigest(),'two_matching_reads':True,'scope':'Live only; saved objects exported separately. No transient granular audio or calibration.'}
 (out/'Preflight.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ('was_playing','clock_source','master','live_sha256','two_matching_reads')},indent=2))
finally:d.close()
