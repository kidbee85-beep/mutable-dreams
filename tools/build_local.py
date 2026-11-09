#!/usr/bin/env python3
"""Native and WASI builds share the exact firmware app and DSP. No browser DSP substitute."""
from pathlib import Path
import os,subprocess,sys,tomllib,shutil
R=Path(__file__).resolve().parents[1];A=R/'app';B=R/'base';G=A/'build/gen';G.mkdir(parents=True,exist_ok=True)
conf=tomllib.loads((A/'fm1.toml').read_text()); mode=sys.argv[1] if len(sys.argv)>1 else 'host'
san='--sanitize' in sys.argv
W=Path(os.environ.get('WASI_SDK','/opt/wasi-sdk'))
if mode=='web' and not (W/'bin/clang').exists():raise SystemExit('Set WASI_SDK to your WASI SDK directory (tested: 34.0).')
out=A/'build'/('wasm' if mode=='web' else 'san' if san else 'native');out.mkdir(parents=True,exist_ok=True)
env={**os.environ,'X0X_FONTSET':conf.get('font_set','barlow')}
for args in ([] if "--no-gen" in sys.argv else [[B/'tools/gen_font.py',G/'felucca_font.h'],[B/'tools/gen_words.py',A/'words.tsv',A/'web/words',G/'ann_words.h']]):subprocess.run([sys.executable,*map(str,args)],check=True,env=env,stdout=subprocess.DEVNULL)
cc=str(W/'bin/clang') if mode=='web' else 'clang'; cxx=str(W/'bin/clang++') if mode=='web' else 'clang++'
inc=[f'-I{p}' for p in [A,B/'host',B/'device',B/'include',B/'lib',G,A/'third_party']]
flags=['-O2','-ffp-contract=off','-DPLAT_HOST','-DTEST','-ffunction-sections','-fdata-sections',*inc]
if mode!='web' and sys.platform=='darwin':
 sdk=os.environ.get('SDKROOT','/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')
 if Path(sdk).exists():flags+=['-isysroot',sdk]
if san:flags+=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
if mode=='web':flags+=['-DPLAT_WEB','-D_POSIX_C_SOURCE=200809L','-fvisibility=hidden']
objs=[]
for src in conf['o2']:
 o=out/(src.replace('/','_').replace('.','_')+'.o');subprocess.run([cxx,*flags,*conf.get('o2_cflags',[]),f'-I{A / "compat"}','-std=c++11','-fno-exceptions','-fno-rtti','-c',str(A/src),'-o',str(o)],check=True);objs.append(str(o))
if mode in ('dsp','quality','next','render','mellow','evolution','palette','kits','motion'):
 target=out/('audio-quality-tests' if mode=='quality' else 'dsp-tests')
 test={'motion':'motion.cpp','kits':'kits.cpp','palette':'render_palette.cpp','evolution':'evolution.cpp','mellow':'render_mellow.cpp','quality':'audio_quality_tests.cpp','next':'next_features.cpp','render':'render_development.cpp'}.get(mode,'dsp_tests.cpp')
 if mode in ('next','render','mellow','evolution','kits','motion'):target=out/(mode+'-tests')
 if mode=='palette':target=out/'render-palette'
 linked=[o for o in objs if mode!='quality' or not o.endswith('dsp_dreams_cpp.o')]
 subprocess.run([cxx,*flags,*conf.get('o2_cflags',[]),*([f'-I{A / "compat"}'] if mode=='quality' else []),'-std=c++11',str(R/'tests'/test),*linked,'-o',str(target)],check=True)
else:
 src=B/('web/emu/emu_web.c' if mode=='web' else 'host/plat_host.c');o=out/'platform.o'
 subprocess.run([cc,*flags,'-std=c99','-Wno-unknown-attributes','-c',str(src),'-o',str(o)],check=True);objs.append(str(o))
 target=out/('dreams.wasm' if mode=='web' else 'mutable-dreams-host')
 link=['-Wl,--no-entry','-Wl,--export-dynamic','-Wl,-z,stack-size=262144','-Wl,--initial-memory=4194304','-Wl,--max-memory=4194304'] if mode=='web' else (['-Wl,-dead_strip'] if sys.platform=='darwin' else ['-Wl,--gc-sections'])
 subprocess.run([cxx,*flags,*objs,*link,'-o',str(target)],check=True)
print(target)
