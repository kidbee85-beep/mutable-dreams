"""Save a qualified private checkpoint; never publishes or opens a device."""
from pathlib import Path
import json,zipfile,hashlib
from dreams_backup import B,KIND
R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Worlds').resolve();back=R/'backups/Before-Warm-Worlds-2026-11-10'
a=dict(B.parse((back/'Saved-Before-Warm-Worlds.mddb').read_bytes(),KIND)[1]);b=dict(B.parse((back/'Saved-After-Warm-Worlds.mddb').read_bytes(),KIND)[1]);assert a==b and len(a)==22
build=json.loads((O/'Build-Report.json').read_text());q=json.loads((O/'Device-Qualification.json').read_text());reg=json.loads((O/'Legacy-Stereo-Comparison.json').read_text());assert q['pass'] and q['owner_live_exact'] and not q['final']['playing'] and not q.get('cleanup_error');assert reg['bit_identical'] and reg['reference_biomes']==197
q['all_22_saved_objects_exact']=True;(O/'Device-Qualification.json').write_text(json.dumps(q,indent=2)+'\n');peak=max(c['callback_peak_percent'] for c in q['cases']);duration=sum(c['seconds'] for c in q['cases'])
text=f'''MUTABLE DREAMS 2.0 / WARM WORLDS — PRIVATE CHECKPOINT
10 November 2026. Kid Bunka. Public 1.5 and Syntherella unchanged.

INSTALLED BUILD
{build['package']}
SHA256 {build['sha256']}
SDK build {build['build_id']} / identity {build['identity']}.
Source fingerprint matches the linked image. CRC/directory/payload verified.

NEW IN THIS STEP
Two original memory-reusing voices: Willow Bow18 and Aero Flute19. Eight starting
presets each, plus16 curated sounds across existing voices. Total452 presets and
20 engines. Sixteen authored contrasting Biomes appended at internal IDs197–212
(screen198–213), bringing212 musical Biomes plus Blank. All earlier IDs and the
197 preceding world /420 tone definitions are preserved. Browse order interleaves
contrasts; the first four familiar entries stay first. New engine combinations,
registers, note densities, phrase direction, room settings and dry/kit/meditation
roles are authored separately. The two new kit worlds use3 synths +1 lo-fi kit.

Ten cached visual families on Home: Floating Cells, Medusae, Deep Roots, Currents,
Reed Beds, Orbital Seeds, Membranes, Mineral Facets, Mycelium, Lantern Pods.
They use existing paths/canvas; controls change density/size/light/motion. Home
places each distinct silhouette beside the list, surrounded by six voice markers.
The old1728-byte duplicate contour cache and its repeated geometry were removed.
Native LCD resolution remains240x240. Motion0 freezes the decorative drawing.
Living Lights is retained: function-corresponding LEDs, clear mute/lock/freeze/
recording states, OFF/DIM/FLOW and a slow event-shaped dim breath. USB mirror
shows bright/dim state but not the continuous physical PWM brightness.

FIXES
Engine-to-first-preset lookup scans the actual table, fixing the Skin fallback
that previously pointed at396 instead of404; Bow/Aero begin at420/428. Authored
percussion FX now live in factory_scene, so Journey matches direct recall. Both
new engines have pressure/breath controls, correct melodic envelope labels in
the browser, long-hold field guides, MIDI control and saved-patch support. The
browser's LOOK value now says FLOW consistently. Journey guide updated to v11.

MEASURED RESOURCES
App {build['app_bytes']}/581564 ->{build['app_slot_spare']} flash bytes spare.
Ordinary static {build['ordinary_static_ram']}/98304; reserve8192 ->
{build['ordinary_discretionary']} discretionary,1728 more than Living Lights.
DSP pool {build['dsp_pool']}/344064; separate reserve8192 ->16 discretionary.
RAM code5444/32768. No new audio buffer or per-voice memory. Neither reserve cut.
32 new tones /16 worlds /two engines /ten visual families cost13408 flash bytes
versus Living Lights. Retaining an additional8KiB application-space margin leaves
5636 bytes for future flash additions, not an entitlement to spend the DSP reserve.

VERIFICATION
197 complete12-second stereo streams compared against a fresh build of the exact
Living Lights source ZIP: bit-identical (104252400 stereo frames). A stale cached
renderer differed on190/193; rebuilding its actual checkpoint source resolved the
reference discrepancy. New24s renders cover all16 worlds; every new preset and
seven registers per new voice are finite, audible and release / Panic safely.
24000 deterministic mutation steps across20 models,6 slots and3 logical MIDI
transports passed ASan/UBSan. All213 native world pages and40 held-help contexts
passed sanitizers; long release invokes no short action. All20 actual encoder
preset-entry routes pass. All40 navigation lights and legacy LED prefs pass.
Actual-map130 interrupted-restore writes and interrupted writes in21 bank objects
pass. Actual WASM tests cover new voices/worlds, export/import, save/reboot,
MIDI learn, links, drift, Journey, focus/range controls and complete editor syntax.
Rendered browser check: new voices, pressure and melodic envelope labels visible;
master0, transport stopped, console errors empty; navigation closes audio context.

HARDWARE
All16 new Biomes,6 Bow voices,6 Aero voices and a dense6-model mix measured on the
one exact Mutable Dreams FM-1. Combined playback cases {duration:.2f}s, including
90s Bow +90s Aero +240s mixed with108BPM USB clock,14bit CC, Flow LEDs, shared FX,
long grains, changing UI pages and active USB mirror. Worst callback {peak:.2f}%
of5804us. Zero playback xruns/overloads/nonfinite/lost commands/USB audio overruns/
corrupt mirror frames or sequence gaps. Each of the three deliberately extreme
FULL requests was changed to ECO by the existing guard before timed playback
(counter1/2/3 at case start). These are ECO-admitted results, not proof that all
maximum combinations run in FULL. Full display received. Three real stereo
USB clips are under Device-Audio; native auditions separately labelled in Audio.
Original live patch restored byte-exact and all22 saved objects unchanged. Stopped,
physical master released. There were no bank writes in this qualification.

REMAINING RELEASE GATES
This is a private development checkpoint. This image has a verified post-install
restart, not a new physical power-removal test. Final2.0 install/rollback matrix,
owner listening review, physical external-TRS / separate BLE-controller coverage,
new UC25HQ80-board coverage and SMK port remain separate; no stable2.0 publication.
The earlier mirror checkpoint passed a true power-removal test and15-minute
combined stress; those results are history, not a substitute for current gates.
Read Review.html and LISTENING-NOTES.txt for the audition. No browser auto-play.
'''
(O/'DEVELOPMENT-STATUS.txt').write_text(text);(R/'docs/WARM-WORLDS-STATUS-2026-11-10.txt').write_text(text)
state={**build,'private':True,'installed':True,'qualification_in_progress':False,'saved_objects_exact':22,'owner_live_restored':True,'stopped':True,'physical_master_released':True,'device_qualified':True,'cold_start_this_build':False,'callback_peak_percent':peak,'physical_playback_test_seconds':duration,'public_1_5_changed':False,'syntherella_touched':False,'next':'Owner listening / physical cold start and final release matrix, before any public 2.0 release'}
(O/'WORKBENCH-STATE.json').write_text(json.dumps(state,indent=2)+'\n');(R/'DEVELOPMENT-IN-PROGRESS.json').write_text(json.dumps(dict(active=False,phase='Warm Worlds checkpoint qualified',output=str(O.relative_to(R.parent.parent)),installed_build=build['build_id'],pending_release_gates=state['next']),indent=2)+'\n')
files=[]
for p in sorted(R.rglob('*')):
 if not p.is_file() or p.is_symlink():continue
 rel=p.relative_to(R)
 if any(x in ('build','.git','backups','__pycache__','.DS_Store') for x in rel.parts):continue
 if 'docs/checkpoints' in str(rel):continue
 if rel.parts[:2]==('docs','development') and p.suffix in ('.wav','.mp3','.f32','.mp4'):continue
 if p.suffix in ('.elf','.o','.dis','.fwsc','.mddb','.pyc'):continue
 files.append(p)
