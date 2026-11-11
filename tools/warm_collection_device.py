"""Exact FM-1 qualification: 64 physical tones, 20 ensembles and combined load.

No saved-slot writes. The owner patch is restored in finally; a second saved
backup is compared after this process, and the physical Master is released.
"""
from pathlib import Path
import hashlib, json, re, struct, threading, time, wave
import mido
import numpy as np
import sounddevice as sd
from creative_bench import Bench
from dreams_backup import B, KIND
from mirror_watch_20 import MirrorWatch

R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Collection').resolve()
back=R/'backups/Before-Warm-Collection-2026-11-10'
original=(back/'live-original.raw').read_bytes()
saved=dict(B.parse((back/'Saved-Before.mddb').read_bytes(),KIND)[1])
prefs=saved[2]
pre=json.loads((back/'Preflight.json').read_text())
assert hashlib.sha256(original).hexdigest()==pre['live_sha256'] and len(saved)==22
cat=json.loads((R/'presets/catalog.json').read_text())
en=re.search(r'enum \{([^}]*MD_COMMAND_COUNT[^}]*)\}',(R/'app/dsp/dreams.h').read_text(),re.S).group(1)
cmd={};n=-1
for entry in en.split(','):
    a,eq,b=entry.strip().partition('=');n=int(b,0) if eq else n+1;cmd[a.strip()]=n
report=dict(private=True,tones=[],biomes=[],stress=[],checks=[],errors=[])
d=w=stream=clock_thread=None
clock_on=threading.Event()
meter={'frames':0,'energy':0.,'overflow':0,'nonfinite':0}
clips=None

def flush():
    (O/'Device-Qualification.json').write_text(json.dumps(report,indent=2)+'\n')

def check(label,ok):
    assert ok,label
    report['checks'].append(label);flush();print('PASS',label,flush=True)

def setc(name,t,v):d.command(cmd[name],t,v)

def stats():
    x=struct.unpack('<49I',d.call(1))
    return dict(ms=x[1],xruns=x[4],overloads=x[5],nonfinite=x[7],drops=x[8],playing=x[10],
                peak_us=x[37],budget_us=x[38],quality=x[9],reductions=x[40],ui_frames=x[47],ui_peak_ms=x[48])

def capture(a,n,t,s):
    meter['frames']+=n;meter['energy']+=float(np.sum(a*a))
    meter['overflow']+=int(bool(s.input_overflow));meter['nonfinite']+=int(not np.isfinite(a).all())
    target=clips
    if target is not None:target.append(a.copy())

def clean(a,b):
    for k in ('xruns','overloads','nonfinite','drops'):assert a[k]==b[k],(k,a,b)

def evaluate(m0,elapsed,blocks):
    frames=meter['frames']-m0['frames']
    assert frames>44100*elapsed*.78 and meter['overflow']==m0['overflow'] and meter['nonfinite']==m0['nonfinite']
    x=np.concatenate(blocks);rms=float(np.sqrt(np.mean(x*x)));peak=float(np.max(abs(x)))
    assert .00003<rms and peak<.88,(rms,peak)
    return dict(frames=frames,rms=rms,peak=peak,seconds=round(elapsed,2)),x

def save_clip(name,x):
    f=O/'Device-Audio'/f'{name}.wav'
    with wave.open(str(f),'wb') as out:
        out.setparams((2,2,44100,0,'NONE','not compressed'))
        out.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes())
    return str(f.relative_to(O))

def factory(id):
    setc('MD_PLAY',0,0);setc('MD_CLOCK_SOURCE',0,0);setc('MD_SCENE',0,id);setc('MD_SCENE_CONFIRM',0,1)
    p=d.snapshot();assert struct.unpack_from('<i',p,620)[0]==id
    return p

def clock():
    next_at=time.monotonic()
    while clock_on.is_set():
        d.o.send(mido.Message('clock'));next_at+=60/(108*24)
        time.sleep(max(0,next_at-time.monotonic()))

