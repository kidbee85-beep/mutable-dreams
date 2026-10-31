#!/usr/bin/env python3
"""Authoring source for the factory atlas. Deterministic, no runtime random bank.
Generated C tables live in flash; JSON supplies the browser's matching labels.
Tone recall intentionally preserves note, mix, sends, rhythm and mute.
"""
from pathlib import Path
import json
R=Path(__file__).resolve().parents[1]
fields='model tone harm decay level send hits length rotate prob note pan mute speed attack ratchet accent shape cutoff resonance filter breathe'.split()
tone_fields='model tone harm decay attack shape cutoff resonance filter breathe'.split()
names=[
'Felt Foot|Deep Pillow|Round Sub|Moss Kick|Soft Knock|Dub Heart|Clay Drum|Warm Pulse|Low Pebble|Thud Room|Rubber Step|Satin Kick|Stone Floor|Dust Punch|Long Earth|Tight Seed',
'Brush Skin|Paper Snare|Muted Rim|Sand Brush|Velvet Snap|Dry Twig|Soft Clap|Dust Snare|Thin Shell|Wood Snare|Loose Skin|Grain Snap|Rain Stick|Low Rattle|Warm Wire|Short Ember',
'Silk Hat|Mist Tick|Dry Leaf|Closed Dust|Shaker Sand|Open Air|Fine Rain|Soft Metal|Leaf Shake|Glass Dust|Tiny Bell|Loose Seeds|Steam Hat|Warm Ride|Grain Rust|Short Hiss',
'Pure Water|Sine Pearl|Wool Bass|Glass Drop|Soft Tine|Rubber Bell|Warm Reed|Amber Keys|Low Orbit|Velvet FM|Copper Bell|Hollow Chime|Dream Marimba|Deep Bloom|Moon Tone|Slow Glass',
'Round Reed|Wool Pad|Satin Bass|Soft Saw|Warm Strings|Hollow Flute|Dust Organ|Muted Pluck|Slow Brass|Cedar Pad|Moss Pulse|Silk Lead|Low Current|Warm Fifth|Cloud Reed|Felt Keys',
'Wood Bowl|Warm Wire|Clay Bell|Moss String|Glass Pool|Soft Steel|Hollow Log|Deep Vessel|Copper Rain|Silk String|Small Stone|Low Gong|Paper Plate|Ice Bell|Amber Chime|Felt Mallet',
'Sine Lantern|Warm Organ|Odd Velvet|Soft Spectrum|Hollow Pipe|Amber Pad|Glass Choir|Low Partial|Silk Harmonic|Felt Organ|Slow Aurora|Moss Chord|High Mist|Bronze Bloom|Deep Spectrum|Pure Harmonic',
'Minor Cloud|Major Dawn|Suspended Air|Open Fifth|Velvet Choir|Sunlit Voices|Floating Sus|Deep Fifth|Minor Moss|Major Silk|Suspended Dew|Wide Horizon|Night Voices|Warm Meadow|Slow Halo|Low Shelter']
# Individual contour tables, rather than 128 copies with arbitrary names.
tones=[]
t=[55,90,140,205,280,340,410,480,100,170,260,350,460,560,70,220]
h=[60,150,280,420,560,700,840,950,220,370,510,640,780,900,100,320]
d=[250,700,420,180,550,820,330,620,920,480,200,760,380,600,1000,120]
a=[0,0,0,0,100,0,160,0,450,300,0,0,80,650,800,350]
for model,row in enumerate(names):
 for j,name in enumerate(row.split('|')):
  attack=a[j] if model in [3,4,6,7] else 0
  shape=([500,500,490,510,485,520,500,505,495,500,530,470,510,500,500,515][j] if model==3 else 750 if model==6 else 80+j*34 if model==4 else 90+j*31)
  tone=t[j] if model!=7 else 100+j*28
  harm=h[j] if model!=7 else [0,300,600,900][j%4]
  cutoff=([660,620,570,720,690,740,680,710,550,640,760,630,700,590,610,730][j])
  if model==2:cutoff=min(950,cutoff+170)
  if model==0:cutoff-=50
  tones.append(dict(id=len(tones),name=name,model=model,tone=tone,harm=harm,decay=d[j],attack=attack,shape=shape,cutoff=cutoff,resonance=35+(j%5)*45,filter=3 if j in [3,7,12,15] else 2 if j in [2,8,9,13] else 1,breathe=(180+(j%4)*80) if attack else (70 if j%3==0 else 0)))
# Keep pitched presets on musical oscillator-ratio plateaus. VA's original
# HARMONICS control spans continuously between intervals; arbitrary values
# can detune the second oscillator by several non-scale semitones.
for x in tones:
 j=x['id']%16;m=x['model']
 if m==3:x.update(harm=[234,234,477,649,477,750,234,649,477,234,750,649,477,234,750,649][j],shape=500,tone=min(x['tone'],380))
 if m==4:x.update(harm=[500,500,500,622,744,500,622,500,744,500,622,500,500,622,744,500][j])
 if m==6:x.update(tone=[0,25,45,60,20,35,75,0,50,15,40,65,85,55,5,0][j],shape=800)
 if m==5:x.update(decay=min(x['decay'],650))
 x['resonance']=0 if m>=3 else min(x['resonance'],70)
 if m==5:x['harm']=[260,280,310,270,300,340,265,285,330,275,260,320,295,350,315,270][j]
 if m in [3,4,6,7]:x['cutoff']=min(930,x['cutoff']+130)
 if m==7:x['shape']=min(x['shape'],220)
