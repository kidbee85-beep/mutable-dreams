"""Isolate any stopped-control underrun without altering saved objects."""
import json,struct,time
from pathlib import Path
import sounddevice as sd
from device_editor import Editor
R=Path(__file__).resolve().parents[1];out=R/'../../outputs/Mutable-Dreams-2.0-Journey-Workbench/Device-Tests/Stopped-Control-Timing.json';home=(R/'backups/Before-Journey-2026-11-10/live-migrated-v10.raw').read_bytes();d=Editor('Mutable Dreams FM-1');rows=[]
def stats():
 w=struct.unpack('<49I',d.call(1));return dict(ms=w[1],frames=w[6],playing=w[10],xruns=w[4],overloads=w[5],peak_us=w[37],budget_us=w[38])
def record(name,before):
 after=stats();rows.append(dict(name=name,before=before,after=after,delta_xruns=after['xruns']-before['xruns']));print(rows[-1],flush=True)
try:
 d.command(16,0,0)
 for i in range(8):
  before=stats();d.upload(home);record('stopped-live-upload-'+str(i),before)
 for i in range(8):
  before=stats();d.bank(0,14,0,1);record('stopped-bank-recall-'+str(i),before)
 d.upload(home)
 devices=sd.query_devices();ids=[i for i,v in enumerate(devices) if v['name']=='Mutable Dreams FM-1' and v['max_input_channels']==2];assert len(ids)==1
 for i in range(3):
  before=stats()
  with sd.InputStream(device=ids[0],samplerate=44100,channels=2,callback=lambda *a:None):time.sleep(.3)
  time.sleep(.1);record('USB-input-open-close-'+str(i),before)
finally:
 d.upload(home);d.close();out.write_text(json.dumps(rows,indent=2)+'\n')
