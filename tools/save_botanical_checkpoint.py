"""Save the qualified private Botanical Atlas checkpoint without opening a device."""
from pathlib import Path
import json,zipfile,hashlib
from dreams_backup import B,KIND
R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Headroom-Lab').resolve();T=R/'backups/Before-Headroom-2026-11-10'
b=json.loads((O/'Build-Report.json').read_text());q=json.loads((O/'Device-Qualification.json').read_text());audio=json.loads((O/'Stereo-Comparison.json').read_text());gfx=json.loads((O/'Pixel-Comparison.json').read_text())
a,z=[dict(B.parse((T/f'Saved-{stage}-Headroom.mddb').read_bytes(),KIND)[1]) for stage in ('Before','After')];assert a==z and len(a)==22
assert b['source_fingerprint_matches'] and q['pass'] and q['owner_live_exact'] and not q['final']['playing'] and not q.get('cleanup_error') and audio['bit_identical'] and gfx['pages_1_to_39_pixel_identical']
full=[c for c in q['cases'] if c['name'].startswith(('six-18-FULL','six-19-FULL'))];assert len(full)==2 and all(c['before']['quality']==c['after']['quality']==1 for c in full)
q['all_22_saved_objects_exact']=True;(O/'Device-Qualification.json').write_text(json.dumps(q,indent=2)+'\n');peak=max(c['callback_peak_percent'] for c in q['cases']);sec=sum(c['seconds'] for c in q['cases']);frame_peak=max(c['ui_peak_ms'] for c in q['cases']);fps=min(c['ui_fps'] for c in q['cases'])
text=f'''MUTABLE DREAMS 2.0 / BOTANICAL ATLAS — PRIVATE CHECKPOINT
10 November 2026. Kid Bunka. Public 1.5 and Syntherella unchanged.

INSTALLED BUILD
{b['package']} / {b['build_id']} / {b['identity']}
SHA256 {b['sha256']}
Source fingerprint and container CRC/payload verified.

ART AND CONTROLS
Replaced the circle-only Home voice markers at the owner's request. The full
72-point leaf contours are back, with coloured layers, veins, an inner contour
for the selected leaf and audio-linked growth. Root, reed and mycelium worlds
have branched arrangements; the other worlds retain the flower-like arrangement.
Existing palettes and ten world families still vary the character. Each point
uses signed10-bit relative Q4 offsets in3 bytes (1296 bytes total), instead of
1728 bytes for the old absolute-coordinate cache. Native240x240 resolution is
unchanged. The muted row preserves the sound/kit name and adds OFF. Status,
LEDs and long-hold help retain their prior meanings. No new save fields.

HEADROOM
The linked factory tone table is10848 bytes rather than19888, saving9040 bytes
losslessly. All452 tone definitions and all213 factory Biome IDs are unchanged.
The display uses eight30-row strips rather than six40-row strips. Canvas14400
bytes instead of19200 ->4800 bytes recovered in the DSP pool. System text tiles
long glyphs, including the large font in the panel screens. The other39 page
renders and the 27 system-font comparisons are pixel-identical. Plant graphics
add1296 ordinary bytes versus Warm Worlds. Two8KiB RAM reserves retained.

App {b['app_bytes']}/581564; flash free{b['app_slot_spare']} (vs13828), net+6592.
Retaining8192 flash margin leaves12228 for future application data/code.
Ordinary RAM {b['ordinary_static_ram']}/98304 ->{b['ordinary_discretionary']} after reserve.
DSP pool {b['dsp_pool']}/344064 ->{b['dsp_discretionary']} after reserve.
RAM code {b['ram_code']}/32768. No larger tape, delay, room or voice buffer.

SOUND QUALITY
A bounded FULL admission change for Willow Bow and Aero Flute permits four grains
on the tested six-voice combinations. The denser mixed case still goes to ECO.
The two default factory voices and every factory mix retain their authored values:
all213 complete12-second native stereo renders match the installed Warm Worlds
reference bit-for-bit (112719600 stereo frames). No waveform quantisation or
reduced synthesis precision. The stereo ECO/FULL audition uses the same five-voice
Ember Room ensemble, two separate USB recordings and a common playback gain.

CHECKS
ASan/UBSan all213 factory screens and40 held-help contexts; all40 navigation LEDs;
24,000 DSP mutations; all new voice/patch and exact stereo regressions. Actual-font
30-row versus single-canvas comparison covers27 alignments/widths/faces. Actual-map
recovery130 interrupted writes passed. Matching WASM browser playback, save/reload,
patch export/import and finite audio passed. Review contains native animation and
real USB screen images. See Offline-Test-Index.json and Device-Qualification.json.

DEVICE
{sec:.2f}s measured playback, sixteen factory worlds, six Bow FULL and six Aero FULL
(60s each with LP24, repeats, large grains, FX,108BPM USB clock,14bit CC and mirror),
180s dense mixed ECO, two20s direct USB auditions. Callback peak{peak:.2f}% of5804us;
slowest case-average UI{fps:.2f} frames/s; maximum measured frame{frame_peak}ms.
Zero playback xruns, overloads, nonfinite outputs, DSP queue drops, USB capture
errors, corrupt mirror frames or sequence gaps. The current live patch and all22
saved objects are byte-identical to the fresh pre-update backup. Transport stopped;
physical master released; no bank writes made. Warm Worlds rollback retained.

RELEASE STATUS
Private FM-1 development only. This image restarted successfully after install;
a separate physical power-removal test and the final release install/rollback
matrix remain. Previously qualified public1.5 and Syntherella stay untouched.
'''
(O/'DEVELOPMENT-STATUS.txt').write_text(text);(R/'docs/BOTANICAL-ATLAS-STATUS-2026-11-10.txt').write_text(text)
state={**b,'installed':True,'qualification_in_progress':False,'saved_objects_exact':22,'owner_live_restored':True,'stopped':True,'physical_master_released':True,'device_qualified':True,'cold_start_this_build':False,'callback_peak_percent':peak,'physical_playback_test_seconds':sec,'minimum_case_average_ui_fps':fps,'max_ui_frame_ms':frame_peak,'public_1_5_changed':False,'syntherella_touched':False,'next':'Owner review / physical cold start / final 2.0 install and rollback qualification'}
(O/'WORKBENCH-STATE.json').write_text(json.dumps(state,indent=2)+'\n');(R/'DEVELOPMENT-IN-PROGRESS.json').write_text(json.dumps(dict(active=False,phase='Botanical Atlas checkpoint qualified',installed_build=b['build_id'],output=str(O.relative_to(R.parent.parent)),pending_release_gates=state['next']),indent=2)+'\n')
files=[]
for p in sorted(R.rglob('*')):
 if not p.is_file() or p.is_symlink():continue
 rel=p.relative_to(R)
 if any(x in ('build','.git','backups','__pycache__','.DS_Store') for x in rel.parts):continue
 if 'docs/checkpoints' in str(rel):continue
 if rel.parts[:2]==('docs','development') and p.suffix in ('.wav','.mp3','.f32','.mp4'):continue
 if p.suffix in ('.elf','.o','.dis','.fwsc','.mddb','.pyc'):continue
 files.append(p)
