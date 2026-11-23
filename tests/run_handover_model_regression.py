#!/usr/bin/env python3
"""Targeted host-only HANDOVER regression; no device/target access."""
import argparse,hashlib,json,os,subprocess,tomllib
from pathlib import Path
P=argparse.ArgumentParser(description=__doc__);P.add_argument('--old',type=Path,required=True);P.add_argument('--objects',type=Path,required=True);P.add_argument('--output',type=Path,required=True);a=P.parse_args();root=Path(__file__).resolve().parents[1];out=a.output.resolve();assert 'Mobile Documents' not in str(out);out.mkdir(parents=True,exist_ok=True)
a.old=a.old.resolve();a.objects=a.objects.resolve()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def tree(p):return {str(q.relative_to(p)):sha(q) for q in p.rglob('*') if q.is_file()}
vendor=tree(root/'app/third_party')
for source in [a.old,a.objects]:assert tree(source/'app/third_party')==vendor,'Vendor source/header mismatch'
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=a.old,text=True).strip()=='1db2733328171850018a28841a458b90604a3dbc'
old_hash=sha(a.old/'app/dsp/dreams.cpp');new_hash=sha(root/'app/dsp/dreams.cpp');conf=tomllib.loads((root/'app/fm1.toml').read_text());objects=[a.objects/'app/build/san'/(s.replace('/','_').replace('.','_')+'.o') for s in conf['o2'] if s.startswith('third_party/')];assert all(p.is_file() for p in objects)
flags=['clang++','-O2','-ffp-contract=off','-DPLAT_HOST','-DTEST','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-std=c++11','-pthread','-DFM1B_UAC_CH=8','-DMD_MIX_DEVICE','-DMD_MIX_DEVICE_TEST','-DMD_ENGINE_UNION','-DMD_SOURCE_MIX_PIPELINE','-include',str(root/'tests/mix_pipeline_device_test_backend.h')]
flags+=['-I'+str(root/p) for p in ['app','base/host','base/device','base/include','base/lib','app/third_party','tests']]
sdk=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')
if sdk.exists():flags+=['-isysroot',str(sdk)]
commands=[]
for name,extra in [('old',['-DMD_REFERENCE_SOURCE="'+str(a.old/'app/dsp/dreams.cpp')+'"']),('fixed',[])]:
 cmd=[*flags,*extra,str(root/'tests/handover_model_probe.cpp'),*map(str,objects),'-o',str(out/name)];commands.append(cmd);r=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'.build.log')).write_text(r.stdout+r.stderr);r.check_returncode()
results=[];control={}
for name in ['old','fixed']:
 for target in [144,212]:
  for kind in ['model','tone-preset','program','tone-value']:
   # All publicbug triggers are checked on thread/jitter/refusal backends.
   for mode in (['thread'] if name=='old' else ['thread','jitter','refuse']):
    env=dict(os.environ,MD_MIX_MODE=mode,ASAN_OPTIONS='abort_on_error=1:detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1');cmd=[str(out/name),str(target),kind,name];r=subprocess.run(cmd,capture_output=True,text=True,env=env);label=f'{name}-{target}-{kind}-{mode}';(out/(label+'.stdout.jsonl')).write_text(r.stdout);(out/(label+'.stderr.log')).write_text(r.stderr);rows=[json.loads(x) for x in r.stdout.splitlines() if x.startswith('{')];negative=name=='old' and kind!='tone-value';mismatch=any(x.get('mismatch') is True for x in rows);ubsan='runtime error:' in r.stderr and 'SUMMARY: UndefinedBehaviorSanitizer' in r.stderr
    if negative:assert (r.returncode==0 and mismatch) or (r.returncode!=0 and ubsan),(label,r.stdout,r.stderr)
    else:
     assert r.returncode==0 and rows[-1]['event']=='safe_audio_and_stop' and not mismatch and not r.stderr,(label,r.stdout,r.stderr)
     if name=='old':control[target]=rows
     elif kind=='tone-value':assert rows==control[target],('Unrelated non-model edit changed',label)
    row=dict(name=label,returncode=r.returncode,expected_old_negative=negative,mismatch_observed=mismatch,ubsan_observed=ubsan,rows=rows);results.append(row);print(json.dumps({k:v for k,v in row.items() if k!='rows'}),flush=True)
assert old_hash==sha(a.old/'app/dsp/dreams.cpp') and new_hash==sha(root/'app/dsp/dreams.cpp')
report=dict(pass_=True,host_only=True,no_device_access=True,no_target_build=True,production_change='Only soft_apply resetcondition adds voices[i].model!=patch.track[i].model; body/Factoryrebuild unchanged',old_commit='1db2733328171850018a28841a458b90604a3dbc',old_source_sha256=old_hash,fixed_source_sha256=new_hash,vendor_files=len(vendor),vendor_objects={str(p):sha(p) for p in objects},commands=commands,results=results)
(out/'results.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS32 HANDOVER checks: oldnegativebug3publictriggers/fixedownershipfiniteaudio + unchangedMD_TONEcontrol')
