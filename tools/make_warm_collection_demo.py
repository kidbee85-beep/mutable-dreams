"""Edit a private demonstration from real FM-1 stereo and firmware displays."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import json, math, subprocess, sys, wave
import numpy as np
from PIL import Image, ImageDraw, ImageFont

R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Collection').resolve()
V=O/'Video';W=R.parent/'warm-collection-video';S=W/'Screens'
for p in (V,W,S):p.mkdir(exist_ok=True)
FF=Path(sys.argv[1]);assert FF.is_file()
Q=json.loads((O/'Device-Qualification.json').read_text());assert Q['pass']
C=json.loads((R/'presets/catalog.json').read_text());B=json.loads((O/'Build-Report.json').read_text())
clips={x['id']:O/x['usb_capture'] for x in Q['biomes']}
host=R/'app/build/san/mutable-dreams-host';assert host.is_file()
def simulate(script,log):
    with (W/log).open('w') as f:
        subprocess.run([str(host),str(script),str(S)],check=True,stdout=f,stderr=subprocess.STDOUT)
for name in ('journey-ui','creative-ui'):simulate(R/f'tests/{name}.txt',f'{name}.log')
moving=(213,219,224,227)
sc=['wait 3400','tap HOME']
for id in moving:
    sc += [f'scene {id}','wait 150',f'expect biome {id}','tap PLAY','wait 250']
    for f in range(144):sc.extend([f'shot Motion-{id}-{f:03}.png','wait 83'])
    sc += ['tap PLAY','wait 100']
for id,page,name in [(217,3,'Granular'),(226,17,'MIDI-Performance'),(219,11,'Button-Flow'),(213,18,'User-Banks')]:
    sc += [f'scene {id}','wait 130',f'page {page}','wait 100',f'shot {name}.png','tap HOME']
(W/'Motion.txt').write_text('\n'.join(sc)+'\n');simulate(W/'Motion.txt','Motion.log')

shots=[
 (213,'A LIVING INSTRUMENT','Mutable Dreams\n2.0','Warm wood, soft air and slow-moving colour. A new private preview for the M-VAVE FM-1.','motion'),
 (214,'THE WARM COLLECTION','More ways\nto begin','64 new tones in 16 families. 20 new Biomes, from close wooden instruments to wide, quiet rooms.','device'),
 (219,'SIX TRACKS, YOUR CHOICE','Hands, wood\nand clay','Cedar strings, damped membranes and rounded resonators. Freely assign the sound on each track.','motion'),
 (227,'NEW VOICE COLOURS','Rosin and\nbreath','Willow Bow, Aero Flute, Cedar String, Reed Organ and Skin Percussion join the atlas. 20 engines in all.','motion'),
 (226,'COMMUNITY SOUND REQUEST','Warm voltage','Soft saw colours, bass and felt keys. Apricot Night Bus takes the collection from stillness to a groove.','device'),
 (217,'ONE SHARED GRANULAR BUS','Leave room\nfor the tails','Send each track into the grain buffer. Shape position, length and pitch; blend in shared reverb and delay.','Granular'),
 (215,'JOURNEY ARRANGER','Build a set','Arrange up to eight sections. Choose a Biome for each, set its length and repeats, then play the journey.','Journey-Arrange'),
 (225,'JOURNEY ANCHORS','Keep what\nis playing','Keep chosen tracks across a transition. Change the other voices, rhythm or key while an anchor stays in the set.','Journey-Anchors'),
 (222,'LIVING LINKS','Make sounds\nmove each other','Link a source track’s activity to a destination parameter. Four routes turn a rhythm or envelope into motion.','Living-Links'),
 (216,'BIOME DRIFT','Between two\nsettings','Capture two states, then move between them by hand or let Drift evolve while you play.','Biome-Drift'),
 (218,'MIDI LEARN','Make it yours','Learn up to 16 controller assignments. Edit the target and track so your own controller feels at home.','MIDI-Learn'),
 (229,'MIDI PERFORMANCE','Fit your rig','Choose the MIDI input channel per track. Use clock, fine macro CC control and adjustable pitch-bend range.','MIDI-Performance'),
 (224,'BOTANICAL ATLAS','A world\nin motion','Leaf veins, stems and palettes follow the Biome. In FLOW, the buttons breathe with the music and show their real functions.','motion'),
 (230,'PLAY, SAVE, CAPTURE','Take it\nfurther','Play or edit in the browser. Keep named User Biomes and export backups. Use USB screen mirroring for walkthroughs.','User-Banks'),
 (228,'SHAPED BY LISTENING','Thank you','Your feedback shaped the warmer sounds and MIDI workflow. Larger live views and automatic screen-reader loading are still in progress.','device'),
 (232,'KID BUNKA / PRIVATE PREVIEW','FM-1 first.\nSMK-37 next.','The SMK-37 edition is planned separately, with its own controls and hardware checks. Thanks for listening.','device')]
rate=44100;seconds=12;overlap=.75;step=seconds-overlap;N=len(shots)
fades=round(rate*overlap);stride=round(rate*step)
mix=np.zeros((stride*(N-1)+rate*seconds,2));waves=[];theta=np.linspace(0,math.pi/2,fades)[:,None]
for i,(id,*_) in enumerate(shots):
    with wave.open(str(clips[id])) as f:
        assert (f.getnchannels(),f.getframerate(),f.getsampwidth())==(2,rate,2)
        raw=np.frombuffer(f.readframes(rate*seconds),dtype='<i2').reshape(-1,2)/32768.
    if len(raw)<rate*seconds:raw=np.pad(raw,((0,rate*seconds-len(raw)),(0,0)))
    waves.append(raw.copy())
    if i:raw[:fades]*=np.sin(theta)
    if i<N-1:raw[-fades:]*=np.cos(theta)
    mix[i*stride:i*stride+len(raw)]+=raw
mix[:round(.45*rate)]*=np.linspace(0,1,round(.45*rate))[:,None]
mix[-round(1.5*rate):]*=np.linspace(1,0,round(1.5*rate))[:,None]
gain=min(8.,.82/float(np.max(abs(mix))));mix*=gain
assert np.isfinite(mix).all() and .01<float(np.max(abs(mix)))<.83
wav=W/'Original-FM1-Stereo.wav'
with wave.open(str(wav),'wb') as f:
    f.setparams((2,2,rate,0,'NONE','not compressed'));f.writeframes(np.rint(mix*32767).astype('<i2').tobytes())

F=R/'base/assets/fonts'
def font(name,size,weight=None):
    f=ImageFont.truetype(str(F/name),size)
    if weight:f.set_variation_by_axes([weight])
    return f
nun='Nunito[wght].ttf';bar='BarlowSemiCondensed-SemiBold.ttf'
body_font=font(nun,37,570);small=font(bar,27);stamp=font(bar,24)
def wrap(text,width):
    rows=[];line=''
    for word in text.split():
        test=(line+' '+word).strip()
        if body_font.getlength(test)>width and line:rows.append(line);line=word
        else:line=test
    return '\n'.join(rows+[line])
palette=[(176,216,176),(216,188,140),(222,151,128),(158,199,222),(215,175,133),(161,203,187),(181,195,229),(218,173,141)]
xx,yy=np.meshgrid(np.arange(1920),np.arange(1080));px,py,side=1080,154,720
spot=np.exp(-(((xx-1510)/1040)**2+((yy-220)/820)**2))
for i,(id,kicker,title,body,kind) in enumerate(shots):
    accent=palette[i%len(palette)];ink=(237,241,223);muted=(169,192,178)
    bg=np.dstack([np.clip(10+(14+(c-150)*.07)*spot,0,255) for c in accent]).astype('uint8')
    im=Image.fromarray(bg,'RGB');d=ImageDraw.Draw(im)
    for j in range(7):
        line=[(900+k,1080-int(180+17*j+(.18+.025*j)*k+31*math.sin(k/121+j*.75))) for k in range(0,1021,4)]
        d.line(line,fill=(27+j,44+j,42+j),width=2)
    d.rounded_rectangle((px-23,py-23,px+side+23,py+side+23),32,fill=(12,18,20),outline=(64,88,81),width=3)
    img=O/f'Screens/Biome-{id}.png' if kind=='device' else S/f'Motion-{id}-000.png' if kind=='motion' else S/f'{kind}.png'
    with Image.open(img) as p:
        assert p.size==(240,240),(img,p.size)
        im.paste(p.convert('RGB').resize((side,side),Image.Resampling.NEAREST),(px,py))
    d.text((112,113),'MUTABLE DREAMS  /  KID BUNKA',font=stamp,fill=muted);d.line((112,165,926,165),fill=(63,87,78),width=2)
    d.text((113,217),kicker,font=small,fill=accent)
    title_font=font(nun,99,790)
    if max(title_font.getlength(s) for s in title.splitlines())>875:title_font=font(nun,86,790)
    assert max(title_font.getlength(s) for s in title.splitlines())<900,title
    d.multiline_text((106,265),title,font=title_font,fill=ink,spacing=-8)
    body=wrap(body,818);assert body.count('\n')<=3,body
    d.multiline_text((114,553),body,font=body_font,fill=muted,spacing=12)
    d.text((115,815),'ORIGINAL FM-1 AUDIO',font=stamp,fill=accent)
    d.text((115,852),C['biomes'][id]['name'],font=body_font,fill=ink)
    values=[float(np.max(abs(x))) for x in np.array_split(waves[i],113)];scale=max(values) or 1
    for j,value in enumerate(values):
        x=115+j*7;l=int(7+38*math.sqrt(value/scale));d.line((x,963-l//2,x,963+l//2),fill=accent,width=3)
    d.text((px,928),'FM-1 USB DISPLAY' if kind=='device' else '2.0 FIRMWARE DISPLAY',font=stamp,fill=muted)
    d.text((px,975),f'{i+1:02} / {N:02}     ·     PRIVATE 2.0 PREVIEW',font=stamp,fill=accent)
    im.save(W/f'Card-{i:02}.png')
    if not i:im.save(V/'Thumbnail.jpg',quality=94)
print(f'Prepared {N} scenes; music is direct device stereo; shared gain {20*math.log10(gain):+.2f} dB.',flush=True)

def encode(i):
    id,kicker,*_,kind=shots[i];duration=seconds if i==N-1 else step
    args=[str(FF),'-hide_banner','-loglevel','error','-y','-loop','1','-framerate','30','-i',str(W/f'Card-{i:02}.png')]
    end=.42 if i==N-1 else .18
    vf=f'fade=t=in:d=0.23,fade=t=out:st={duration-end}:d={end},format=yuv420p'
    if kind=='motion':
        args+=['-framerate','12','-i',str(S/f'Motion-{id}-%03d.png'),'-filter_complex',f'[1:v]scale={side}:{side}:flags=neighbor[disp];[0:v][disp]overlay={px}:{py},{vf}']
    else:args+=['-vf',vf]
    out=W/f'Segment-{i:02}.mp4'
    args+=['-t',str(duration),'-r','30','-an','-c:v','libx264','-threads','3','-preset','veryfast','-crf','17','-pix_fmt','yuv420p',str(out)]
    subprocess.run(args,check=True);print('Rendered',i+1,kicker,flush=True);return out
with ThreadPoolExecutor(3) as pool:segments=list(pool.map(encode,range(N)))
concat=W/'Segments.txt';concat.write_text(''.join(f'file {f.name}\n' for f in segments))
video=V/'Mutable-Dreams-2.0-Warm-Collection-Demo.mp4'
subprocess.run([str(FF),'-hide_banner','-loglevel','error','-y','-f','concat','-safe','0','-i',str(concat),'-i',str(wav),'-map','0:v:0','-map','1:a:0','-c:v','copy','-c:a','aac','-b:a','320k','-movflags','+faststart','-shortest',str(video)],check=True)
with (V/'Decode-Check.log').open('w') as log:
    subprocess.run([str(FF),'-hide_banner','-v','info','-i',str(video),'-map','0:v:0','-map','0:a:0','-f','null','-'],check=True,stdout=log,stderr=subprocess.STDOUT)
def tc(s):return f'{int(s)//60:02}:{int(s)%60:02}'
chapters='\n'.join(f'{tc(i*step)}  {s[1].title()}  ·  {C["biomes"][s[0]]["name"]}' for i,s in enumerate(shots))
description=[
'Mutable Dreams 2.0 — Warm Collection | M-VAVE FM-1',
'A private look at Mutable Dreams 2.0. Bowed wood, soft flutes, deep bass, hand percussion and warm voltage open up 20 new Biomes and 64 sounds. Journey, Living Links, Drift and MIDI Learn turn those sounds into something you can make your own.',
'Thanks to everyone who asked for more colour, clearer live controls and deeper MIDI. Every sound in the film is recorded straight from the FM-1. No voiceover. The separate SMK-37 edition is planned and will have its own hardware checks.',
'Created by Kid Bunka. With gratitude to Émilie Gillet / Mutable Instruments and the fm1-base and felucca-bsp communities. Independent project. This is a private preview; release timing is not announced.',
chapters]
(V/'Chapters-and-Description.txt').write_text('\n\n'.join(description)+'\n')
(V/'Video-Report.json').write_text(json.dumps(dict(private=True,firmware=B['sha256'],length_seconds=len(mix)/rate,resolution=[1920,1080],fps=30,
    clips=N,audio_source='Direct device USB stereo 44.1 kHz',same_gain_db=round(20*math.log10(gain),2),no_external_fx=True,
    peak=float(np.max(abs(mix))),biomes=[C['biomes'][s[0]]['name'] for s in shots],decoded_without_errors=True),indent=2)+'\n')
print('PASS complete film decoded:',video,round(len(mix)/rate,2),'s',flush=True)
