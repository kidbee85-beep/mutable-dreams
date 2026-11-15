#!/usr/bin/env python3
"""Compile the real app drain and cross-feature tests against built DSP objects."""
from pathlib import Path
import os,re,subprocess,sys,tomllib
R=Path(__file__).resolve().parents[1];A=R/'app';B=R/'base'
san='--sanitize' in sys.argv;out=A/'build'/('san' if san else 'native')
c=tomllib.loads((A/'fm1.toml').read_text())
source=(A/'app.c').read_text();match=re.search(r'static void midi_out_drain\(void\)\s*\{',source)
assert match,'Expected real app.c drain definition'
start=match.start();at=match.end()-1;depth=1;end=at+1
while depth:
 if source[end]=='{':depth+=1
 elif source[end]=='}':depth-=1
 end+=1
g=A/'build/gen';g.mkdir(parents=True,exist_ok=True)
(g/'midi_out_drain_test.inc').write_text(source[start:end]+'\n')
flags=['-O2','-ffp-contract=off','-DPLAT_HOST','-DTEST','-ffunction-sections','-fdata-sections']
flags += [f'-I{p}' for p in [A,B/'host',B/'device',B/'include',B/'lib',g,A/'third_party']]
if sys.platform=='darwin':
 sdk=os.environ.get('SDKROOT','/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')
 if Path(sdk).exists():flags+=['-isysroot',sdk]
for key,define in [('mirror','FM1B_MIRROR'),('backup','FM1B_BACKUP')]:
 if c.get(key):flags+=['-D'+define+'=1']
if c.get('usb_audio_channels')==8:flags+=['-DFM1B_UAC_CH=8']
if san:flags+=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
objs=[str(out/(s.replace('/','_').replace('.','_')+'.o')) for s in c['o2']]
for name in ('midi_itinera_compat','midi_usb_backpressure'):
 target=out/(name+'-tests')
 subprocess.run(['clang++',*flags,*c.get('o2_cflags',[]),'-std=c++11',str(R/'tests'/f'{name}.cpp'),*objs,'-o',str(target)],check=True)
 subprocess.run([str(target)],check=True)