m={str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files};(O/'Source-Manifest.json').write_text(json.dumps(m,indent=2)+'\n')
zip_path=O/'Mutable-Dreams-2.0-Botanical-Atlas-Private-Source.zip'
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as f:
 for p in files:f.write(p,'mutable-dreams-2.0-dev/'+str(p.relative_to(R)))
 for name in ('Source-Manifest.json','Build-Report.json','DEVELOPMENT-STATUS.txt','Offline-Test-Index.json'):f.write(O/name,name)
with zipfile.ZipFile(zip_path) as f:assert f.testzip() is None and all(hashlib.sha256(f.read('mutable-dreams-2.0-dev/'+p)).hexdigest()==h for p,h in m.items())
(O/'SHA256SUMS.txt').write_text('\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name for p in (O/b['package'],zip_path))+'\n')
p=R/'../../PROJEKTSTAND.txt';old=p.read_text();p.write_text(f'AKTUELLER STAND — 10. November 2026 / Mutable Dreams 2.0 Botanical Atlas (privat)\nQuelle: work/mutable-dreams-2.0-dev\nÜbergabe: outputs/Mutable-Dreams-2.0-Headroom-Lab/WORKBENCH-STATE.json\nFM-1: {b["sha256"][:8]} / {b["build_id"]} installiert und geprüft. Blattkonturen und Stängel zurück, stumme Klangnamen mit OFF. 1076B normal /4816B DSP nach Reserven, 20420B Flash. 452 Töne und213 Werks-Biomes erhalten. 6 Bow /6 Aero in FULL geprüft, Peak {peak:.2f}%. Alle22 Speicherobjekte und Live-Stand exakt. Gerät gestoppt, private Vorschau Review.html. Public1.5 / Syntherella unverändert.\n\nVORHERIGER CHECKPOINT:\n\n'+old)
print('PASS Botanical Atlas source archive and device checkpoint verified:',len(m),'files;',round(sec,2),'s hardware playback;',peak,'% callback peak.')
