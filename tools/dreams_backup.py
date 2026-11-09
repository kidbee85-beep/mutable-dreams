#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-3.0-only
"""Mutable Dreams saved project/preferences, 16 Biomes and 64 sounds.
Export the live patch separately. Backup never implicitly saves it.
Restore requires stopped transport, internal clock and physical SEL on the FM-1.
"""
import argparse,sys
from pathlib import Path
from midi_target import select_port,DEFAULT_PORT
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'base/tools'))
import fm1_backup as B
KIND=B.Kind(magic=b'MDDB',version=1,objs=(0,2,*range(3,23)),
 names=lambda o:'Project' if o==0 else 'Preferences' if o==2 else f'Biome {o-2:02}' if o<19 else f'Tones {(o-19)*16+1}-{(o-18)*16}',
 identity='FM-1_710',name='Mutable Dreams',port=DEFAULT_PORT)
KIND.saved_only=True

class ExactBackend(B.I.MidoBackend):
 def __init__(self,port=None):
  super().__init__();self.name=select_port(self.mido,port)
 def input_names(self):return [self.name]
 def output_names(self):return [self.name]
 def open(self,in_name,out_name):
  if in_name!=self.name or out_name!=self.name:raise ValueError('MIDI target changed; no device opened.')
  select_port(self.mido,self.name)
  return super().open(in_name,out_name)

def main(argv=None):
 argv=list(sys.argv[1:] if argv is None else argv)
 select=argparse.ArgumentParser(add_help=False);select.add_argument('--port');select.add_argument('command',nargs='?')
 args,_=select.parse_known_args(argv)
 # File inspection and help must not initialize CoreMIDI.
 if args.command=='info' or any(v in argv for v in ('-h','--help')):
  return B.main(argv,kind=KIND,description=__doc__)
 try:return B.main(argv,backend=ExactBackend(args.port),kind=KIND,description=__doc__)
 except (ImportError,ValueError) as e:print(f'error: {e}',file=sys.stderr);return 2

if __name__=='__main__':
 print('Saved objects only. Unsaved live edits, granular audio and calibration are excluded.')
 sys.exit(main())
