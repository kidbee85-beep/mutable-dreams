"""Read-only USB mirror qualification, existing fm1-base decoder and protocol."""
import sys,time,threading,struct
from pathlib import Path
import mido
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'base/tools'))
import fm1_mirror as M
class MirrorWatch:
 def __init__(self,port,outdir):
  self.input=mido.open_input(port);self.output=mido.open_output(port)
  self.screen=M.Screen();self.cover=bytearray(240*240);self.last_seq=None
  self.frames=self.inputs=self.hellos=self.gaps=self.bytes=self.messages=0
  self.alive=True;self.keep=True;self.panel=None;self.outdir=Path(outdir);self.file=(self.outdir/'Mirror-USB.syx').open('wb')
  self.output.send(mido.Message.from_bytes(list(M.B.frame(0x50,b'\x02'))))
  self.worker=threading.Thread(target=self.loop,daemon=True);self.worker.start()
 def loop(self):
  next_hello=time.monotonic()+1
  while self.alive:
   now=time.monotonic()
   if self.keep and now>=next_hello:
    self.output.send(mido.Message.from_bytes(list(M.B.frame(0x50,b'\x01'))));next_hello=now+1
   for msg in self.input.iter_pending():
    if msg.type!='sysex':continue
    pkt=bytes(msg.bytes());f=M.B.parse_frame(pkt)
    if not f or f[0] not in (0x50,0x51,0x52,0x53,0x54):continue
    cmd,b=f;self.file.write(pkt);self.messages+=1;self.bytes+=len(pkt)
    if cmd in (0x51,0x52):
     if self.last_seq is not None and b[4]!=(self.last_seq+1)&255:self.gaps+=1
     self.last_seq=b[4];self.screen.frame(pkt);self.frames+=1
     x,y,w,h=b[:4]
     for row in range(y,min(y+h,240)):self.cover[row*240+x:row*240+min(x+w,240)]=bytes([1])*min(w,240-x)
    elif cmd==0x53:
     self.inputs+=1
     if len(b)>=27:self.panel=dict(buttons=struct.unpack_from('<H',b,15)[0],keys=struct.unpack_from('<I',b,17)[0],dim_buttons=struct.unpack_from('<H',b,21)[0],dim_keys=struct.unpack_from('<I',b,23)[0])
    elif cmd==0x50:self.hellos+=1
   time.sleep(.002)
 def summary(self):return dict(frames=self.frames,input_reports=self.inputs,hellos=self.hellos,bytes=self.bytes,bad_frames=self.screen.bad,sequence_gaps=self.gaps,pixels_received=sum(self.cover),total_pixels=57600)
 def full(self,timeout=15):
  deadline=time.monotonic()+timeout
  while time.monotonic()<deadline and sum(self.cover)<57600:time.sleep(.1)
  return sum(self.cover)==57600
 def close(self):
  self.keep=False;self.output.send(mido.Message.from_bytes(list(M.B.frame(0x50,b'\x00'))));time.sleep(.1);self.alive=False;self.worker.join(timeout=3);self.file.close();self.input.close();self.output.close()
