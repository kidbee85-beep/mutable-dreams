#!/usr/bin/env python3
"""Read-only export of live Mutable Dreams state and its saved flash object.
The saved object may belong to a previous firmware. No implicit conversion.
Requires a stopped transport and bench protocol 2 (Mutable Dreams 0.3.1 and newer).
"""
import argparse,hashlib,json,struct,zlib
from pathlib import Path
from hardware_bench import Device

def main():
 p=argparse.ArgumentParser();p.add_argument('--port');p.add_argument('--user-banks',action='store_true',help='Read 1.1 named Biome and tone objects as well');p.add_argument('directory');a=p.parse_args()
 d=Device(a.port);out=Path(a.directory);out.mkdir(parents=True,exist_ok=True)
 try:
  state=d.status()
  if state['playing']:raise SystemExit('Stop playback before the snapshot.')
  live=d.patch();assert live==d.patch(),'Live patch changed during snapshot'
  (out/'Mutable-Dreams-Live.raw').write_bytes(live)
  (out/'Mutable-Dreams-Live.mdp').write_bytes(struct.pack('<IIII',0x50444d44,1,len(live),zlib.crc32(live))+live)
  (out/'Mutable-Dreams-Live.json').write_text(json.dumps(dict(format='mutable-dreams-patch',version=1,name='Backed-up live Biome',patch=list(live)),indent=2)+'\n')
  def saved():
   size,magic,version=struct.unpack('<III',d.call(6));assert 12<=size<=3840
   b=b''.join(d.call(5,[off&127,off>>7]) for off in range(0,size,224));assert len(b)==size
   return b,magic,version
  try:
   b,magic,version=saved();assert b==saved()[0],'Saved object changed'
   (out/'FM1-Gespeichertes-Projekt.raw').write_bytes(b)
   if magic==0x4D445231:
    (out/'Mutable-Dreams-Saved.json').write_text(json.dumps(dict(format='mutable-dreams-patch',version=1,name='Backed-up saved Biome',patch=list(b)),indent=2)+'\n')
   if magic==0x50444143:(out/'Cut-A-Dor-Gespeichertes-Projekt.cadp').write_bytes(struct.pack('<IIII',0x4a504443,1,len(b),zlib.crc32(b))+b)
   info={'bytes':len(b),'magic':hex(magic),'version':version,'sha256':hashlib.sha256(b).hexdigest(),'two_identical_reads':True}
  except RuntimeError as e:
   if str(e)!='command 6: status 3':raise
   info={'status':'no valid saved object'}
  report={'live_sha256':hashlib.sha256(live).hexdigest(),'live_bytes':len(live),'saved':info,'device_status':state,'read_only':True}
  if a.user_banks:
   banks=[]
   for obj in range(3,23):
    def read_bank():
     size=struct.unpack_from('<I',d.call(6,[obj]))[0];assert 12<=size<=3840
     data=b''.join(d.call(5,[off&127,off>>7]) for off in range(0,size,224));assert len(data)==size
     return data
    try:
     data=read_bank();assert data==read_bank(),'User bank changed during snapshot'
    except RuntimeError as e:
     if str(e)!='command 6: status 3':raise
     banks.append(dict(object=obj,empty=True));continue
    file=('User-Biome-%02d.raw'%(obj-2)) if obj<19 else ('User-Tones-%02d.raw'%(obj-18))
    (out/file).write_bytes(data);banks.append(dict(object=obj,file=file,bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),two_identical_reads=True))
   report['user_banks']=banks
   report['scope']='Live patch, saved project and 20 user-bank objects. Not a full-flash/calibration backup. Bank uploads/restoration are not implemented by this export tool.'
  (out/'Projekt-Sicherungspruefung.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
 finally:d.close()
if __name__=='__main__':main()
