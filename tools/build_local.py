#!/usr/bin/env python3
"""Native and WASI builds share the exact firmware app and DSP. No browser DSP substitute."""
from pathlib import Path
import os,subprocess,sys,shutil
try:
 import tomllib
except ModuleNotFoundError:
 # The local Mac may still ship Python 3.9. This host builder only reads
 # simple scalar and list fields from fm1.toml; device packaging uses its
 # own parser and is unaffected.
 import ast
 class tomllib:
  @staticmethod
  def loads(source):
   data={}
   for line in source.splitlines():
    line=line.split('#',1)[0].strip()
    if not line or line.startswith('[') or '=' not in line:continue
    key,value=line.split('=',1)
    data[key.strip()]=ast.literal_eval(value.strip().replace('true','True').replace('false','False'))
   return data
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
if conf.get('mirror'):flags+=['-DFM1B_MIRROR=1']
if conf.get('usb_audio_channels')==8:flags+=['-DFM1B_UAC_CH=8']
if san:flags+=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
if mode=='web':flags+=['-DPLAT_WEB','-D_POSIX_C_SOURCE=200809L','-fvisibility=hidden']
objs=[]
for src in conf['o2']:
 o=out/(src.replace('/','_').replace('.','_')+'.o');subprocess.run([cxx,*flags,*conf.get('o2_cflags',[]),f'-I{A / "compat"}','-std=c++11','-fno-exceptions','-fno-rtti','-c',str(A/src),'-o',str(o)],check=True);objs.append(str(o))
if mode in ('dsp','quality','next','render','mellow','evolution','palette','kits','motion','biomes','palette20','journey','midiperformance','fullstereo','creative','creativeaudio','atlas','disconnect','percussion','stability','warmworlds','warmcollection'):
 target=out/('audio-quality-tests' if mode=='quality' else 'dsp-tests')
 test={'warmcollection':'warm_collection.cpp','warmworlds':'warm_worlds.cpp','stability':'stability.cpp','percussion':'percussion.cpp','disconnect':'midi_disconnect.cpp','atlas':'atlas.cpp','creativeaudio':'creative_audio.cpp','creative':'creative.cpp','fullstereo':'render_full_stereo.cpp','midiperformance':'midi_performance.cpp','journey':'journey.cpp','palette20':'palette_20.cpp','biomes':'render_biome_audit.cpp','motion':'motion.cpp','kits':'kits.cpp','palette':'render_palette.cpp','evolution':'evolution.cpp','mellow':'render_mellow.cpp','quality':'audio_quality_tests.cpp','next':'next_features.cpp','render':'render_development.cpp'}.get(mode,'dsp_tests.cpp')
 if mode in ('next','render','mellow','evolution','kits','motion','biomes','palette20','journey','midiperformance','fullstereo','creative','creativeaudio','atlas','disconnect','percussion','stability','warmworlds','warmcollection'):target=out/(mode+'-tests')
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
