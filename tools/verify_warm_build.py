"""Check the local, exact-source device image and leave existing reserves intact."""
from pathlib import Path
import ast, hashlib, json, re, shutil, subprocess, sys

R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Collection').resolve()
app=R/'app';build=app/'build';base=R/'base';identity='FM-1_710020000'
log=(O/'Device-Build.log').read_text()
m=re.search(r'image (\d+) B; RAM code .ram_text (\d+) B of (\d+); RAM .data\+\.bss (\d+) B of (\d+); pool (\d+) B of (\d+)',log)
assert m, 'Target linker report missing'
image,ramcode,ramcode_limit,ram,ram_limit,pool,pool_limit=map(int,m.groups())
build_id=re.search(r'^build\s+([0-9A-F]{8})\s',log,re.M)[1]
fn=next(n for n in ast.parse((base/'tools/build.py').read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='build_id')
fn.decorator_list=[]
ns=dict(APP=app,SRC=base,BSP=base/'bsp',VERSION='2.0.0-DEV',hashlib=hashlib)
exec(compile(ast.Module(body=[fn],type_ignores=[]),'build-id','exec'),ns)
assert f"{ns['build_id']():08X}"==build_id
candidate=build/'mutable-dreams-2.0.0-dev.fwsc';binary=build/'mutable-dreams.bin'
assert binary.stat().st_size==image
digest=hashlib.sha256(candidate.read_bytes()).hexdigest()
out=subprocess.check_output([sys.executable,str(R/'tools/verify_package.py'),str(candidate),str(binary),identity],text=True)
assert digest in out
(O/'Package-Verification.log').write_text(out)
slot=581564;reserve=8192
assert slot-image>=reserve and ram_limit-ram>=reserve and pool_limit-pool>=reserve and ramcode<ramcode_limit
previous=json.loads((O.parent/'Mutable-Dreams-2.0-Headroom-Lab/Build-Report.json').read_text())
assert ram==previous['ordinary_static_ram'] and pool==previous['dsp_pool']
assert hashlib.sha256((O/'Rollback-Botanical-Atlas.fwsc').read_bytes()).hexdigest()==previous['sha256']
package=f'Mutable-Dreams-2.0-Warm-Collection-{digest[:8]}.fwsc'
shutil.copy2(candidate,O/package)
report=dict(private=True,identity=identity,build_id=build_id,source_fingerprint_matches=True,
  package=package,sha256=digest,app_bytes=image,app_slot_spare=slot-image,app_slot_retained_margin=reserve,
  ordinary_static_ram=ram,ordinary_limit=ram_limit,ordinary_reserve=reserve,ordinary_discretionary=ram_limit-ram-reserve,
  dsp_pool=pool,dsp_limit=pool_limit,dsp_reserve=reserve,dsp_discretionary=pool_limit-pool-reserve,
  app_bytes_added=image-previous['app_bytes'],ram_code=ramcode,safety_reserves_reduced=False,
  project_schema=11,project_bytes=2604,models=20,tones=516,biomes=233,new_tones=64,new_biomes=20)
(O/'Build-Report.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS package and exact sources:',package,build_id)
print('Remaining flash / ordinary RAM / DSP:',slot-image,ram_limit-ram-reserve,pool_limit-pool-reserve)
