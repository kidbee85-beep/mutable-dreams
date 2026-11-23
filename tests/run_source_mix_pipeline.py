#!/usr/bin/env python3
"""Host-only Source+Mix audit. Reuse vendor objects only after exact tree comparison.
No target compilation; probes include dreams.cpp and link existing host objects.
"""
import argparse, hashlib, json, os, re, subprocess, time, tomllib
from pathlib import Path
P=argparse.ArgumentParser(description=__doc__)
P.add_argument('--reference',type=Path,required=True)
P.add_argument('--objects',type=Path,required=True,help='Donor tree with app/build/san host vendor objects')
P.add_argument('--output',type=Path,required=True)
P.add_argument('--atlas-probe',type=Path)
P.add_argument('--dream-probe',type=Path)
a=P.parse_args();root=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
assert 'Mobile Documents' not in str(out)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def tree_manifest(p):return {str(q.relative_to(p)):sha(q) for q in p.rglob('*') if q.is_file()}
conf=tomllib.loads((root/'app/fm1.toml').read_text())
vendor=tree_manifest(root/'app/third_party')
for t in [a.reference,a.objects]:assert tree_manifest(t/'app/third_party')==vendor,'Vendor source/header mismatch: '+str(t)
objects=[a.objects/'app/build/san'/(s.replace('/','_').replace('.','_')+'.o') for s in conf['o2'] if s.startswith('third_party/')]
assert all(p.is_file() for p in objects)
manifest={'reference':str(a.reference),'vendor_files':len(vendor),'vendor_sources_sha256':hashlib.sha256(json.dumps(vendor,sort_keys=True).encode()).hexdigest(),'objects':[{'path':str(p),'sha256':sha(p)} for p in objects]}
# Production defaults may enable both experiments. Keep old-reference/default-off
# probes explicit, and qualify Union+Source together in every candidate build.
base_cflags=[f for f in conf.get('o2_cflags',[]) if f.split('=',1)[0] not in ('-DMD_ENGINE_UNION','-DMD_SOURCE_MIX_PIPELINE','-DMD_ELEMENTS_BODY_UNION')]
candidate_options=['-DMD_ENGINE_UNION','-DMD_SOURCE_MIX_PIPELINE','-DMD_ELEMENTS_BODY_UNION']
hint_options=['-DMD_SOURCE_READY_HOST_TEST'] # Host-only stale-positive scheduling hint; no ownership shortcut
flags=['clang++','-O2','-ffp-contract=off','-DPLAT_HOST','-DTEST','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-std=c++11','-pthread','-DFM1B_UAC_CH=8',*base_cflags]
flags += ['-I'+str(root/p) for p in ['app','base/host','base/device','base/include','base/lib','app/third_party','tests']]
sdk=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')
if sdk.exists():flags+=['-isysroot',str(sdk)]
backend=root/'tests/mix_pipeline_host_backend.h'
commands=[]
def build(name,probe,extra):
 target=out/name;cmd=[*flags,*extra,str(probe),*map(str,objects),'-o',str(target)]
 commands.append(cmd);r=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'.build.log')).write_text(r.stdout+r.stderr);r.check_returncode();return target