def tone(t):
    global clips
    id=t['id'];setc('MD_PANIC',0,0)
    d.o.send(mido.Message('control_change',channel=0,control=0,value=id//128))
    d.o.send(mido.Message('program_change',channel=0,program=id%128));time.sleep(.06)
    p=d.snapshot();assert struct.unpack_from('<i',p,624)[0]==id,(id,'Program change')
    tr=struct.unpack_from('<22i',p,56)
    assert [tr[k] for k in (0,1,2,3,14,17,18,19,20,21)]==[t[k] for k in cat['tone_fields']]
    d.call(3);before=stats();m0=dict(meter);blocks=[];clips=blocks;start=time.monotonic()
    base=36 if t['model']==8 else 48 if t['model']==17 else 55
    try:
        for note,hold in ((base,1.15),(base+7,.77)):
            d.o.send(mido.Message('note_on',channel=0,note=note,velocity=92));time.sleep(hold)
            d.o.send(mido.Message('note_off',channel=0,note=note));time.sleep(.20)
    finally:clips=None
    clean(before,stats());row,x=evaluate(m0,time.monotonic()-start,blocks)
    a=stats();row.update(id=id,name=t['name'],model=t['model'],callback_peak_percent=round(a['peak_us']/a['budget_us']*100,2))
    assert row['callback_peak_percent']<85,row
    # One actual FM-1 example from every engine, plus all full Biomes below.
    if (id-452)%4==0:row['usb_capture']=save_clip('Tone-'+str(id),x)
    report['tones'].append(row);flush();print('PASS tone',id,t['name'],row['callback_peak_percent'],'%',flush=True)

def play(name,p,seconds,external=False,expect=None,record=False):
    global clips,clock_thread
    setc('MD_PLAY',0,0);setc('MD_CLOCK_SOURCE',0,0);d.upload(p)
    if external:
        setc('MD_CLOCK_SOURCE',0,1);clock_on.set();clock_thread=threading.Thread(target=clock,daemon=True);clock_thread.start()
    d.call(3);before=stats();m0=dict(meter);blocks=[];clips=blocks;start=time.monotonic();seen=[]
    try:
        setc('MD_PLAY',0,1);i=0
        while time.monotonic()-start<seconds:
            time.sleep(.3);s=stats();clean(before,s);seen.append(s)
            if expect is not None:assert s['quality']==expect,(name,s)
            if external:
                for k in range(4):
                    v=(i*877+k*173)%16384
                    d.o.send(mido.Message('control_change',channel=15,control=16+k,value=v>>7))
                    d.o.send(mido.Message('control_change',channel=15,control=48+k,value=v&127))
                d.ui(100,0,[0,1,3,11,12,19,31,38][int((time.monotonic()-start)//3)%8])
            i+=1
    finally:
        elapsed=time.monotonic()-start;clips=None;setc('MD_PLAY',0,0)
        if external:clock_on.clear();clock_thread.join(timeout=3);setc('MD_CLOCK_SOURCE',0,0)
    after=stats();clean(before,after);row,x=evaluate(m0,elapsed,blocks)
    percent=max(s['peak_us']/s['budget_us']*100 for s in seen)
    fps=(after['ui_frames']-before['ui_frames'])/elapsed
    assert percent<85 and fps>=6 and max(s['ui_peak_ms'] for s in seen)<=185,(name,percent,fps)
    row.update(name=name,callback_peak_percent=round(percent,2),ui_fps=round(fps,2),
               ui_peak_ms=max(s['ui_peak_ms'] for s in seen),quality=seen[-1]['quality'],before=before,after=after)
    if record:row['usb_capture']=save_clip(name,x)
    return row

try:
    d=Bench('Mutable Dreams FM-1')
    check('FM-1 exposes schema 11 / 516 tones / 233 Biomes',struct.unpack('<8I',d.call(7))==(1,11,2604,40,16,64,516,233))
    d.remote([1,0,0]);d.remote([2,0,0,0,0]);setc('MD_PLAY',0,0);setc('MD_CLOCK_SOURCE',0,0)
    d.upload(original);check('pre-update owner live patch imports byte-exact',d.snapshot()==original)
    for folder in ('Screens','Examples','Device-Audio'):(O/folder).mkdir(exist_ok=True)
    d.ui(103,0,1);d.ui(108,0,2)
    w=MirrorWatch('Mutable Dreams FM-1',O);check('complete mirror frame',w.full())
    audio=[i for i,v in enumerate(sd.query_devices()) if v['name']=='Mutable Dreams FM-1' and v['max_input_channels']==2]
    assert len(audio)==1,audio
    stream=sd.InputStream(device=audio[0],samplerate=44100,channels=2,dtype='float32',blocksize=1024,callback=capture)
    stream.start();d.remote([4,96,18]);time.sleep(.4)
    p=bytearray(factory(64))
    for i in range(6):
        at=56+i*88
        for off,v in ((16,650),(20,0),(44,500),(48,int(i!=0))):struct.pack_into('<i',p,at+off,v)
    d.upload(p);setc('MD_MIDI_ROUTE',0,1);setc('MD_MIDI_QUANTIZE',0,0);d.ui(101,0,0);d.ui(100,0,1)
    for t in cat['tones'][452:]:tone(t)
    check('all 64 tones speak over physical USB stereo; bank 4 recalls IDs 512–515',len(report['tones'])==64)
    # Exercise the actual K3 preset encoder at the 511/512 boundary.
    setc('MD_TONE_PRESET',0,511);d.remote([3,2,65]);time.sleep(.12)
    check('hardware preset encoder crosses 511/512',struct.unpack_from('<i',d.snapshot(),624)[0]==512)
    for id in range(213,233):
        p=factory(id);b=cat['biomes'][id];d.ui(100,0,0);time.sleep(.3)
        assert list(struct.unpack_from('<6i',p,624))==b['tones']
        (O/f'Examples/Biome-{id}.json').write_text(json.dumps(dict(format='mutable-dreams-patch',version=1,firmware='2.0-dev',name=b['name'],patch=list(p)))+'\n')
        row=play('Biome-'+str(id),p,12,record=True);row.update(id=id,biome=b['name'])
        w.screen.png(O/f'Screens/Biome-{id}.png')
        report['biomes'].append(row);flush();print('PASS Biome',id,b['name'],row['callback_peak_percent'],'%',flush=True)
    check('all 20 authored Biomes audible on the FM-1, all six tone references exact',len(report['biomes'])==20)
    setc('MD_MIDI_MACRO',0,1)
    for models,name,sec,expect in [((18,)*6,'Bow-FULL',45,1),((19,)*6,'Aero-FULL',45,1),((18,19,15,16,9,17),'Mixed-ECO',120,0)]:
        p=bytearray(factory(213));
        for off,v in ((24,1000),(40,1000),(48,850),(52,1)):struct.pack_into('<i',p,off,v)
        for i,model in enumerate(models):
            at=56+i*88
            for off,v in ((0,model),(16,540),(20,700),(24,16),(28,16),(32,0),(36,1000),(40,36+i*7),(48,0),(52,1),(56,80),(60,4),(76,1000),(80,2)):
                struct.pack_into('<i',p,at+off,v)
        row=play(name+'-LP24-FLOW-clock-FX-CC-mirror',p,sec,external=True,expect=expect)
        report['stress'].append(row);flush();print('PASS stress',name,row['callback_peak_percent'],'%',flush=True)
    report['mirror']=w.summary();report['audio']=dict(meter)
    check('no USB-audio or mirror sequence errors',meter['overflow']==meter['nonfinite']==0 and w.gaps==w.screen.bad==0)
    report['pass']=True
except BaseException as exc:
    report['errors'].append(repr(exc));raise
finally:
    clock_on.clear()
    if clock_thread:clock_thread.join(timeout=3)
    if stream:stream.stop();stream.close()
    if w:w.close()
    if d:
        try:
            d.remote([1,0,0]);d.remote([2,0,0,0,0]);setc('MD_PANIC',0,0);setc('MD_CLOCK_SOURCE',0,0)
            setc('MD_MIDI_ROUTE',0,prefs[20]);setc('MD_MIDI_MACRO',0,prefs[26]);setc('MD_MIDI_QUANTIZE',0,0)
            d.ui(103,0,struct.unpack_from('<i',prefs,4)[0]);d.ui(108,0,struct.unpack_from('<i',prefs,12)[0])
            d.upload(original);d.ui(100,0,0);d.remote([4,1,32]);report['owner_live_exact']=d.snapshot()==original;report['final']=stats()
            assert report['owner_live_exact'] and not report['final']['playing']
        except Exception as exc:report['cleanup_error']=repr(exc);report['pass']=False
        finally:d.close()
    flush()