family_names=['Mist & Atmosphere','Drone & Depth','Resonance & Wood','Dub & Techno','Broken Pulses','Organic Cycles','Rhythm Laboratory','Light & Melody']
family_short=['MIST','DRONE','RESONANCE','TECHNO','BROKEN','ORGANIC','PERCUSSION','MELODY']
biome_names=[
'Velvet Mist|Pearl Lagoon|Amber Fog|Soft Canopy|Silver Marsh|Cloud Atlas|Still Water|Moon Meadow',
'Deep Tundra|Low Horizon|Slow Monolith|Submerged Sun|Midnight Soil|Ancient Air|Blue Cavern|Distant Shelter',
'Resonant Field|Woodland Bells|Clay Basin|Copper Grove|Glass River|Moss Resonance|Hollow Valley|Stone Chimes',
'Soft Machines|Dub Estuary|Velvet Circuit|Warm Current|Basalt Steps|Night Transit|Copper Motor|Soft Pressure',
'Broken Fern|Odd Islands|Loose Gravity|Skipping Stones|Fracture Bloom|Tilted Rain|Uneven Tide|Polyrhythm Bay',
'Root Network|Seed Dispersal|Rainforest Dew|Coral Pulse|Mycelium Drift|Branching Paths|Tidal Roots|Living Sand',
'Six Kicks|Six Brushes|Six Leaves|Clay Ensemble|Skin And Stone|Dust Ritual|Low Drum Circle|Wooden Clocks',
'Sunlit Path|Tiny Constellation|Amber Arpeggio|Open Clearing|Glass Orchard|Quiet Lanterns|Pastel Stream|Morning Orbit']
# Authored engine arrangements by musical family. Each variation changes chord,
# articulation, rhythmic topology and tape behaviour, not just a random seed.
arrangements=[
[7,6,5,4,3,7],[7,4,6,7,3,5],[5,5,3,6,5,4],[0,2,1,3,5,0],
[0,1,2,5,3,4],[5,7,2,6,3,5],[0,0,0,0,0,0],[3,6,7,5,4,3]]
roots=[0,2,7,5,9,3,10,0]; scales=[1,3,4,2,5,7,1,3]
biomes=[]
for fam,row in enumerate(biome_names):
 for j,name in enumerate(row.split('|')):
  tempo=([76,64,82,58,72,88,60,68][j] if fam==0 else [48,56,42,64,52,60,46,70][j] if fam==1 else [84,92,72,104,66,88,110,78][j] if fam==2 else [126,118,130,122,136,128,114,140][j] if fam==3 else [112,138,96,124,146,108,132,116][j] if fam==4 else [78,96,86,104,70,92,118,82][j] if fam==5 else [110,96,132,108,124,140,92,120][j] if fam==6 else [92,108,84,116,100,76,124,96][j])
  b=dict(name=name,family=fam,tempo=tempo,root=roots[j],scale=scales[j],seed=0xD4EA125+fam*7919+j*101,macro=[500,250+j*65,420+(j%4)*35,750 if fam==1 else 650 if fam==0 else 500 if fam in [2,5,7] else 200+(j%4)*65],locked=1,freeze=0,grain_size=450+(j%5)*100,grain_pos=150+j*90,feedback=300+(j%5)*75,quality=0,transpose=0,swing=(j%4)*100 if fam in [3,4,6] else 0,grain_pitch=[0,0,12,-12,7,0,-5,12][j],reverse=int(j in [3,6] and fam!=3),duck_source=0 if fam in [3,4] else -1,duck_amount=450 if fam in [3,4] else 0,duck_release=350,tracks=[],tones=[])
  for i,model in enumerate(arrangements[fam]):
   if fam==6:model=([0]*6 if j==0 else [1]*6 if j==1 else [2]*6 if j==2 else [0,1,5,2,5,0] if j==3 else [0,1,2,5,1,5]) [i]
   if fam==0 and j==0:ix=[116,96,80,65,48,112][i]
   else:ix=model*16+(j*3+i*5+fam)%16
   tone=tones[ix];track=dict.fromkeys(fields,0);track.update({k:tone[k] for k in tone_fields})
   track.update(level=500 if fam not in [3,4,6] else 580,send=700 if fam in [0,1] else 0 if fam in [3,4] and i==0 else 450,note=([36,55,48,60,67,36][i] if fam in [3,4,6] else [36,43,48,55,60,67][i] if fam==1 else [48,55,60,63,67,72][i]),pan=200+i*120,prob=1000 if i==0 and fam in [3,4] else 720+(j%4)*70,length=16 if fam in [3,6,7] else [13,16,15,11,17,19][(i+j)%6],speed=1 if fam in [3,4,6,7] else 4+(i+j)%4 if fam==1 else 2+(i+j)%3,ratchet=2 if fam==4 and (i+j)%3==0 else 1,accent=350 if fam in [3,4,6] else 120)
   track['hits']=([4,8+j%4,2,3+j%3,3,2][i] if fam==3 else [3,5,7,4,2,5][(i+j)%6] if fam==4 else 1 if fam==1 else 2+(i+j)%4)
   track['rotate']=(i+j*2)%track['length']
   if fam in [0,1] and model in [3,4,6,7]:track.update(attack=250+j*65,decay=800+j*25,cutoff=520+(i%3)*45,breathe=200+j*60)
   if fam==6:track['note']=30+i*5+j*2
   if i==0:track.update(rotate=0,prob=1000) # Audible anchor immediately when a Biome starts.
   b['tracks'].append(track);b['tones'].append(ix if all(track[k]==tone[k] for k in tone_fields) else -1)
  biomes.append(b)
# Keep the original four start indices and sounds for existing control tests/users.
order=[0,24,48,16]+[i for i in range(64) if i not in [0,24,48,16]]
biomes=[biomes[i] for i in order]
for bid in range(4):
 b=biomes[bid];b.update(tempo=126 if bid==1 else 76,seed=0xD4EA125,root=0,scale=1,macro=[500,400,500,220 if bid==1 else 650],grain_size=650,grain_pos=350,feedback=450,swing=160 if bid==1 else 0,grain_pitch=0,reverse=0,duck_source=0 if bid==1 else -1,duck_amount=450 if bid==1 else 0)
 models=[[7,6,5,4,3,7],[0,2,1,3,5,0],[0]*6,[5]*6][bid]
 for i,model in enumerate(models):
  b['tracks'][i]=dict(zip(fields,[model,100+i*20 if bid==0 else 250+i*70,300,350 if bid==1 else 800,600 if bid==1 else 500,(0 if i==0 else 250) if bid==1 else 650,(4 if i==0 else 9 if i==1 else 2 if i==2 else 3) if bid==1 else i+2 if bid==2 else 2+i%3,16 if bid==1 else 13+i%4,i,850,(36 if i in [0,5] else [48,55,60,63,67,72][i]) if bid==1 else 33+i*3 if bid==2 else [48,55,60,63,67,72][i],200+i*120,0,1 if bid==1 else 2+i%3,250 if bid==0 and model in [3,4,6,7] else 0,1,300 if bid==1 else 0,500 if model==3 else 750 if model==6 else 150,590 if bid==0 else 850,100,1 if bid==0 else 0,220 if bid==0 else 0]))
  b['tones'][i]=-1
