"""64 deliberately authored voices / 20 contrasting ensembles. Append-only IDs.

No new DSP allocation, changed oscillator, random preset generator or master EQ.
Each row is a sound-design decision: colour, partials, contour, breath and filter.
"""
import copy

# model / collection / (name, tone, harm, shape, attack, decay, cutoff, filter, breathe)
GROUPS=[
 (8,'Low foundations',[
  ('Velour Sub Bed',75,870,90,60,960,620,2,25),
  ('Rubberwood Bass',710,110,940,5,470,840,1,40),
  ('Hollow Earth Pulse',280,660,470,20,650,700,2,80),
  ('Warm Octave Floor',560,340,780,180,890,770,1,160)]),
 (4,'Rounded voltage',[
  ('Tobacco Pulse',110,500,190,8,520,620,2,100),
  ('Apricot Brass',440,500,560,190,730,760,1,180),
  ('Woven Fifth Pad',245,622,350,690,1000,680,2,340),
  ('Low Velvet Saw',340,500,840,45,620,590,2,65)]),
 (12,'Felt and tine',[
  ('Unlit Felt Piano',110,70,150,55,860,810,1,65),
  ('Honey Tine Tremolo',490,320,620,35,730,780,1,270),
  ('Walnut Toy Keys',750,570,250,8,390,850,3,30),
  ('Slow Felt Halo',230,820,880,540,1000,730,1,390)]),
 (10,'Soft oscillators',[
  ('Warm Sine Cradle',30,10,60,750,1000,850,1,190),
  ('Wool Triangle Pluck',870,90,130,12,550,720,1,60),
  ('Twin Lantern Pad',450,920,950,470,980,790,1,400),
  ('Mellow Thumb Bass',700,540,260,25,680,660,2,90)]),
 (11,'Satin waves',[
  ('Satin Clarinet',930,340,320,230,890,800,1,170),
  ('Copper Silk Strings',870,710,900,650,1000,740,1,380),
  ('Muted Linen Organ',680,980,100,70,780,620,2,100),
  ('Soft Saw Lantern',1000,125,540,170,640,730,1,260)]),
 (13,'Spectral colour',[
  ('Amber Formant Cloud',740,120,770,760,1000,790,1,410),
  ('Hollow Ochre Prism',910,410,230,180,740,680,2,160),
  ('Woolen Overtone Veil',620,780,950,890,1000,720,1,450),
  ('Papery Spectral Pluck',960,970,90,18,440,690,2,50)]),
 (14,'Air and shoreline',[
  ('Quiet Shore Wash',170,970,90,930,1000,650,1,360),
  ('Low Wind Lantern',95,380,210,650,990,720,1,290),
  ('Sand Through Silk',720,890,60,280,800,610,2,210),
  ('Breathing Reed Ghost',390,110,800,430,910,820,1,420)]),
 (7,'Choral rooms',[
  ('Low Candle Choir',85,0,130,880,1000,750,1,280),
  ('Sunroom Major Voices',320,300,430,500,950,820,1,360),
  ('Suspended Warmth',190,600,800,720,1000,690,2,410),
  ('Bare Fifth Shelter',460,900,90,230,860,810,1,150)]),
 (15,'Plucked wood',[
  ('Palm Muted Cedar',780,100,130,5,350,850,1,20),
  ('Round Kora Thumb',470,460,790,45,810,730,1,140),
  ('Long Oud Shadow',280,830,470,85,970,610,2,270),
  ('Hearth Dulcimer',880,290,950,15,650,790,1,90)]),
 (16,'Bellows and reeds',[
  ('Low Chapel Air',140,90,150,680,1000,660,2,250),
  ('Honey Reed Choir',690,910,760,430,960,790,1,380),
  ('Close Wooden Pipe',380,330,50,30,680,860,1,70),
  ('Wavering Bellows',510,650,980,850,1000,720,1,470)]),
 (18,'Bowed wood',[
  ('Cello Beneath Moss',180,880,270,710,1000,650,2,210),
  ('Viola By Lamplight',510,410,590,340,900,800,1,240),
  ('Soft Rosin Stroke',750,180,390,100,680,790,1,110),
  ('Long Harmonic Bow',340,620,930,890,1000,730,1,440)]),
 (19,'Blown tubes',[
  ('Low Bamboo Column',130,140,180,390,950,740,1,180),
  ('Hollow Alto Breath',390,580,630,660,1000,810,1,350),
  ('Warm Fingertip Flute',780,270,270,25,510,890,1,90),
  ('Paper Pipe Horizon',560,840,940,870,1000,720,1,430)]),
 (5,'Resonant objects',[
  ('Felt Ceramic Cup',130,220,120,0,310,700,1,30),
  ('Low Wooden Vessel',240,80,670,0,590,610,2,160),
  ('Muted Bronze Chime',380,430,890,0,710,680,1,220),
  ('Warm String Pebble',190,680,360,0,450,790,1,90)]),
 (9,'Mallets and particles',[
  ('Soft Kalimba Body',170,150,180,0,490,790,1,45),
  ('Felt Gourd Plectrum',270,510,470,0,660,710,1,110),
  ('Rain On Walnut',90,820,890,0,800,650,2,230),
  ('Round Clay Mallet',360,320,110,0,380,780,1,35)]),
 (3,'Gentle phase colour',[
  ('Velvet Fundamental FM',45,234,500,210,910,850,1,210),
  ('Warm Copper Thumb',180,477,500,30,570,720,1,65),
  ('Low Porcelain Key',290,649,500,90,760,640,2,160),
  ('Soft Prism Afterglow',390,750,500,600,1000,690,2,370)]),
 (17,'Hands and clay',[
  ('Muted Palm Bongo',170,90,130,0,260,810,1,0),
  ('Low Clay Conga',340,310,370,0,470,700,1,0),
  ('Warm Finger Udu',90,740,640,0,640,650,2,0),
  ('Brush On Ochre Skin',620,910,870,0,330,740,1,0)])]

