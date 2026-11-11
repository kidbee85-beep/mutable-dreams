"""Seal the tested private Warm Collection build; keep owner data out of ZIP."""
from pathlib import Path
import hashlib, json, re, zipfile

R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Collection').resolve()
def read(name):return json.loads((O/name).read_text())
build=read('Build-Report.json');device=read('Device-Qualification.json');post=read('Postflight.json')
audit=read('Catalog-Audio-Verification.json');gain=read('Listening-Gain.json')
assert build['private'] and build['source_fingerprint_matches'] and not build['safety_reserves_reduced']
assert device['pass'] and post['installed'] and post['owner_live_exact'] and post['stopped']
assert audit['legacy_bit_identical'] and audit['legacy_browse_order_retained'] and post['saved_objects_exact']==22
assert (len(device['tones']),len(device['biomes']),len(device['stress']))==(64,20,3)
assert device['mirror']['bad_frames']==device['mirror']['sequence_gaps']==device['audio']['overflow']==0
assert gain['clips']==36
assert 'PASS 64 Warm Collection tones' in (O/'Web-Engine.log').read_text()
assert 'backup test passed' in (O/'Recovery-Map.log').read_text()
pkg=O/build['package'];assert hashlib.sha256(pkg.read_bytes()).hexdigest()==build['sha256']
links=re.findall(r'(?:src|href)="([^"]+)"',(O/'Review.html').read_text())
assert all((O/p).is_file() for p in links if not p.startswith('#'))
cases=device['tones']+device['biomes']+device['stress'];peak=max(c['callback_peak_percent'] for c in cases)
seconds=round(sum(c['seconds'] for c in cases),2)
flash=build['app_slot_spare']-build['app_slot_retained_margin']
state={**build,**post,'device_qualified':True,'qualification_in_progress':False,
       'physical_cases':len(cases),'physical_test_seconds':seconds,'callback_peak_percent':peak,
       'legacy_biomes_bit_identical':213,'legacy_presets_unchanged':452,'minimum_case_average_ui_fps':min(r['ui_fps'] for r in device['biomes']+device['stress']),
       'app_slot_discretionary':flash,'public_1_5_changed':False,'syntherella_touched':False,
       'physical_power_removal_this_build':False,'next':'Owner listening; final 2.0 release, cold boot and update/rollback coverage remain separate.'}
(O/'WORKBENCH-STATE.json').write_text(json.dumps(state,indent=2)+'\n')
report=f'''MUTABLE DREAMS 2.0 — WARM COLLECTION / PRIVATE FM-1 CHECKPOINT
10 November 2026 · Kid Bunka

{build['package']}
Build {build['build_id']} · {build['identity']} · SHA256 {build['sha256']}

64 deliberately authored tones over 16 engines and 20 contrasting six-track
Biomes. The 452 older factory tones, 213 older Biomes (including Blank),
relative wheel order, existing plant visuals and user save format remain.
The new entries occupy tone IDs 452–515 and Biome IDs 213–232, zero-based.
Actual waveform, spectrum and contour comparisons: Catalog-Audio-Verification.json.
No oscillator or signal-chain changes. 213 complete 12 s stereo legacy
renders matched exactly. The older package will not understand the new
factory IDs; use the pre-update saved backup and live patch if rolling back.

TARGET LINKER
App {build['app_bytes']} B / 581564 B, increase {build['app_bytes_added']} B.
{build['app_slot_spare']} B spare, {flash} B after 8192 B retained margin.
{build['ordinary_discretionary']} B ordinary RAM and {build['dsp_discretionary']} B DSP beyond 8192 B reserves.
No additional static RAM or DSP allocation. Patch schema 11 / 2604 B.

CHECKS
Native sanitizer: 64 tones at three notes, presets above 511, all 20 scenes,
Journey parity, full-rate audio, exact patch roundtrip and panic silence.
WASM: same shared engine, all new entries, user banks/export, finite stereo.
Recovery: actual application storage map, cut at every flash-write boundary.
Real FM-1: {len(cases)} cases, {seconds:.2f} s of checked audio, max {peak:.2f}% callback deadline.
USB stereo for every new tone and Biome; Bank Select and the physical encoder
at the 511/512 boundary. Load: six Bow FULL, six Aero FULL, mixed automatic ECO
with long grains, LP24, effects, USB clock/CC, FLOW and live screen mirroring.
No XRuns, DSP queue drops, nonfinite samples, mirror corruption or USB input overflows.
Live patch and all 22 saved objects byte-identical after test. Transport stopped.
No unrelated FM-1 or published version changed. This is a private build.

Review.html holds all 20 actual scene recordings and one tone per new family.
The review copies use one shared {gain['shared_gain_db']:+.2f} dB gain only.
Device-Audio contains raw 44.1 kHz stereo recordings. No browser audio left playing.

BUILD AGAIN
See BUILDING.md and docs/development/BUILD-ENVIRONMENT-FIX.txt. Use the same
JieLi toolchain/AC79 SDK and Blink runner; sh tools/build_device.sh --fm1-test-candidate.
Tools compile locally without publishing. This image targets FM-1 only.
The source archive excludes owner backups, intermediate binaries and public
release artifacts. Device install validates the exact package before transfer.
'''
(O/'DEVELOPMENT-STATUS.txt').write_text(report)
files=[]
for part in ('app','assets','base','presets','tests','tools','web'):
 for p in (R/part).rglob('*'):
  if p.is_symlink() or not p.is_file():continue
  rel=p.relative_to(R)
  if any(x in ('build','.git','backups','__pycache__','.DS_Store') for x in rel.parts):continue
  if p.suffix in ('.elf','.o','.dis','.fwsc','.mddb','.pyc'):continue
  files.append(p)
