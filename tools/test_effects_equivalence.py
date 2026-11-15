#!/usr/bin/env python3
"""Compare real float mix/six-stem hashes from two separately built DSP trees.

First run tools/build_local.py midiout --sanitize in each tree. The probe
exercises all dreams and eight echo/grain/freeze/Stop transitions. Matching
hashes supplement (and do not replace) direct PCM comparisons/device tests.
"""
import argparse, json, os, subprocess, sys, tomllib
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline',type=Path)
p.add_argument('--candidate',type=Path,default=Path(__file__).resolve().parents[1])
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
probe=Path(__file__).resolve().parents[1]/'tests/effects_equivalence.cpp'
results={}
for name,tree in [('baseline',a.baseline.resolve()),('candidate',a.candidate.resolve())]:
 app=tree/'app';base=tree/'base';out=app/'build/san'
 conf=tomllib.loads((app/'fm1.toml').read_text())
 flags=['-O2','-ffp-contract=off','-DPLAT_HOST','-DTEST','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
 flags += [f'-I{d}' for d in (app,base/'host',base/'device',base/'include',base/'lib',app/'build/gen',app/'third_party')]
 assert conf.get('usb_audio_channels')==8,'Probe requires the six dry stems'
 flags+=['-DFM1B_UAC_CH=8']
 sdk=Path(os.environ.get('SDKROOT','/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk'))
 if sys.platform=='darwin' and sdk.exists():flags+=['-isysroot',str(sdk)]
 objects=[out/(s.replace('/','_').replace('.','_')+'.o') for s in conf['o2']]
 assert all(o.is_file() for o in objects),'Build sanitized DSP objects first'
 exe=a.output/(name+'-effects-probe')
 subprocess.run(['clang++',*flags,*conf.get('o2_cflags',[]),'-std=c++11',str(probe),*map(str,objects),'-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True)
 (a.output/(name+'.log')).write_text(run.stdout+run.stderr)
 run.check_returncode();rows=[]
 for line in run.stdout.splitlines():
  case,frames,digest,nonfinite,dropped=line.split(',')
  rows.append(dict(case=int(case),frames=int(frames),mix_and_stems_fnv64=digest,nonfinite=int(nonfinite),dropped=int(dropped)))
 assert len(rows)==22 and all(not v['nonfinite'] and not v['dropped'] for v in rows)
 results[name]=rows
results['matching']=results['baseline']==results['candidate']
(a.output/'Equivalence.json').write_text(json.dumps(results,indent=2)+'\n')
assert results['matching'],'Audio/stem hashes differ; inspect before accepting an optimization'
print('PASS: all 14 dreams and eight effect-transition cases match the independent reference.')