# Names / six explicit sound references / rhythm / harmony / level / space.
# All 64 sounds occur in at least one ensemble. The last six worlds deliberately
# explore a single acoustic family with contrasting registers and articulations.
SCENES=[
 ('Understory Cello',[0,12,40,44,28,24],38,2,3,[38,62,50,69,57,81],[320,180,270,140,130,45],[1,1,1,1,1,1],[8,8,8,8,8,8],360,'A low cello and sub foundation, soft sine above it, distant air; no ostinato.'),
 ('Walnut Courtyard',[32,48,8,1,60,36],102,7,8,[55,67,74,31,55,62],[390,150,190,310,220,120],[5,2,3,3,4,1],[2,4,3,2,1,8],90,'Close muted wood, dry felt and a hand-played bongo. A nimble bass keeps the floor.'),
 ('Honey Tine Session',[9,56,16,2,61,20],86,0,4,[60,67,72,36,48,79],[330,190,150,310,170,90],[3,2,1,3,2,1],[3,4,8,2,2,8],190,'A swaying electric-piano conversation with soft FM thumb notes and a rounded conga.'),
 ('Bellows At First Light',[37,29,41,25,13,45],46,5,2,[53,65,60,77,72,84],[290,180,230,70,190,120],[1,1,1,1,1,1],[8,8,8,8,7,8],310,'Major reed, viola and soft triangle; a broad sunrise rather than six competing leads.'),
 ('Ochre Observatory',[21,49,52,26,30,3],58,3,10,[63,51,70,82,58,39],[260,240,170,55,150,270],[1,2,2,1,1,1],[8,6,5,8,8,8],390,'Hollow spectral colour, low wooden vessel and kalimba; sand and suspended harmony hang behind.'),
 ('Tobacco Dub Club',[4,0,62,57,33,10],118,0,1,[60,24,48,67,72,79],[270,380,220,150,180,120],[3,4,5,2,3,2],[2,1,2,3,4,4],150,'A dry voltage stab over sub pulses and udu; close wood and a short copper echo.'),
 ('Gourd And Footsteps',[63,53,34,1,38,46],109,9,3,[57,64,69,33,76,81],[280,230,300,340,130,150],[5,3,4,3,2,2],[1,3,3,2,6,5],110,'Brush contact, gourd plectrum and low oud answer each other in an asymmetric hand rhythm.'),
 ('Rosin And Paper',[42,47,22,11,2,27],64,10,9,[58,70,65,77,34,82],[330,170,160,180,280,100],[2,1,1,1,2,1],[6,8,8,8,6,8],290,'Short bowed gestures sit in a long pipe horizon. Felt halos and reed ghosts move slowly.'),
 ('Hearth Post Office',[35,58,50,31,14,39],78,2,11,[62,69,74,57,81,50],[330,180,120,170,170,150],[4,2,1,1,1,1],[3,5,8,8,7,8],170,'Dulcimer and porcelain keys leave room for a bare-fifth choir and wavering bellows.'),
 ('Copper Silk Rain',[17,5,23,43,3,54],52,7,5,[55,67,74,62,31,79],[260,180,120,230,280,110],[1,1,1,1,1,2],[8,8,8,8,8,6],350,'Slow copper-silk strings, brass breath and a harmonic bow; walnut particles drift at the edge.'),
 ('Pebbles In Felt',[8,51,32,60,15,0],92,5,4,[65,72,60,53,77,29],[310,150,260,180,160,300],[3,3,4,3,2,2],[3,4,3,2,5,4],130,'A tactile felt piano and resonant string pebbles above a dry cedar reply and thumb bass.'),
 ('A Window Left Open',[24,44,20,28,18,6],32,0,5,[72,60,67,48,55,79],[80,260,160,180,150,140],[1,1,1,1,1,1],[8,8,8,8,8,8],430,'Quiet shore, bamboo and linen organ. Very long breaths, open fifths and no drum pulse.'),
 ('Clay Key Conversation',[55,61,59,9,1,33],98,3,3,[63,51,70,75,27,58],[240,240,140,280,330,270],[4,3,2,3,3,4],[3,2,6,3,2,3],120,'Clay mallet and low conga speak in turns, with honey tines, wood and a soft phase afterglow.'),
 ('Apricot Night Bus',[7,19,36,10,62,2],126,9,1,[57,69,64,76,53,33],[260,180,150,160,220,360],[3,2,1,4,5,4],[2,4,8,2,2,1],100,'A low velvet saw groove with papery keys and udu. Shorter, drier and more rhythm-led.'),
 ('Four Bows By Firelight',[40,41,42,43,45,3],44,0,9,[48,55,60,67,72,36],[260,210,180,160,110,280],[1,1,2,1,1,1],[8,8,7,8,8,8],330,'Four bow articulations range from low cello to rosin stroke, with alto air and a deep octave floor.'),
 ('Bamboo Breath Quartet',[44,45,46,47,38,25],56,5,4,[53,60,65,72,48,77],[300,220,180,150,170,60],[1,1,3,1,1,1],[8,8,6,8,8,8],240,'Four pipe registers: a low column, breathy alto, fingertip response and distant horizon.'),
 ('Four Floors Below',[0,1,2,3,63,4],104,2,8,[26,38,45,50,62,69],[330,210,180,120,190,140],[4,3,2,1,5,2],[1,2,3,4,2,4],90,'A bass-led room with four genuinely different contours and registers; brushed skin keeps it light.'),
 ('Cedar String Table',[32,33,34,35,11,52],84,7,3,[55,62,67,74,79,50],[290,250,200,170,130,160],[4,3,2,3,1,2],[3,4,6,5,8,6],150,'Muted palm, kora thumb, long oud and dulcimer form a close wooden ensemble.'),
 ('Porcelain After Hours',[56,57,58,59,16,26],62,0,12,[48,60,67,72,55,79],[280,230,180,140,170,55],[1,3,2,1,1,1],[8,6,7,8,8,8],280,'Four restrained phase colours, from almost pure fundamental to prism afterglow, with sand far behind.'),
 ('Mallets Under Ochre',[52,53,54,55,50,0],73,9,10,[57,64,69,76,52,33],[240,220,150,210,130,290],[3,2,1,3,1,2],[4,5,8,6,8,6],260,'Kalimba, gourd, walnut rain and clay mallet use different exciter types and uneven slow cycles.')]

