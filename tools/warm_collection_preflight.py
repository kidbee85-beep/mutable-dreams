"""Exact-target backup for the additive warm collection. No flash or saved writes."""
from pathlib import Path
import json, struct, hashlib, shutil
from creative_bench import Bench
from dreams_backup import B, KIND, ExactBackend
R=Path(__file__).resolve().parents[1]
O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Collection').resolve()
O.mkdir(exist_ok=True)
back=R/'backups/Before-Warm-Collection-2026-11-10'
back.mkdir(exist_ok=True)
assert not (back/'Preflight.json').exists(), 'Do not overwrite an existing backup'
reference=(R/'../../outputs/Mutable-Dreams-2.0-Headroom-Lab').resolve()
for src,dst in [('presets/catalog.json','Reference-Catalog.json'),('app/build/native/fullstereo-tests','Reference-Stereo.native')]:
    if not (O/dst).exists():shutil.copy2(R/src,O/dst)
shutil.copy2(reference/'Mutable-Dreams-2.0-Botanical-Atlas-990c1534.fwsc',O/'Rollback-Botanical-Atlas.fwsc')
d=Bench('Mutable Dreams FM-1')
try:
    cap=struct.unpack('<8I',d.call(7))
    assert cap==(1,11,2604,40,16,64,452,213),cap
    p=d.snapshot();assert p==d.snapshot()
    w=struct.unpack('<49I',d.call(1))
    if (back/'live-original.raw').exists():
        prev=(back/'live-original.raw').read_bytes()
        if p!=prev:(back/'live-preliminary.raw').write_bytes(prev)
    (back/'live-original.raw').write_bytes(p)
    d.command(16,0,0)
finally:d.close()
archives=[back/'Saved-Before.mddb',back/'Saved-Before-Second.mddb']
for path in archives:assert B.backup(ExactBackend(),path,port='Mutable Dreams FM-1',kind=KIND)==0
first,second=[B.parse(a.read_bytes(),KIND)[1] for a in archives]
assert first==second and len(first)==22
report=dict(port='Mutable Dreams FM-1',capabilities=cap,
  live_sha256=hashlib.sha256(p).hexdigest(),saved_objects=len(first),
  was_playing=w[10],telemetry=list(w),two_matching_reads=True)
for dest in (back,O):(dest/'Preflight.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS exact Mutable Dreams device; current live and',len(first),'objects verified twice and backed up; transport stopped.')
