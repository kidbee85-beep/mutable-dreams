#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-3.0-only
"""Live-editor client for Mutable Dreams 1.4 development (mido + python-rtmidi).
Uploads change the live patch only; explicit bank saves are separate.
"""
import argparse,json,struct,time,zlib
from pathlib import Path
import mido
from midi_target import select_port

def pack(data):
 out=[]
 for off in range(0,len(data),7):
  b=data[off:off+7];out.append(sum((v>>7)<<j for j,v in enumerate(b)));out.extend(v&127 for v in b)
 return out

def unpack(data):
 out=bytearray();at=0
 while at<len(data):
  mask=data[at];at+=1
  for j in range(min(7,len(data)-at)):out.append(data[at]|((mask>>j&1)<<7));at+=1
 return bytes(out)

class Editor:
 def __init__(self,port=None):
  port=select_port(mido,port);self.i=mido.open_input(port)
  try:self.o=mido.open_output(port)
  except BaseException:self.i.close();raise
  self.seq=0;time.sleep(.6)
 def close(self):self.i.close();self.o.close()
 def call(self,c,p=()):
  for attempt in range(2 if c in (1,7,9) else 1):
   self.seq=(self.seq+1)&127;head=[0x7d,0x4d,0x44,c,self.seq];self.o.send(mido.Message('sysex',data=head+list(p)));end=time.monotonic()+3
   while time.monotonic()<end:
    for msg in self.i.iter_pending():
     if msg.type!='sysex':continue
     r=list(msg.data)
     if r[:5]!=head:continue
     if len(r)<6:raise ValueError('Incomplete reply')
     time.sleep(.02)
     if r[5]:raise RuntimeError(f'command {c}: status {r[5]}')
     return unpack(r[6:])
    time.sleep(.002)
  raise TimeoutError(f'No reply to {c}; write not retried')
 def command(self,c,t,v):
  z=v+8192;return self.call(2,[c,t,z&127,z>>7])
 def ui(self,c,t,v):
  z=v+8192;return self.call(10,[c,t,z&127,z>>7])
 def snapshot(self):
  size,crc,token=struct.unpack('<III',self.call(8));assert size in (2220,2288) # read old v8 only for verified upgrade backup
  p=b''.join(self.call(5,[off&127,off>>7,token]) for off in range(0,size,224));assert len(p)==size and zlib.crc32(p)==crc
  return p
 def upload(self,p):
  assert len(p)==2288
  token=self.call(11,pack(struct.pack('<II',len(p),zlib.crc32(p))))[0]
  try:
   for off in range(0,len(p),196):self.call(12,[token,off&127,off>>7,*pack(p[off:off+196])])
   self.call(13,[token])
  except BaseException:
   try:self.call(14,[token])
   except Exception:pass
   raise
 def bank(self,kind,slot,track,action,name=''):
  b=name.encode('ascii');assert len(b)<=23 and all(32<=v<=126 for v in b)
  return self.call(15,[kind,slot,track,action,*((b+bytes(24-len(b))) if action>=2 else b'')])

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['info','export','import']);p.add_argument('file',nargs='?');p.add_argument('--port',help='Exact unique MIDI input/output name');a=p.parse_args();d=Editor(a.port)
 try:
  cap=struct.unpack('<8I',d.call(7));assert cap[:3]==(1,9,2288)
  if a.action=='info':print(json.dumps(dict(protocol=cap[0],patch_bytes=cap[2],biomes=cap[4],sounds=cap[5]),indent=2))
  elif a.action=='export':Path(a.file).write_text(json.dumps(dict(format='mutable-dreams-patch',version=1,firmware='1.4-dev',name='FM-1 Live Biome',patch=list(d.snapshot())),indent=2)+'\n')
  else:
   v=json.loads(Path(a.file).read_text());assert v['format']=='mutable-dreams-patch' and v['version']==1;d.upload(bytes(v['patch']));print('Live patch loaded. Use SAVE or User Banks explicitly to keep it.')
 finally:d.close()
if __name__=='__main__':main()