def extend_warm_collection(tones,biomes,tone_fields):
    assert len(tones)==452 and len(biomes)==213
    flat=[]
    for model,collection,rows in GROUPS:
        for name,tone,harm,shape,attack,decay,cutoff,filt,breathe in rows:
            t=dict(id=len(tones),name=name,model=model,tone=tone,harm=harm,shape=shape,
                   attack=attack,decay=decay,cutoff=cutoff,filter=filt,resonance=0,breathe=breathe,
                   collection=collection,introduced='2.0 Warm Collection')
            tones.append(t);flat.append(t)
    assert len(flat)==64
    used=set()
    for j,(name,refs,bpm,root,scale,notes,levels,hits,speeds,space,intent) in enumerate(SCENES):
        dry=space<=150;slow=bpm<65
        b=copy.deepcopy(biomes[64]);b.update(id=len(biomes),name=name,
          family=6 if j in (6,12,19) else 3 if j in (5,13,16) else 2 if j in (1,8,10,17) else 7 if j in (2,3,7,18) else 0 if j in (9,11,15) else 1,
          tempo=bpm,root=root,scale=scale,seed=0xB07A000+j*7919,
          macro=[500,0 if slow else 35,500,space],locked=1,freeze=0,quality=0,
          grain_size=940 if slow else 620 if dry else 790,grain_pos=140+j*37,
          feedback=150 if dry else 260 if j in (4,9,11) else 190,
          grain_pitch=0,reverse=int(j==4),swing=140 if j in (2,5,6,10,12,13,16) else 0,
          duck_source=1 if j==5 else 5 if j in (13,16) else -1,
          duck_amount=130 if j in (5,13,16) else 0,duck_release=500,
          intent=intent,introduced='2.0',sound_family='Botanical Warm Collection',
          # Flash-only extension recipe. No growth of the 2604-byte user patch.
          warm_fx=[min(space,400),300 if dry else 810 if slow else 580,
                   315 if j in (4,7,9,11) else 205 if dry else 285,
                   110 if dry else 290 if j in (4,9) else 220,
                   7 if j in (0,11) else 8 if j in (3,9) else 0,
                   0 if slow or j in (6,16,19) else 1+j%4,
                   j%2])
        for i,ref in enumerate(refs):
            used.add(ref);t=flat[ref];tr=b['tracks'][i]
            tr.update({k:t[k] for k in tone_fields})
            tr.update(note=notes[i],level=levels[i],hits=hits[i],speed=speeds[i],
              length=8 if slow and i==0 else [16,19,23,17,29,31][i],
              rotate=0 if i==0 else [0,3,7,11,2,5][i],
              prob=1000 if i<2 or slow else 920,pan=[470,630,290,740,390,520][i],
              send=25 if t['model']==8 else space,ratchet=1,accent=45 if slow else 110,mute=0)
            tr['rotate']%=tr['length'];b['tones'][i]=t['id']
        biomes.append(b)
    assert used==set(range(64)),sorted(set(range(64))-used)
    assert len(tones)==516 and len(biomes)==233