common=['-DMD_MIX_PIPELINE','-include',str(backend)]
reference=build('reference',root/'tests/source_mix_probe.cpp',[*common,'-DMD_REFERENCE_SOURCE="'+str(a.reference/'app/dsp/dreams.cpp')+'"'])
default=build('default-off',root/'tests/source_mix_probe.cpp',common)
candidate=build('source-on',root/'tests/source_mix_probe.cpp',[*common,*candidate_options])
candidate_ready=build('source-ready-on',root/'tests/source_mix_probe.cpp',[*common,*candidate_options,*hint_options])
device=build('device-source-on',root/'tests/source_mix_probe.cpp',['-include',str(root/'tests/mix_pipeline_device_test_backend.h'),*candidate_options,'-DMD_MIX_DEVICE_TEST'])
device_ready=build('device-source-ready-on',root/'tests/source_mix_probe.cpp',['-include',str(root/'tests/mix_pipeline_device_test_backend.h'),*candidate_options,*hint_options,'-DMD_MIX_DEVICE_TEST'])
fault=build('partial-fault',root/'tests/source_mix_fault_probe.cpp',[*candidate_options,'-DMD_MIX_DEVICE_TEST'])
fault_ready=build('partial-fault-ready',root/'tests/source_mix_fault_probe.cpp',[*candidate_options,*hint_options,'-DMD_MIX_DEVICE_TEST'])
contract=build('backend-contract',root/'tests/cpu2_backend_contract.cpp',[])
ready_contract=build('source-ready-contract',root/'tests/source_ready_contract.cpp',[])
results=[]
def run(exe,name,mode='thread',rounding='n'):
 env={**os.environ,'MD_MIX_MODE':mode,'MD_ROUND':rounding,'MD_MIX_REPORT':'1','ASAN_OPTIONS':'abort_on_error=1:detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1:print_stacktrace=1'}
 start=time.monotonic();p=subprocess.Popen([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env);h=hashlib.sha256();size=0
 while True:
  x=p.stdout.read(1024*1024)
  if not x:break
  h.update(x);size+=len(x)
 log=p.stderr.read().decode();rc=p.wait();(out/(name+'.log')).write_text(log)
 row=dict(name=name,mode=mode,rounding=rounding,returncode=rc,bytes=size,sha256=h.hexdigest(),seconds=time.monotonic()-start)
 results.append(row);print(json.dumps(row),flush=True)
 assert rc==0,log[-5000:]
 return row,log
reference_hash={}
for rnd in ['n','u','d','z']:
 r,_=run(reference,'reference-'+rnd,rounding=rnd);reference_hash[rnd]=(r['bytes'],r['sha256'])
r,_=run(default,'default-off');assert (r['bytes'],r['sha256'])==reference_hash['n']
source_total=None
for hint,exe in [('false',candidate),('true',candidate_ready)]:
 for mode,rnd in [('thread','n'),('jitter','n'),('refuse','n'),('mixed','n'),('deferred','n'),('sync','n'),('thread','u'),('thread','d'),('thread','z')]:
  r,log=run(exe,'source-hint-'+hint+'-'+mode+'-'+rnd,mode,rnd);assert (r['bytes'],r['sha256'])==reference_hash[rnd],r
  m=re.search(r'SOURCE_MIX accepted=(\d+) completed=(\d+) serial=(\d+)',log);assert m,log
  ac,co,se=map(int,m.groups());assert ac==co and ((mode=='refuse' and ac==0 and se>0) or (mode!='refuse' and ac>0))
  if source_total is None:source_total=ac+se
  assert ac+se==source_total,(r,ac,se,source_total)
  if hint=='false':assert se>0 # Busy-worker CPU0 source branch was exercised.
  r['source_counts']=dict(accepted=ac,completed=co,serial=se)
for hint,exe in [('false',device),('true',device_ready)]:
 for mode in ['thread','jitter','mixed','refuse']:
  r,log=run(exe,'device-source-hint-'+hint+'-'+mode,mode);assert (r['bytes'],r['sha256'])==reference_hash['n'],r
  m=re.search(r'SOURCE_MIX accepted=(\d+) completed=(\d+) serial=(\d+)',log);assert m,log
  ac,co,se=map(int,m.groups());assert ac==co and ((mode=='refuse' and ac==0 and se>0) or (mode!='refuse' and ac>0))
  assert ac+se==source_total,(r,ac,se,source_total)
  r['source_counts']=dict(accepted=ac,completed=co,serial=se)
run(fault,'accepted-partial-source-hint-false')
run(fault_ready,'accepted-partial-source-hint-true')
for mode in ['thread','jitter','refuse','deferred','mixed']:
 run(contract,'backend-'+mode,mode)
run(ready_contract,'source-ready-contract')
longs=[('atlas',a.atlas_probe,'aba42c11870266ac4bf7c7aaf142c66a7a8c14cf8ad467684b059518023af872',1270908000,'e9311f8d2b622c2b4a65307b2e658f704cbf96f15cce103be8e0736970a3097b'),('dream',a.dream_probe,'e5e75ecc2e278294ec0ec973d6006aee12b79dd41e0bec44c010d6d2d2800798',3222341024,'c96c5888fd1cdb434a6c72c1473961c88bcaa51b84936d0f278e0891c19b008e')]
for name,p,src_digest,size,digest in longs:
 if not p:continue
 assert sha(p)==src_digest,(p,sha(p));probe=out/(name+'-probe.cpp');probe.write_text(p.read_text())
 for hint,opts in [('false',[]),('true',hint_options)]:
  exe=build(name+'-hint-'+hint,probe,[*common,*candidate_options,*opts]);r,_=run(exe,name+'-canonical-hint-'+hint);assert (r['bytes'],r['sha256'])==(size,digest),r
# Complete active-engine histories, not only the common Voice tail. The old
# baseline has no union/direct-member macro and no source-job definition.
engine_text=(root/'tests/engine_union_probe.cpp').read_text()
engine_ref=out/'engine-state-reference.cpp';engine_ref.write_text(engine_text.replace('#include "dsp/dreams.cpp"','#include "'+str(a.reference/'app/dsp/dreams.cpp')+'"'))
engine_reference=build('engine-state-reference',engine_ref,common)
r,_=run(engine_reference,'engine-state-reference');engine_expected=(676698624,'361f1ccd3aabdfb59ff4265f52b306d7f92a84fbd8629a5095ba20e2c74d7ba5');assert (r['bytes'],r['sha256'])==engine_expected,r
for hint,opts in [('false',[]),('true',hint_options)]:
 exe=build('engine-state-hint-'+hint,root/'tests/engine_union_probe.cpp',[*common,*candidate_options,*opts])
 for mode in ['thread','jitter','refuse']:
  r,_=run(exe,'engine-state-hint-'+hint+'-'+mode,mode);assert (r['bytes'],r['sha256'])==engine_expected,r
report=dict(pass_=True,host_only=True,readiness_hints=[False,True],source_math_and_order_unchanged=True,no_target_build=True,no_new_pcm_buffers=True,measured_device_speedup=False,manifest=manifest,commands=commands,results=results)
(out/'results.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS source+mix host parity/fault matrix',flush=True)