# Musical curation pass: one tonal centre, deliberate roles, less competing
# random motion, and a clearly audible rhythmic anchor. IDs remain stable.
for bid,b in enumerate(biomes):
 fam=b['family'];j=bid%8;root=b['root'];major=b['scale'] in [2,5];third=4 if major else 3
 b['macro']=[500,60+(j%4)*35,500,360 if fam in [0,1] else 280 if fam in [2,5,7] else 140]
 b.update(grain_pitch=12 if fam in [0,1,7] and j==4 else 0,reverse=0,feedback=180+(j%4)*35,grain_pos=180+(j%4)*70,grain_size=650+(j%3)*100,locked=1)
 if fam in [3,4,6]:b['swing']=60+(j%3)*60
 for i,tr in enumerate(b['tracks']):
  m=tr['model'];tr.update(pan=[500,340,660,220,780,500][i],ratchet=1,prob=1000 if i==0 else 900 if fam in [0,1,2,5,7] else 1000)
  if m>=3:
   tr['note']=(36 if fam==1 and m in [3,4] and i==1 else 48)+root+[0,7,12,third,19,12][i]
   if m==3:tr.update(harm=[234,477,649,234,750,477][i],shape=500,tone=min(tr['tone'],300))
   if m==4:tr.update(harm=500 if i%2 else 622,shape=min(tr['shape'],450))
   if m==5:tr.update(harm=265+(i%3)*15,tone=min(tr['tone'],420),decay=600 if fam in [2,5] else 460)
   if m==6:tr.update(tone=15+(i%3)*10,shape=800)
   if m==7:tr.update(note=48+root+(12 if i==5 else 0),harm=300 if major else 0,shape=120)
   tr.update(filter=1,cutoff=720+(i%3)*55,resonance=0,breathe=100+(j%3)*60)
  if fam in [0,1]:
   tr.update(level=[520,250,270,230,250,190][i],send=[500,350,450,400,450,450][i],length=16,hits=[1,2,2,1,3,1][i] if fam==0 else 1,rotate=[0,4,8,3,6,12][i],speed=[2,2,2,4,2,4][i] if fam==0 else [4,4,3,4,4,8][i])
   if m in [3,4,6,7]:tr.update(attack=250 if bid==0 else 300+(j%3)*100,decay=850 if fam==1 else 740)
  elif fam in [3,4]:
   tr.update(length=16,speed=1,level=[660,290,370,310,240,240][i],hits=([4,8,2,3,2,1] if fam==3 else [3,2,7,3,2,3])[i],rotate=([0,1,4,2,6,12] if fam==3 else [0,4,1,3,6,9])[i],accent=200,send=0 if i==0 else 180)
   if m==0:tr.update(note=24+root,decay=420 if i==0 else 230,tone=180,harm=100)
   if m==1:tr.update(note=48+root,decay=250,tone=300,harm=400)
   if m==2:tr.update(note=72,decay=180,tone=340,level=260)
   if m in [3,4,6,7]:tr.update(attack=0,decay=330)
  elif fam==6:
   tr.update(length=16,speed=1,level=[630,280,270,230,210,200][i],hits=[4,2,8,3,2,1][i],rotate=[0,4,1,3,6,12][i],send=70,decay=280+(i%3)*70)
   if m==0:tr.update(note=24+root+[0,12,7,12,19,0][i],tone=160+i*25,harm=100)
   if m==1:tr.update(note=48+root+i,decay=200+i*25)
   if m==2:tr.update(note=72,decay=120+i*25,level=270 if i==0 else 170)
  else:
   tr.update(level=[460,300,240,280,220,210][i],send=350,ratchet=1,hits=[2,3,1,3,2,1][i],rotate=[0,2,4,1,6,8][i])
   if fam!=5:tr.update(length=16,speed=2 if fam==2 else 1)
   if m in [3,4,6,7]:tr.update(attack=120 if fam==5 else 0,decay=420+(i%3)*110)
  # Rhythmic controls stay independently editable; the first voice anchors time.
  if i==0:tr.update(rotate=0,prob=1000)
  ix=b['tones'][i]
  if ix>=0 and not all(tr[k]==tones[ix][k] for k in tone_fields):b['tones'][i]=-1
empty=json.loads(json.dumps(biomes[0]));empty.update(name='Blank Biome',family=8,seed=0xD4EA125,macro=[500,0,500,0],feedback=0,grain_pitch=0,reverse=0)
for i,tr in enumerate(empty['tracks']):
 tr.update({k:tones[112][k] for k in tone_fields});tr.update(hits=0,length=16,rotate=0,prob=1000,send=350,attack=0,speed=1,level=550);empty['tones'][i]=-1
biomes.append(empty)
for i,b in enumerate(biomes):b['id']=i
# Additional 1.1 palettes; IDs 0-127 and all original Biomes are preserved.
bass_names=['Velvet Foundation','Deep Root','Round Step','Soft Rubber','Warm Undertow','Dub Anchor','Low Lantern','Sub Bloom','Wooden Pulse','Gentle Current','Round Motion','Night Floor','Felt Octave','Amber Bass','Slow Earth','Clear Foundation']
strike_names=['Felt Mallet','Moss Marimba','Wooden Bell','Clay Bowl','Amber Bar','Soft Glass','Muted Metal','Low Vessel','Gentle Pluck','Reed Pluck','Warm Tine','Quiet Wire','Sand Seeds','Pebble Rain','Dust Bells','Soft Chimes']
for model,names in [(8,bass_names),(9,strike_names)]:
 for j,name in enumerate(names):
  tones.append(dict(id=len(tones),name=name,model=model,tone=(80+j*25 if model==8 else 100+(j%8)*45),harm=(80+(j%5)*110 if model==8 else 80+(j%8)*95),decay=(80+(j%6)*55 if model==8 else 150+(j%6)*70),attack=0,shape=(100 if model==8 else 100 if j<8 else 500 if j<12 else 800),cutoff=(400+(j%4)*80 if model==8 else 450+(j%5)*60),resonance=0,filter=1,breathe=0))
# 1.1 Mellow collection: new IDs, existing 1.0 factory data unchanged.
for group,model in [('Mellow',10),('Velvet',8),('Hearth',4)]:
 names=['Candle','Linen','Dusk','Willow','Honey','Ember','Lagoon','Moss','Cloud','Dune','Cedar','Haze','Amber','Shelter','Wool','Moon']
 for j,name in enumerate(names):
  tones.append(dict(id=len(tones),name=group+' '+name,model=model,tone=60+(j%4)*65,harm=500 if model==4 else 80+(j%4)*70,decay=450+(j%4)*150,attack=[0,220,450,650][j%4],shape=120+(j%4)*180,cutoff=450+(j%4)*65,resonance=0,filter=1,breathe=100+(j%3)*100))
for j,name in enumerate(['Linen Evening','Honey Lagoon','Mellow Canopy','Candle Drift','Warm Shelter','Willow Haze','Amber Rest','Moonlit Wool']):
 b=json.loads(json.dumps(biomes[0]));b.update(id=len(biomes),name=name,tempo=56+j*3,root=[0,5,2,7,9,3,10,0][j],scale=4,macro=[500,0,480,240],feedback=180)
 for i,tr in enumerate(b['tracks']):
  ix=160+(j+i)%16 if i!=1 else 176+j
  tr.update({k:tones[ix][k] for k in tone_fields});tr.update(note=36+b['root']+[12,0,19,24,7,12][i],level=[460,270,220,180,200,160][i],length=16,hits=[1,2,1,1,2,1][i],rotate=[0,0,4,8,6,12][i],speed=4,prob=1000,send=250,attack=300+(i%3)*150,decay=850,resonance=0)
  b['tones'][i]=-1
 biomes.append(b)
