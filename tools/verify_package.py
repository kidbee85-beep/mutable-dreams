#!/usr/bin/env python3
"""Read container structure back and compare its decrypted application with this build."""
from pathlib import Path
import struct,sys,hashlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'base/tools'))
import fm1pkg_make as p
pkg,app=map(Path,sys.argv[1:3]);raw=pkg.read_bytes();image=app.read_bytes()
identity=''.join(chr((raw[i*48+47]-i-1)&255) for i in range(20) if raw[i*48+47]!=0x7d)
assert identity==(sys.argv[3] if len(sys.argv)>3 else 'FM-1_710010100'),identity
u=bytearray(b''.join(raw[i*48:i*48+47] for i in range(20))+raw[960:]);h=bytearray(u[:64]);p.enc(h,0,64)
assert struct.unpack_from('<H',h)[0]==p.crc16(h[2:])
size,count=struct.unpack_from('<IH',h,4);assert size==len(u) and count==2
assert struct.unpack_from('<H',h,2)[0]==p.crc16(u[64:64+count*80])
files={}
for i in range(count):
 e=bytearray(u[64+i*80:144+i*80]);p.enc(e,0,80);typ,idx,crc,_,off,n,padded=struct.unpack_from('<HHHHIII',e)
 assert off>=1024 and off+n<=len(u)
 name=bytes(e[64:]).split(b'\0')[0].decode();data=u[off:off+n];assert p.crc16(data)==crc;files[name]=data
f=files['flash.bin'];assert len(f)==p.FLASH_SIZE
r=bytearray(f[0x4000:]);p.sfc(r,0,len(r),0,p.KEY)
for off in (0,32,64):
 e=r[off:off+32];assert struct.unpack_from('<H',e)[0]==p.crc16(e[2:])
crc,a,n=struct.unpack_from('<HII',r,34);assert n==p.APP_SLOT
stored=r[a:a+n];assert p.crc16(stored)==crc
assert stored[:len(image)]==image and all(x==255 for x in stored[len(image):]);assert len(image)<=p.APP_SLOT
print('PASS: identity, container size, header/directory/file CRCs, app payload and padding')
print('Identity:',identity,'App bytes:',len(image),'App-slot headroom:',p.APP_SLOT-len(image))
print('SHA256:',hashlib.sha256(raw).hexdigest())