manifest={str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files};(O/'Source-Manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
z=O/'Mutable-Dreams-2.0-Warm-Worlds-Private-Source.zip'
with zipfile.ZipFile(z,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as f:
 for p in files:f.write(p,'mutable-dreams-2.0-dev/'+str(p.relative_to(R)))
 for name in ('Source-Manifest.json','Build-Report.json','DEVELOPMENT-STATUS.txt','LISTENING-NOTES.txt','Offline-Test-Index.json'):f.write(O/name,name)
with zipfile.ZipFile(z) as f:assert f.testzip() is None
(O/'SHA256SUMS.txt').write_text('\n'.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name for p in (O/build['package'],z))+'\n')
p=R/'../../PROJEKTSTAND.txt';old=p.read_text();p.write_text(f'AKTUELLER STAND — 10. November 2026 / Mutable Dreams 2.0 Warm Worlds (privat)\nQuelle: work/mutable-dreams-2.0-dev\nÜbergabe: outputs/Mutable-Dreams-2.0-Warm-Worlds/WORKBENCH-STATE.json\nFM-1: {build["sha256"][:8]} /{build["build_id"]} geflasht und geprüft.20 Engines,452 Töne,212 Biome +Blank,10 visuelle Familien;1728B RAM zurückgewonnen. Nach Reserven2372B normal /16B DSP;13828B Flash. Hardwaremax {peak:.2f}%, keine Audio-/USB-/MIDI-Queue-Fehler. Besitzer-Live und22 Speicherobjekte identisch. Gestoppt/Master freigegeben. Hörseite: Review.html. Public1.5 /Syntherella unverändert. Weitere Release-Gates siehe Statusdatei.\n\nVORHERIGER CHECKPOINT:\n\n'+old)
print('PASS exact-source checkpoint ZIP verified,22 saved objects exact; private2.0 state recorded.')