# Satin Morph: original rounded wavetable family; preserve every earlier ID.
for j,name in enumerate(['Soft Sine','Wool Triangle','Hollow Reed','Rounded Saw','Dusky Organ','Linen Strings','Cedar Bass','Honey Pulse','Felt Keys','Slow Amber','Velour Fifth','Quiet Brass','Warm Current','Paper Flute','Deep Satin','Morning Ribbon']):
 tones.append(dict(id=len(tones),name=name,model=11,tone=700+(j%3)*100,harm=[0,333,665,1000][j%4],decay=500+(j%4)*130,attack=[0,250,450,600][j%4],shape=200+(j%4)*160,cutoff=650+(j%3)*65,resonance=0,filter=1,breathe=120))
for j,name in enumerate(['Satin River','Cedar Hearth','Velour Dawn','Soft Current']):
 b=json.loads(json.dumps(biomes[65+j]));b.update(id=len(biomes),name=name,tempo=64+j*6)
 for i,tr in enumerate(b['tracks']):
  if i!=1:
   ix=208+(j*3+i)%16;tr.update({k:tones[ix][k] for k in tone_fields});tr.update(attack=250+(i%3)*180,decay=850);b['tones'][i]=-1
 biomes.append(b)
# 1.2 curation changes factory recalls only. Imported 1.0/1.1 patches retain
# their actual parameters. In Plaits VA variant 2, low TIMBRE means narrow
# pulses, not low brightness; the warm region is around .36-.46, below sync.
for t in tones:
 if t['model']==4:
  j=t['id']%16;t.update(tone=360+(j%4)*25,shape=480+(j%4)*65,cutoff=490+(j%4)*35,filter=2,resonance=0)
for b in biomes:
 if b['id']==64:continue
 for i,tr in enumerate(b['tracks']):
  if tr['model']==4:
   tr.update(tone=375+(i%3)*25,shape=500+(i%3)*45,cutoff=500+(i%3)*35,filter=2,resonance=0,level=round(tr['level']*.72))
   if b['family'] in [0,1,7]:tr.update(attack=max(tr['attack'],300),note=min(tr['note'],67))
   b['tones'][i]=-1
# New colors are authored as complementary roles with different articulation.
for model,prefix in [(12,'Velvet Keys'),(13,'Bloom'),(14,'Breath'),(8,'Earth')]:
 for j,name in enumerate(['Hearth','Petal','Stillness','Honey','Willow','River','Linen','Amber','Shelter','Dusk','Lagoon','Cedar','Moon','Moss','Dawn','Home']):
  tones.append(dict(id=len(tones),name=prefix+' '+name,model=model,tone=180+(j%4)*100 if model==12 else 650+(j%4)*90 if model==13 else 90+(j%4)*130 if model==14 else 70+(j%4)*50,harm=(j%4)*250 if model==12 else (j%4)*333 if model==13 else [150,400,700,1000][j%4] if model==14 else 150+(j%4)*100,decay=500+(j%4)*140,attack=[0,180,480,700][j%4],shape=100+(j%4)*150,cutoff=540+(j%4)*45,resonance=0,filter=1,breathe=70+(j%4)*50))
