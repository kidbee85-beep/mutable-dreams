"""Private 2.0 help/import regression on one exact FM-1. Preserve saved banks and owner live state."""
from pathlib import Path
import json, struct, time
from creative_bench import Bench

R = Path(__file__).resolve().parents[1]
OUT = (R/'../../outputs/Mutable-Dreams-2.0-Creative-Workbench').resolve()
BACK = R/'backups/Resume-2026-11-10'
HOME = (BACK/'live-initial.raw').read_bytes()
report = {'candidate_sha256':'cc7f48b2bcffc21483cfce3a91ccd5e36a201a23805ba75e873605341872389a', 'checks':[], 'cases':[]}
d = Bench('Mutable Dreams FM-1')
stored = None
saved_test = False
def state():
    raw = d.call(9)
    return struct.unpack_from('<46i',raw), struct.unpack_from('<i',raw,216)[0]
def check(name, ok):
    assert ok, name
    report['checks'].append(name)
    print('PASS',name,flush=True)
def metrics():
    w = struct.unpack('<49I',d.call(1))
    return dict(ms=w[1], xruns=w[4], overloads=w[5], nonfinite=w[7], drops=w[8],
                playing=w[10], callback_peak_us=w[37], callback_budget_us=w[38],ui_peak_ms=w[48])
try:
    check('editor v11 identity and patch length', struct.unpack('<8I',d.call(7))[:4] == (1,11,2604,40))
    d.remote([1,0,0]); d.remote([2,0,0,0,0]); d.command(16,0,0); d.command(61,0,0)
    stored = d.stored(0)
    (BACK/'saved-project-before-tests.raw').write_bytes(stored)
    d.upload(HOME)
    for page in range(34,40):
        d.ui(100,0,page); before=d.snapshot()
        d.remote([1,1,0]); time.sleep(.98)
        s,_=state(); check(f'page {page} long FX opens help',s[0]==page and s[4]==0)
        d.remote([1,0,0]); time.sleep(.08)
        s,_=state(); check(f'page {page} release has no short action',s[0]==page and d.snapshot()==before)
        d.remote([1,64,0]); time.sleep(.08); d.remote([1,0,0]); time.sleep(.08)
        check(f'page {page} dismisses help',state()[0][4]==-1)
    # A failed import must preserve the discard warning. A successful explicit import must clear it.
    d.ui(100,0,0); tempo=struct.unpack_from('<i',HOME,28)[0]
    d.command(15,0,tempo+1 if tempo<300 else tempo-1)
    try: d.bank(0,13,0,1)
    except RuntimeError as e: check('dirty bank recall is guarded',str(e)=='command 15: status 8')
    else: raise AssertionError('Dirty recall was not guarded')
    check('pending user bank recorded',state()[1]==13)
    bad=bytearray(HOME);bad[0]^=1
    try:d.upload(bad)
    except RuntimeError:pass
    else:raise AssertionError('Invalid upload accepted')
    check('invalid import preserves warning',state()[1]==13)
    d.upload(HOME)
    check('explicit live import clears obsolete warning',state()[1]==-1 and d.snapshot()==HOME)
    d.save();saved_test=True
    check('first SAVE after explicit import persists exact project',d.stored(0)==HOME)
    # Preferences use the full MDDB backup protocol, not the app's project-read command.
    # Compare all saved objects against Saved-A.mddb after this test has closed MIDI.
    report['preferences_and_banks_comparison']='Pending separate full MDDB export'
    # Stop-save timing is measured separately from uninterrupted playback.
    report['after_stopped_save']=metrics()
    demo=bytes(json.loads((OUT/'Living-Tidelands.json').read_text())['patch'])
    dense=bytearray(demo)
    struct.pack_into('<i',dense,28,180)
    for t in range(6):
        b=56+t*88
        for off,v in [(0,9),(16,380),(20,600),(24,8),(28,16),(32,0),(36,1000),(48,0),(52,1)]:
            struct.pack_into('<i',dense,b+off,v)
        dense[1292+t*132]=0
    struct.pack_into('<i',dense,40,1000);struct.pack_into('<i',dense,48,650);struct.pack_into('<i',dense,52,0)
    for j in range(4):struct.pack_into('<h',dense,2356+j*8+4,1000 if j%2==0 else -1000)
    d.remote([4,0,0]) # Keep test audio silent; no Mac output stream is opened.
    for name,patch,seconds in [('warm-living-tidelands',demo,30),('dense-six-elements-grain-links-drift',dense,60)]:
        d.command(16,0,0);d.upload(patch);d.call(3);before=metrics();d.command(16,0,1)
        start=time.monotonic();samples=[]
        while time.monotonic()-start<seconds:
            time.sleep(.4);m=metrics();samples.append(m)
            for k in ['xruns','overloads','nonfinite','drops']:assert m[k]==before[k],(name,k,before,m)
            assert m['playing']
            d.ui(100,0,34+int((time.monotonic()-start)//5)%6)
        d.command(16,0,0)
        peak=max(m['callback_peak_us']/m['callback_budget_us']*100 for m in samples)
        check(name+' uninterrupted playback',peak<85)
        report['cases'].append(dict(name=name,seconds=time.monotonic()-start,callback_peak_percent=peak,before=before,final=metrics(),samples=samples))
        (OUT/'Resume-Device-Results-2026-11-10.json').write_text(json.dumps(report,indent=2)+'\n')
    report['pass']=True
finally:
    try:
        d.command(16,0,0);d.command(61,0,0);d.remote([1,0,0]);d.remote([2,0,0,0,0])
        if saved_test and stored is not None and d.stored(0)!=stored:
            d.upload(stored);d.save()
        d.upload(HOME);d.ui(100,0,0)
        report['owner_live_restored']=d.snapshot()==HOME
        report['saved_project_unchanged']=stored is not None and d.stored(0)==stored
        d.remote([4,1,32])
    except Exception as e:report['cleanup_error']=str(e)
    d.close()
    (OUT/'Resume-Device-Results-2026-11-10.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='cases'},indent=2),flush=True)
