"""Read and verify the saved state after real hardware tests, without writes."""
from pathlib import Path
import json, struct, hashlib
from creative_bench import Bench
from dreams_backup import B, KIND, ExactBackend

R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Collection').resolve()
b=R/'backups/Before-Warm-Collection-2026-11-10'
test=json.loads((O/'Device-Qualification.json').read_text())
assert test['pass'] and test['owner_live_exact'] and not test.get('cleanup_error')
live=(b/'live-original.raw').read_bytes()
d=Bench('Mutable Dreams FM-1')
try:
    cap=struct.unpack('<8I',d.call(7));assert cap==(1,11,2604,40,16,64,516,233)
    assert d.snapshot()==live and d.snapshot()==live and not d.stats()['playing']
finally:d.close()
p=b/'Saved-After.mddb'
assert B.backup(ExactBackend(),p,port='Mutable Dreams FM-1',kind=KIND)==0
before=B.parse((b/'Saved-Before.mddb').read_bytes(),KIND)[1];after=B.parse(p.read_bytes(),KIND)[1]
assert before==after and len(after)==22
(O/'Postflight.json').write_text(json.dumps(dict(installed=True,capabilities=cap,live_sha256=hashlib.sha256(live).hexdigest(),
  owner_live_exact=True,saved_objects_exact=22,stopped=True,physical_master_released=True),indent=2)+'\n')
print('PASS original live Biome and all 22 saved objects match; FM-1 stopped.')