for name in ('BUILDING.md','README.md','LICENSE','LICENSE-MIT.txt','LICENSE.md','LICENSES.md',
             'docs/checkpoints/2026-11-10-before-palette/catalog.json',
             'docs/development/BUILD-ENVIRONMENT-FIX.txt'):
 p=R/name
 if p.is_file():files.append(p)
files=sorted(set(files));manifest={str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
(O/'Source-Manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
zip_path=O/'Mutable-Dreams-2.0-Warm-Collection-Private-Source.zip'
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as f:
 for p in files:f.write(p,'mutable-dreams-2.0-dev/'+str(p.relative_to(R)))
 for name in ('Source-Manifest.json','Build-Report.json','DEVELOPMENT-STATUS.txt','Catalog-Audio-Verification.json'):
  f.write(O/name,name)
with zipfile.ZipFile(zip_path) as f:
 assert f.testzip() is None
 assert all(hashlib.sha256(f.read('mutable-dreams-2.0-dev/'+p)).hexdigest()==v for p,v in manifest.items())
(O/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in (pkg,zip_path)))
(R/'DEVELOPMENT-IN-PROGRESS.json').write_text(json.dumps(dict(active=False,phase='Warm Collection checkpoint qualified',installed_build=build['build_id'],output=str(O.relative_to(R.parent.parent)),pending_release_gates=state['next']),indent=2)+'\n')
p=R/'../../PROJEKTSTAND.txt';old=p.read_text();title='AKTUELLER STAND — 10. November 2026 / Mutable Dreams 2.0 Warm Collection (privat)'
if not old.startswith(title):
 p.write_text(f'{title}\nQuelle: work/mutable-dreams-2.0-dev\nÜbergabe: outputs/Mutable-Dreams-2.0-Warm-Collection/WORKBENCH-STATE.json\nFM-1: {build["sha256"][:8]} / {build["build_id"]} installiert und geprüft. +64 Töne, +20 Biomes; 516/233 gesamt. Alte 213 Biomes samplegenau. {seconds:.0f}s Hardware, Peak {peak:.2f}%. {flash}B Flash, 1076B normal, 4816B DSP nach Reserven. Live und22 Speicherobjekte exakt. Gerät gestoppt. Hörseite Review.html. Public1.5 / Syntherella unverändert.\n\nVORHERIGER CHECKPOINT:\n\n'+old)
print('PASS preserved source checkpoint and checksums;',len(files),'files;',seconds,'s actual device audio; peak',peak,'%')