# Sixteen distinct contours and spectra per family, rather than four repeated
# parameter recipes with different labels. Keep this deterministic and reviewed.
colour=[180,240,330,110,420,270,150,380,210,460,300,125,355,195,405,255]
contour=[420,650,850,540,720,950,480,780,600,900,500,820,680,1000,560,740]
for x in tones[224:]:
 j=(x['id']-224)%16;m=x['model']
 x.update(tone=colour[j] if m==12 else 550+colour[j]//2 if m==13 else colour[j] if m==14 else colour[j]//2,shape=80+j*37,decay=contour[j],attack=[0,120,380,650,50,480,240,720,0,320,160,550,80,780,420,200][j],cutoff=490+(j%6)*32,breathe=60+(j%5)*45)
 if m==14:x['harm']=[180,400,700,1000,250,550,850,350,650,950,120,480,780,300,600,900][j]
 if m==8:x.update(harm=100+(j%5)*135+(j//5)*20,attack=min(x['attack'],100),decay=200+(j%7)*80)
assert len({tuple(t[k] for k in tone_fields) for t in tones[224:]})==64
new_names=['Quiet Arrival','Breathing Room','Still Lake','Evening Paper','Soft Solstice','Piano in the Moss','Reed Sanctuary','Distant Tide','Velvet Observatory','Woodland Prayer','Luminous Rest','Cedar Lantern','Blue Meditation','Ember Valley','Floating Petals','Return Home']
for j,name in enumerate(new_names):
 b=json.loads(json.dumps(biomes[65+j%8]));b.update(id=len(biomes),name=name,tempo=48+(j%6)*4,scale=[4,3,5,1][j%4],macro=[500,0,480,220+(j%3)*30],grain_size=700+(j%3)*100,grain_pos=300,feedback=160,grain_pitch=0)
 order=[[13,8,12,14,10,13],[12,8,13,7,14,11],[14,8,10,12,13,7],[13,8,12,9,14,10]][j%4]
 for i,model in enumerate(order):
  ix=(224 if model==12 else 240 if model==13 else 256 if model==14 else 272 if model==8 else 160 if model==10 else 112 if model==7 else 208 if model==11 else 144)+(j*3+i)%16
  tr=b['tracks'][i];tr.update({k:tones[ix][k] for k in tone_fields})
  tr.update(note=36+b['root']+[12,0,19,12,24,7][i],level=[330,260,230,150,150,140][i],length=16,hits=[1,1,2,1,1,1][i],rotate=[0,0,6,8,4,12][i],speed=[4,4,2,4,8,4][i],prob=1000,send=[300,70,300,220,320,300][i],attack=160 if model==12 else 0 if model in [8,9] else 450+(i%3)*120,decay=650 if model==12 else 850)
  if model==14:tr.update(level=120,harm=700,cutoff=550)
  b['tones'][i]=-1
 biomes.append(b)
# Second 1.2 curation: soften the older FM-led arrangements at their source.
# Existing IDs and imported user patch values remain untouched. Each family
# keeps its rhythm/identity; the meditation scenes gain distinct supporting roles.
fm_colour=[90,150,120,235,180,260,170,210,100,245,320,290,205,135,275,220]
for j,x in enumerate(tones[48:64]):
 x.update(tone=fm_colour[j],shape=500,harm=[234,477,234,649,477,477,234,649,234,477,750,649,477,234,649,477][j],cutoff=[610,650,520,690,620,640,590,670,510,620,700,670,630,540,660,610][j],filter=1,resonance=0,attack=[80,110,45,140,90,50,160,120,180,240,60,170,90,400,220,320][j],decay=[600,720,460,650,570,520,800,700,900,880,480,760,630,960,820,920][j],breathe=100+(j%5)*45)
for b in biomes:
 if b['id']==64:continue
 fam=b['family'];j=b['id'];ambient=fam in [0,1] or j>=65
 if ambient:
  b['macro'][1]=0  # Slow authored intervals replace unrelated random jumps.
  b['macro'][2]=465+(j%4)*12
  b['grain_size']=740+(j%3)*80;b['feedback']=150+(j%4)*25
 for i,tr in enumerate(b['tracks']):
  model=tr['model']
  # A low, airy countervoice replaces the duplicate FM lead in melody scenes.
  if fam==7 and model==3 and i==5:
   ix=256+(j%16);tr.update({k:tones[ix][k] for k in tone_fields});model=14
   tr.update(harm=350,level=115,attack=550,decay=900,cutoff=560)
  if model==3:
   ix=48+(j*3+i*2)%16;tr.update({k:tones[ix][k] for k in tone_fields})
   tr.update(level=min(tr['level'],190 if ambient else 235),note=min(tr['note'],64 if ambient else 72))
   if ambient:tr.update(attack=220+(j%4)*65,decay=780+(j%3)*60,hits=1+(j%2),length=[16,19,23,17][j%4],speed=4,rotate=(i*3+j)%16,prob=860+(j%3)*45,send=320)
   elif fam in [2,5,7]:tr.update(attack=100+(j%3)*60,decay=550+(j%4)*85,hits=1+(j%3),speed=2,prob=850+(j%3)*50)
   else:tr.update(attack=35,decay=290+(j%3)*50)
  if ambient and model in [6,7,10,11,12,13,14]:
   tr.update(breathe=180+((j+i*3)%5)*55,prob=920+(i%3)*40)
   if model!=12:tr.update(attack=max(tr['attack'],380+(i%3)*90),decay=850+(j%3)*60)
   # Unequal slow cycles rather than six similarly articulated ostinatos.
   if i>0:tr.update(length=[16,19,23,17,29][(j+i)%5],rotate=(i*3+j)%16)
  if model==5 and fam in [0,1,2,5,7]:
   tr.update(tone=min(tr['tone'],300),cutoff=min(tr['cutoff'],680),level=round(tr['level']*.82),breathe=160+(j%3)*70)
  if model>=3 and fam!=6:tr['resonance']=0
  if i==0:tr.update(rotate=0,prob=1000)
  ix=b['tones'][i]
  if ix>=0 and not all(tr[k]==tones[ix][k] for k in tone_fields):b['tones'][i]=-1

# Final 1.2 voice-colour pass. Distinct body/air/attack recipes for each family;
# do not make every sound dark or change saved user parameters.
for x in tones:
 j=x['id']%16;m=x['model']
 if m==0:x.update(tone=[90,150,60,220,300,170,260,340,110,195,280,140,230,360,75,180][j],harm=[80,140,40,230,310,170,280,360,110,200,260,130,220,390,65,160][j])
 elif m==1:x.update(tone=150+(j%6)*70,harm=[200,480,80,640,320,120,700,380,150,260,580,420,800,520,340,180][j],cutoff=620+(j%4)*65)
 elif m==2:x.update(tone=410+(j%5)*75,harm=[650,850,420,750,950,550,900,300,800,700,450,850,1000,400,750,600][j],filter=1,cutoff=700+(j%4)*60,resonance=0)
 elif m==6:x.update(harm=[80,350,650,100,480,220,780,0,540,320,160,700,900,430,240,40][j],tone=[0,28,55,90,15,42,125,8,75,35,60,105,160,85,18,5][j],shape=520+(j%5)*90,cutoff=660+(j%4)*55)
 elif m==7:x.update(tone=300+(j%5)*110,shape=260+(j%4)*150,cutoff=620+(j%4)*55,breathe=220+(j%5)*70)
 elif m==8:x.update(shape=180+(j%5)*150,tone=min(650,max(x['tone'],150+(j%4)*90)),harm=180+(j%5)*130)
 elif m==9:x.update(tone=230+(j%4)*130,cutoff=620+(j%4)*55,breathe=120+(j%5)*60)
 elif m==10:x.update(tone=[260,500,740,380,620,850,460,700,320,580,800,410,660,900,520,760][j],harm=150+(j%5)*150,cutoff=600+(j%4)*60,shape=200+(j%4)*170)
 elif m==11:x.update(harm=[0,180,360,580,780,1000,120,440,650,900,250,520,720,960,80,840][j],tone=680+(j%4)*90,cutoff=620+(j%5)*45)
 elif m==12:x.update(tone=[250,420,550,180,650,380,240,590,310,720,470,210,620,280,680,400][j],shape=180+(j%5)*130,cutoff=630+(j%4)*50)
 elif m==13:x.update(harm=[0,180,370,600,800,1000,100,440,660,930,250,530,730,970,70,850][j],tone=760+(j%3)*100,cutoff=650+(j%4)*45,breathe=350+(j%5)*90)
 elif m==14:x.update(harm=[250,500,750,1000,180,650,900,380,720,950,120,550,820,320,600,880][j],tone=240+(j%6)*110,shape=220+(j%5)*140,cutoff=650+(j%4)*55)
 # FM and VA were already individually curated; the revised engine articulation
 # and pulse mapping now provide their audible new contour.
for b in biomes:
 if b['id']==64:continue
 ambient=b['family'] in [0,1] or b['id']>=65
 for i,tr in enumerate(b['tracks']):
  m=tr['model'];j=(b['id']+i*3)%16
  if m==10:tr.update(tone=350+(j%5)*110,harm=220+(j%4)*160,cutoff=max(tr['cutoff'],600))
  elif m==13:tr.update(tone=800+(j%3)*80,harm=[100,430,730,950][j%4],cutoff=max(tr['cutoff'],650),breathe=400+(j%4)*100)
  elif m==11:tr.update(harm=[100,400,700,950][j%4],cutoff=max(tr['cutoff'],610))
  elif m==12:tr.update(tone=280+(j%5)*85,shape=250+(j%4)*150,cutoff=max(tr['cutoff'],630))
  elif m==14:tr.update(tone=300+(j%4)*140,shape=350+(j%3)*150,cutoff=max(tr['cutoff'],650))
  elif m==8:tr.update(shape=300+(j%4)*150,tone=220+(j%4)*100)
  elif m==7:tr.update(tone=380+(j%4)*120,breathe=300+(j%4)*75)
  if ambient and m>=3:
   # Air comes from the voice, not a permanent dark master filter.
   tr['resonance']=0
  ix=b['tones'][i]
  if ix>=0 and not all(tr[k]==tones[ix][k] for k in tone_fields):b['tones'][i]=-1
# 1.3 additions preserve the complete published 1.2 atlas and identifiers.
import copy
warm_names=[
 'Honey Felt','Cedar Hush','Low Cotton','Ember Reed','Wool Horizon','Soft Copper','Apricot Air','Quiet Linen',
 'Velour Sub','Root Pillow','Slow Amber','Rounded Reed','Candle Bass','Cedar Low','Warm Current II','Deep Linen',
 'Worn Ivory','Evening Felt','Paper Rhodes','Muted Pearl','Cocoa Keys','Amber Tines','Sleepy Marimba','Soft Porcelain',
 'Silt Choir','Dawn Wool','Warm Solstice','Willow Air','Vapor Silk','Slow Pollen','Linen Cloud','Quiet Orchard']
for j,name in enumerate(warm_names):
 group=j//8;v=j%8;model=[10,8,12,[7,11,13,14,7,11,13,14][v]][group]
 base=next(t for t in tones if t['model']==model);x=copy.deepcopy(base)
 x.update(id=len(tones),name=name,tone=240+v*44,harm=[180,350,500,650,250,450,700,900][v],shape=160+v*65,attack=[380,10,40,650][group]+v*15,decay=[860,650,560,920][group],cutoff=[590,520,610,620][group]+v*13,resonance=0,filter=1 if group!=1 else 2,breathe=230+v*40 if group in [0,3] else 60)
 if model==7:x['harm']=[0,300,600,900][v%4]
 if model in [11,13]:x['tone']=650+v*30
 tones.append(x)
# Additional warm interpretations of the four existing physical/FM/VA engines.
recipes=[
 (3,'Felt Sine Pearl',100,234,500,150,700,640),(3,'Low Reed FM',145,477,500,420,920,610),(3,'Soft Celesta',185,649,500,30,560,670),(3,'Muted Marimba',120,234,500,15,360,620),
 (4,'Wooden Reed',230,500,140,180,720,620),(4,'Rounded Strings',310,622,280,550,950,660),(4,'Hollow Warmth',190,500,90,460,880,590),(4,'Cotton Brass',350,744,350,250,820,650),
 (5,'Paper Vessel',180,265,250,0,480,590),(5,'Cedar Strings',240,280,480,0,570,620),(5,'Clay Kalimba',210,310,370,0,430,650),(5,'Low Ceramic',145,270,180,0,600,570),
 (9,'Felt Chamber',210,260,180,0,560,580),(9,'Soft Plectrum',270,285,440,0,620,620),(9,'Paper Particles',180,300,780,0,450,600),(9,'Wooden Resonance',240,320,220,0,710,640)]
for model,name,tone,harm,shape,attack,decay,cutoff in recipes:
 x=copy.deepcopy(next(t for t in tones if t['model']==model));x.update(id=len(tones),name=name,tone=tone,harm=harm,shape=shape,attack=attack,decay=decay,cutoff=cutoff,filter=1,resonance=0,breathe=160);tones.append(x)
new_names=['Honey Dusk','Linen Horizon','Cedar Sleep','Amber Stillness','Quiet Orchard','Cotton Current','Willow Shelter','Silt Meditation','Dust Afterglow','Paper Lanterns','Static Meadow','Crackle Estuary','Velour Transit','Dust In Sunlight','Wooden Atrium','Tape Constellation']
for j,name in enumerate(new_names):
 b=copy.deepcopy(biomes[77+j%8]);b.update(id=len(biomes),name=name,tempo=[62,70,54,68,76,60,72,48,82,94,66,78,86,96,72,64][j],root=[0,5,2,9,7,3,10,0,5,2,9,7,3,10,0,5][j],scale=1 if j%3 else 3,seed=0x130001+j*317,macro=[500,150+j%4*65,420,510 if j<8 else 390],grain_pitch=0,feedback=330,grain_size=640,grain_pos=400,swing=100 if j>=8 else 0)
 b['family']=0 if j<8 else 9
 for i,tr in enumerate(b['tracks']):
  # Every world has a distinct ensemble, including the new warm FM/VA/wood voices.
  ensembles=[
   [296,288,304,313,329,322], [300,318,325,307,314,291],
   [303,324,329,315,334,292], [297,321,310,317,326,295],
   [299,313,330,306,319,289], [302,327,308,320,316,293],
   [298,314,332,305,325,294], [301,319,328,312,288,323],
   [296,306,319,312,288,304], [298,329,289,313,305,324],
   [300,314,313,319,307,292], [297,325,333,316,309,320],
   [302,320,308,317,293,326], [299,327,311,318,330,295],
   [301,328,334,312,289,305], [303,315,322,318,291,329]]
  ix=ensembles[j][i];x=tones[ix];tr.update({k:x[k] for k in tone_fields})
  tr.update(level=[420,340,230,290,190,210][i],send=[180,480,280,490,500,360][i],note=[36,48,60,55,67,43][i],hits=[2,1,2,1,1,1][i],length=[16,19,16,23,29,17][i],rotate=0 if i==0 else (i*3+j)%16,prob=1000 if i<2 else 850,speed=[4,6,4,5,7,8][i],ratchet=1,accent=80,pan=[500,340,650,580,200,780][i])
  if tr['model'] in [3,5,9]:tr.update(level=min(tr['level'],230 if i else 340),hits=1+(j+i)%2,speed=3+(j+i)%3,send=340+(j%3)*80)
  if tr['model'] in [7,10,11,13,14]:tr.update(attack=max(tr['attack'],480+(j%4)*100),decay=860+(i%3)*50)
  if j>=8 and i<3:tr.update(speed=[2,2+(j%3),3+(j%2)][i],hits=[3,2,1+(j%2)][i],length=[16,16,12+(j%3)*4][i],rotate=0 if i==0 else (j+i*2)%8,level=[430,310,280][i])
  b['tones'][i]=ix if all(tr[k]==tones[ix][k] for k in tone_fields) else -1
 biomes.append(b)
assert len({tuple(t[k] for k in tone_fields) for t in tones[288:]})==48
# 1.4 atlas extension. Append only: 1.0-1.3 factory IDs and every saved
# biome_id keep their meaning. These voices use existing DSP engines and no
# additional audio buffer. Each new Biome is a deliberately different ensemble.
new_voice_recipes=[
 ('Basalt Kick',0,125,100,90,0,310,550,1,0),
 ('Paper Rim',1,260,300,180,0,220,640,1,0),
 ('Vinyl Hiss',2,610,850,420,0,210,720,1,0),
 ('Pearled FM',3,120,234,500,180,750,650,1,140),
 ('Rolled Pulse',4,390,500,510,250,760,540,2,180),
 ('Low Birch',5,220,275,220,0,580,590,1,100),
 ('Amber Partial',6,35,250,700,420,900,660,1,220),
 ('Distant Choir',7,500,300,350,600,950,650,1,350),
 ('Velour Bass',8,180,230,310,10,690,510,2,50),
 ('Wooden Vessel',9,290,310,260,0,550,620,1,90),
 ('Smoked Organ',10,430,340,400,380,920,630,1,260),
 ('Soft Tape Saw',11,700,420,390,360,850,590,2,230),
 ('Dusky Rhodes',12,390,460,340,70,640,650,1,120),
 ('Slow Bloom',13,830,600,460,650,1000,670,1,400),
 ('Air Column',14,420,650,510,550,930,690,1,390),
 ('Rubber Bassline',8,315,370,520,0,480,570,2,70),
]
for name,model,tone,harm,shape,attack,decay,cutoff,fil,breathe in new_voice_recipes:
 tones.append(dict(id=len(tones),name=name,model=model,tone=tone,harm=harm,
                   decay=decay,attack=attack,shape=shape,cutoff=cutoff,
                   resonance=0,filter=fil,breathe=breathe))
assert len({x['name'] for x in tones[336:]})==16
assert not ({x['name'] for x in tones[336:]} & {x['name'] for x in tones[:336]})

# IDs 109-156 alternate markedly different scenes: sparse habitat, machine
# rhythm, struck matter, bass motion, dusty kit, meditation, air and odd clock.
# Six variations in each family change timbre, register, rhythm and harmony.
atlas_names=[
 ['Pollen Cathedral','Tidal Chapel','Moss Eclipse','Glass Monsoon','Aerial Grove','Blue Canopy'],
 ['Basalt Motor','Night Conveyor','Copper Voltage','Velour Pressure','Midnight Traction','Soft Industry'],
 ['Quartz Footpath','Clay Planetarium','Wooden Aurora','Bell Orchard','Stone Language','Reed Observatory'],
 ['Submerged Engine','Low Lantern','Warm Transit','Deep Meridian','Rubber Current','Moss Substation'],
 ['Dust Archive','Tape Sleepwalk','Paper Breaks','Worn Cassette','Soft Static','Attic Pulse'],
 ['Slow Altitude','Breath Garden','Still Horizon','Cedar Rain','Cloud Chamber','Dawn Ritual'],
 ['Particle Estuary','Wind Atlas','Pale Currents','Salt Echo','Noise Forest','Shoreline Radio'],
 ['Clockwork Fern','Odd Tide','Shifting Cells','Broken Compass','Fractal Steps','Ebb Circuit'],
]
atlas_models=[
 [[7,8,13,10,14,11],[13,8,7,6,14,12],[10,8,7,13,11,14],[7,8,6,12,14,5],[13,8,10,7,14,11],[7,8,13,11,6,10]],
 [[0,8,1,2,4,12],[0,8,2,1,3,11],[0,8,1,2,12,5],[0,8,2,4,1,10],[0,8,1,2,11,9],[0,8,2,1,5,12]],
 [[5,9,5,9,6,14],[9,5,12,5,6,7],[5,9,6,12,9,14],[9,5,5,14,12,6],[5,9,12,6,5,7],[9,5,6,5,14,12]],
 [[8,8,4,10,12,2],[8,4,8,11,3,2],[8,8,12,5,10,14],[8,4,8,13,9,2],[8,8,3,11,14,2],[8,12,8,4,5,2]],
 [[0,1,2,8,9,10],[0,2,1,8,12,14],[0,1,2,8,5,3],[0,2,1,8,11,9],[0,1,2,8,10,7],[0,2,1,8,12,5]],
 [[10,8,7,13,11,14],[7,8,10,14,6,13],[13,8,7,10,12,14],[10,8,13,7,11,6],[7,8,14,10,13,12],[13,8,10,7,6,14]],
 [[14,2,6,5,13,7],[2,14,6,9,7,13],[14,2,5,6,13,11],[2,14,7,5,6,12],[14,2,13,9,7,6],[2,14,6,5,11,13]],
 [[0,9,2,12,5,4],[0,2,9,8,3,12],[9,0,2,5,11,4],[0,9,2,13,8,3],[0,2,9,12,5,14],[9,0,2,8,4,5]],
]
atlas_family=[0,3,2,1,9,0,5,4]
atlas_tempo=[[52,63,48,72,57,66],[128,116,134,122,140,110],
             [88,74,102,82,96,68],[104,92,118,86,108,98],
             [83,94,76,101,88,110],[42,51,46,56,48,60],
             [64,70,58,76,62,68],[113,125,107,131,119,137]]
atlas_scales=[[4,3,1,5,2,8],[1,3,9,8,1,3],[2,5,3,10,2,4],
              [1,3,8,9,1,3],[1,4,3,9,1,8],[5,4,2,3,1,5],
              [6,3,4,8,2,1],[8,9,10,11,12,3]]
atlas_hits=[[1,1,1,1,1,0],[4,3,2,7,2,1],[2,1,3,1,2,1],
            [4,3,2,1,2,0],[4,2,7,2,2,1],[1,0,1,1,0,1],
            [1,1,2,0,1,0],[3,5,7,2,4,3]]
atlas_levels=[[310,240,170,210,130,130],[630,320,240,170,180,110],
              [340,230,220,190,140,120],[460,300,230,150,160,100],
              [610,250,180,300,150,120],[290,190,150,160,110,120],
              [260,170,180,110,140,90],[520,250,180,190,160,120]]
atlas_base_notes=[[48,36,60,55,67,72],[24,36,48,72,55,67],
                  [48,55,60,67,72,84],[30,36,48,55,60,72],
                  [24,48,72,36,60,67],[48,36,55,60,67,72],
                  [60,72,55,67,48,84],[24,48,72,60,36,55]]
model_tones={m:[x['id'] for x in tones if x['model']==m] for m in range(15)}
new_model_tones={m:[x['id'] for x in tones[336:] if x['model']==m] for m in range(15)}
for variation in range(6):
 for arch in range(8):
  b=copy.deepcopy(biomes[0]);name=atlas_names[arch][variation]
  b.update(id=len(biomes),name=name,family=atlas_family[arch],
           tempo=atlas_tempo[arch][variation],root=[0,5,2,9,7,3][variation],
           scale=atlas_scales[arch][variation],seed=0x140000+variation*1019+arch*137,
           macro=[500,[20,90,80,160,130,0,70,220][arch],
                  [480,580,650,460,530,440,700,640][arch],
                  [330,100,220,170,260,240,400,160][arch]],
           grain_size=650+(variation%3)*75,grain_pos=220+variation*75,
           feedback=[180,130,170,200,250,120,230,180][arch],
           swing=[0,90,0,70,140,0,0,110][arch],
           duck_source=0 if arch in [1,4,7] else -1,
           duck_amount=310 if arch in [1,4,7] else 0)
  for i,model in enumerate(atlas_models[arch][variation]):
   ids=model_tones[model]
   ix=(new_model_tones[model][(variation+i)%len(new_model_tones[model])]
       if (variation+i)%3==0 else ids[(variation*11+arch*5+i*7)%len(ids)])
   x=tones[ix];tr=b['tracks'][i]
   tr.update({k:x[k] for k in tone_fields})
   tr.update(level=atlas_levels[arch][i],send=[220,90,280,260,330,180][i] if arch not in [1,4,7] else [0,80,60,120,160,200][i],
             hits=atlas_hits[arch][i],length=([16,19,23,29,17,31][i] if arch in [0,2,5,6] else [16,16,12,16,20,15][i] if arch==7 else 16),
             rotate=0 if i==0 else (i*3+variation*2+arch)%16,
             prob=1000 if i==0 else 760+(variation*27+i*13)%220,
             note=atlas_base_notes[arch][i]+b['root'],pan=[500,360,670,260,750,550][i],
             speed=([5,4,6,8,7,5][i] if arch in [0,5,6] else [2,3,2,4,3,5][i] if arch==2 else 1 if arch in [1,4,7] else [2,2,3,4,2,4][i]),
             ratchet=1,accent=80 if arch in [0,2,5,6] else 170)
   if model>=3 and arch in [0,2,5,6]:
    tr.update(attack=max(tr['attack'],[280,0,200,400,350,450][i]),
              decay=max(tr['decay'],650),resonance=0)
   if model==8:tr.update(note=[36,30,36,30,36,30][i]+b['root'],send=min(tr['send'],110))
   if model==4:tr.update(level=min(tr['level'],250),tone=390,shape=510,cutoff=540,resonance=0)
   if model==2:tr.update(level=min(tr['level'],210),cutoff=min(tr['cutoff'],790))
   if model==3:tr.update(level=min(tr['level'],200),harm=234,shape=500)
   # Shoreline Radio was 15 dB below neighbouring worlds on an eight-second
   # FM-1 USB capture: three voices were both sparse and slow to arrive.
   # Let the air/harmonic/tape voices establish the scene sooner, while the
   # hat and grain noise stay understated.
   if arch==6 and variation==5 and i in (1,2,4):
    tr.update(level={1:310,2:310,4:250}[i],hits=2,
              speed={1:3,2:4,4:3}[i],attack={1:170,2:250,4:120}[i])
   tr['rotate']%=tr['length']
   b['tones'][i]=ix if all(tr[k]==x[k] for k in tone_fields) else -1
  biomes.append(b)
assert len(tones)==352 and len(biomes)==157
assert len({b['name'] for b in biomes})==len(biomes)

# Curated wheel order changes navigation, never stored IDs. Alternate sonic
# families and keep the six-voice signatures distinct at every adjacent step.
def browse_score(prev,candidate,recent):
 a=biomes[prev];b=biomes[candidate]
 am=[t['model'] for t in a['tracks']];bm=[t['model'] for t in b['tracks']]
 overlap=sum(min(am.count(m),bm.count(m)) for m in set(am+bm))
 density=lambda x:sum(t['hits']/max(t['speed'],1) for t in x['tracks'])
 return (20 if a['family']!=b['family'] else -15)-3*overlap \
        +min(abs(a['tempo']-b['tempo']),60)/12 \
        +min(abs(density(a)-density(b)),12)/3 \
        -(10 if b['family'] in [biomes[k]['family'] for k in recent[-2:]] else 0)
remaining=set(range(len(biomes)))-{0,1,2,3,64}
browse_order=[0,1,2,3]
while remaining:
 # Weave the new scenes through the old atlas so a new world appears quickly.
 new_only=(len(browse_order)-4)%3==0
 candidates=[k for k in remaining if (k>=109)==new_only]
 if not candidates:candidates=list(remaining)
 chosen=max(candidates,key=lambda k:(browse_score(browse_order[-1],k,browse_order),-k))
 browse_order.append(chosen);remaining.remove(chosen)
browse_order.append(64)
assert sorted(browse_order)==list(range(len(biomes)))
assert all([t['model'] for t in biomes[a]['tracks']]!=[t['model'] for t in biomes[b]['tracks']]
           for a,b in zip(browse_order,browse_order[1:]))
catalog=dict(version='1.4 private development',families=family_names+['Your own world','Lo-Fi & Warmth'],family_short=family_short+['BLANK','LO-FI'],tone_fields=tone_fields,tones=tones,biomes=biomes,browse_order=browse_order)
(R/'presets/catalog.json').write_text(json.dumps(catalog,indent=2,ensure_ascii=False)+'\n')
lines=['/* Generated by tools/generate_presets.py; MIT. Flash-resident factory atlas. */','struct TonePreset { const char *name; int32_t '+','.join(tone_fields)+'; };','static const TonePreset tone_bank[MD_TONE_PRESETS]={']
for t in tones:lines.append('{'+json.dumps(t['name'])+','+','.join(str(t[k]) for k in tone_fields)+'},')
lines+=['};','struct FactoryPatch {\n uint32_t magic,version,seed; int32_t macro[4],tempo,locked,freeze,grain_size,grain_pos,feedback,quality;\n md_track track[MD_TRACKS];\n int32_t root,scale,transpose,swing,grain_pitch,reverse,duck_source,duck_amount,duck_release;\n int32_t biome_id,tone_preset[6]; uint32_t edited_mask;\n};\nstatic_assert(sizeof(FactoryPatch)==offsetof(md_patch,ext),"factory prefix layout");\nstruct BiomePreset { const char *name; unsigned family; FactoryPatch patch; };','static const char *biome_families[]={'+','.join(json.dumps(x) for x in catalog['family_short'])+'};','static const BiomePreset biome_bank[MD_BIOMES]={']
for b in biomes:
 head=[0x4D445231,4,b['seed']];tail=[b[k] for k in ['tempo','locked','freeze','grain_size','grain_pos','feedback','quality']];gl=[b[k] for k in ['root','scale','transpose','swing','grain_pitch','reverse','duck_source','duck_amount','duck_release']]
 p=','.join(map(str,head))+',{'+','.join(map(str,b['macro']))+'},'+','.join(map(str,tail))+',{'+','.join('{'+','.join(str(t[k]) for k in fields)+'}' for t in b['tracks'])+'},'+','.join(map(str,gl))+','+str(b['id'])+',{'+','.join(map(str,b['tones']))+'},0'
 lines.append('{'+json.dumps(b['name'])+','+str(b['family'])+',{'+p+'}},')
lines+=['};','static const uint16_t biome_browse_order[MD_BIOMES]={'+','.join(map(str,browse_order))+'};'];(R/'app/dsp/preset_bank.inc').write_text('\n'.join(lines)+'\n')
print(f'{len(biomes)} Biomes / {len(tones)} tone presets generated')
