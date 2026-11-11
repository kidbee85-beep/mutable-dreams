"""Install only the sealed private candidate on the exact backed-up FM-1."""
from pathlib import Path
import hashlib, json, struct
from creative_bench import Bench
from dreams_backup import B, KIND

R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Collection').resolve()
b=R/'backups/Before-Warm-Collection-2026-11-10'
pre=json.loads((b/'Preflight.json').read_text());build=json.loads((O/'Build-Report.json').read_text())
audit=json.loads((O/'Catalog-Audio-Verification.json').read_text())
assert audit['legacy_bit_identical'] and build['source_fingerprint_matches']
assert build['identity']=='FM-1_710020000' and build['private'] and not build['safety_reserves_reduced']
pkg=O/build['package'];assert hashlib.sha256(pkg.read_bytes()).hexdigest()==build['sha256']
first=B.parse((b/'Saved-Before.mddb').read_bytes(),KIND)[1]
assert first==B.parse((b/'Saved-Before-Second.mddb').read_bytes(),KIND)[1] and len(first)==22
d=Bench('Mutable Dreams FM-1')
try:
    assert list(struct.unpack('<8I',d.call(7)))==pre['capabilities']
    assert hashlib.sha256(d.snapshot()).hexdigest()==pre['live_sha256'],'Live changed after backup; take a fresh backup before installing'
    assert not d.stats()['playing'],'Device in use; leave stopped for update'
finally:d.close()
raise SystemExit(B.I.main([str(pkg),'--port','Mutable Dreams FM-1','--no-resources','--yes']))
