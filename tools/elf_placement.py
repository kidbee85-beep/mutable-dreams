#!/usr/bin/env python3
"""2.4: the biggest objects (or, with --functions, functions) per ELF section of a device build.
usage: elf_placement.py app/build/mutable-dreams.elf [SECTION ...]   (TOP=n for the count; FUNCTIONS=1 for functions)"""
import struct,sys,collections
d=open(sys.argv[1],'rb').read()
e='<'
shoff,=struct.unpack_from(e+'I',d,0x20); shentsize,shnum,shstrndx=struct.unpack_from(e+'HHH',d,0x2E)
secs=[struct.unpack_from(e+'IIIIIIIIII',d,shoff+i*shentsize) for i in range(shnum)]
def nm(strsec,off):
    o=secs[strsec][4]+off; return d[o:d.index(b'\0',o)].decode('latin1')
names=[nm(shstrndx,s[0]) for s in secs]
tot=collections.Counter(); syms=collections.defaultdict(list)
for s in secs:
    if s[1]!=2: continue
    for k in range(s[5]//16):
        n_,v,sz,info,oth,shn=struct.unpack_from(e+'IIIBBH',d,s[4]+k*16)
        if shn==0 or shn>=0xff00 or (info&15) not in ((2,) if __import__("os").environ.get("FUNCTIONS") else (1,)): continue
        sec=names[shn]
        syms[sec].append((sz,nm(s[6],n_),v)); tot[sec]+=sz
want=sys.argv[2:] or ['.bss','.data','.pool']
for sec in sorted(syms):
    print("%-14s %8d B in %d objects"%(sec,tot[sec],len(syms[sec])))
for sec in want:
    print("\n== largest in",sec)
    for sz,n,v in sorted(syms.get(sec,[]),reverse=True)[:int(__import__('os').environ.get('TOP','22'))]:
        print("  %7d  %s"%(sz,n[:78]))
