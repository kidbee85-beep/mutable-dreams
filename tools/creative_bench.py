"""Read-only/volatile test controls; importing this module never opens a device."""
import time,struct,mido
from device_editor import Editor
class Bench(Editor):
 def remote(self,p):
  self.o.send(mido.Message('sysex',data=[0x7d,0x4c,0x43,*p]));end=time.monotonic()+3
  while time.monotonic()<end:
   for msg in self.i.iter_pending():
    if msg.type=='sysex' and list(msg.data)==[0x7d,0x4c,0x43,p[0]]:time.sleep(.03);return
   time.sleep(.002)
  raise TimeoutError('No remote ACK')
 def stats(self):
  w=struct.unpack('<49I',self.call(1));return dict(ms=w[1],xruns=w[4],overloads=w[5],nonfinite=w[7],queue_drops=w[8],playing=w[10])
 def stored(self,obj):
  n,magic,version=struct.unpack('<III',self.call(6,[obj]));assert n<=3840;return b''.join(self.call(5,[off&127,off>>7]) for off in range(0,n,224))
 def save(self):
  self.remote([1,64,0]);time.sleep(.1);self.remote([1,0,0]);self.ui(100,0,34);self.remote([1,0,1]);time.sleep(.15);self.remote([1,0,0]);time.sleep(.35)
