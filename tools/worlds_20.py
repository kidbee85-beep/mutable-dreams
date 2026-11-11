"""Append sixteen authored contrasting worlds and 32 warm starting tones."""
import copy

def extend_worlds(tones,biomes,tone_fields):
    assert len(tones)==420 and len(biomes)==197
    sounds=[
      ('Willow Cello',18,360,480,420,410,920,690,1,220),
      ('Low Bow Cloud',18,190,750,270,680,990,570,2,160),
      ('Rosin In Linen',18,680,250,630,180,760,820,1,310),
      ('Warm Viola Room',18,510,390,510,310,870,750,1,210),
      ('Felt Bow Whisper',18,130,660,190,740,970,560,1,260),
      ('Open String Horizon',18,790,330,710,130,940,880,1,290),
      ('Amber Bow Bass',18,280,910,340,80,810,630,2,80),
      ('Threaded Harmonics',18,620,140,820,520,990,810,1,440),
      ('Bamboo At Dawn',19,430,240,380,340,930,820,1,200),
      ('Alto Air Column',19,240,430,270,520,980,710,1,280),
      ('Warm Low Flute',19,170,180,190,270,870,670,1,90),
      ('Breathlit Fifth',19,730,610,510,180,820,910,1,340),
      ('Quiet Paper Pipe',19,310,740,720,660,990,790,1,370),
      ('Golden Flute Edge',19,850,290,370,110,750,960,1,140),
      ('Long Air Shelter',19,380,310,850,820,1000,830,1,460),
      ('Bamboo Fingertip',19,590,170,220,30,470,870,3,70),
      ('Felt Current',12,240,240,650,100,850,700,1,160),
      ('Low Wool VCA',4,270,170,390,110,760,640,2,150),
      ('Amber Subline',8,220,640,290,15,760,590,2,45),
      ('Dry Hollow Nylon',15,540,170,810,15,610,810,1,60),
      ('Bamboo Reed Bed',16,410,670,350,540,980,690,1,320),
      ('Warm Glass Shadow',13,270,360,680,430,940,760,1,280),
      ('Hushed Harmonics',6,160,430,510,650,970,720,1,190),
      ('Paper Cathedral',7,220,710,240,770,1000,700,1,230),
      ('Tender Copper FM',3,120,70,370,90,730,700,1,100),
      ('Soft Resin Drift',10,380,310,670,620,980,690,1,290),
      ('Violet Felt Wave',11,290,620,740,280,950,720,1,240),
      ('Clay Ghost Bongo',17,230,170,360,0,330,710,1,0),
      ('Pocket Pulse Warm',0,120,130,220,0,490,650,1,0),
      ('Paper Brush Air',1,160,280,270,0,300,700,1,0),
      ('Linen Shaker',2,390,510,170,0,160,760,1,0),
      ('Filtered Shore',14,190,270,790,780,1000,670,1,410)]
    for name,model,tone,harm,shape,attack,decay,cutoff,filt,breathe in sounds:
        tones.append(dict(id=len(tones),name=name,model=model,tone=tone,harm=harm,shape=shape,
          attack=attack,decay=decay,cutoff=cutoff,filter=filt,resonance=0,breathe=breathe))
    # Distinct role/engine combinations; first arrival should already be musical.
    scenes=[
      ('Ember Chamber',[420,429,443,440,438,451],42,0,3,[48,67,60,55,36,79],[370,230,140,200,260,55],[1,1,1,1,2,1],[8,8,8,8,6,8],180,'Bowed wood, low air and a quiet harmonium; long breaths rather than a busy tune.'),
      ('Bamboo Estuary',[428,435,439,441,445,438],74,7,4,[67,79,55,72,60,31],[320,170,260,150,170,310],[2,3,3,1,1,3],[4,3,3,8,8,4],260,'Close bamboo, dry nylon replies and a restrained low line, with a flowing asymmetric pulse.'),
      ('Hearth Letters',[440,436,442,423,448,450],88,2,9,[57,69,64,52,33,77],[270,270,160,220,370,90],[2,3,1,1,4,5],[4,3,8,8,1,2],110,'Rounded keys over a soft kick; high harmonics stay distant while an unusual scale opens space.'),
      ('Silt Cassette',[438,437,439,447,424,451],82,5,3,[34,58,65,46,53,77],[370,250,240,240,180,50],[4,3,4,4,1,1],[2,3,2,1,8,8],320,'A dusty kit, hollow plucks and a slow wool bass. Tape noise is a texture, not the lead.'),
      ('Open Door Session',[439,436,447,446,438,435],106,0,4,[60,72,55,67,36,79],[330,250,220,120,320,130],[5,3,5,1,3,2],[1,2,1,8,2,4],70,'Dry fingertips and a small wooden ensemble; little reverb, plenty of rhythmic conversation.'),
      ('Moonstone Vessel',[111,442,441,451,444,427],36,9,10,[57,69,64,81,76,45],[250,210,160,50,120,280],[1,1,1,1,1,1],[8,8,8,8,8,8],440,'Soft struck modes and bowed support suspended in a dark, slowly changing cloud.'),
      ('Palm Almanac',[404,408,448,438,445,433],112,4,3,[65,53,29,41,72,77],[280,310,380,340,170,150],[5,3,4,3,2,1],[1,2,1,2,4,8],90,'Two hands, a warm floor pulse and a low bass motif; an airy answer never dominates.'),
      ('Held Breath',[434,443,440,449,451,424],30,0,8,[60,72,55,48,79,43],[290,180,170,0,45,220],[1,1,1,0,1,1],[8,8,8,8,8,8],520,'A near-still air column and distant choir; percussion is deliberately silent.'),
      ('Ferns At Dusk',[420,426,106,446,451,438],54,10,9,[55,43,67,74,81,34],[320,200,150,180,40,250],[1,1,1,1,1,2],[8,8,8,8,8,6],280,'A low chamber of two bows and gentle resonant wood, anchored by a soft fundamental.'),
      ('Basement Current',[438,448,447,439,430,446],118,0,3,[31,31,62,67,74,55],[390,390,220,230,160,180],[4,4,5,3,1,1],[2,1,2,3,8,8],180,'Deep bass and a soft kick hold the floor while wood and air catch short dub echoes.'),
      ('Paper And Glass',[444,436,442,431,109,447],62,3,12,[63,75,70,82,58,51],[230,290,130,150,160,140],[2,2,1,1,1,2],[6,5,8,8,8,6],390,'Felt attacks, restrained copper and translucent air; ringing colours stay behind the keys.'),
      ('Weathered Choir',[438,432,440,447,427,445],68,7,4,[38,62,57,46,50,74],[300,230,230,240,160,120],[3,1,1,3,1,1],[4,8,8,3,8,8],420,'A worn sample-kit texture meets warm wind and organ, with long gaps between events.'),
      ('Nylon Postcards',[439,382,447,436,451,425],92,2,4,[62,74,55,69,81,50],[350,230,220,200,35,170],[4,3,3,2,1,1],[2,3,2,4,8,8],80,'A close, mostly dry nylon duet; shuffled hand contact and small felt-key answers.'),
      ('Warm Current Chords',[423,428,446,438,442,447],66,5,11,[53,65,72,29,60,48],[300,230,170,290,150,120],[1,2,1,2,1,1],[8,6,8,6,8,8],310,'Slow bow and flute voicings move through a harmonic-major journey over a rounded bass.'),
      ('Stillwater Room',[434,451,443,424,447,439],32,0,4,[60,79,67,48,55,72],[280,40,130,210,0,85],[1,1,1,1,0,1],[8,8,8,8,8,8],600,'Two patient breath-led voices and almost-silent shore noise; a meditation with open space.'),
      ('Ochre Circuit',[437,438,448,447,436,433],124,9,3,[57,33,33,65,69,81],[260,360,380,210,210,150],[3,4,4,5,2,1],[2,1,1,2,4,8],140,'Warm electronic syncopation, felt keys and hand percussion; a rhythm-first contrast to the slow rooms.')]
    for j,(name,ids,bpm,root,scale,notes,levels,hits,speeds,space,intent) in enumerate(scenes):
        b=copy.deepcopy(biomes[64]);b.update(id=len(biomes),name=name,family=[0,2,7,9,5,2,6,0,1,3,7,9,2,7,0,3][j],seed=0x6d3100+j*104729,
          tempo=bpm,root=root,scale=scale,macro=[340 if bpm<60 else 520,20 if bpm<60 else 100,510,space],locked=1,freeze=0,quality=0,
          grain_size=640+j%4*90,grain_pos=260+j*29,feedback=100 if j in (4,12) else 250,
          swing=140 if j in (3,4,6,9,12,15) else 0,grain_pitch=0,reverse=int(j==5),
          duck_source=0 if j in (6,9,15) else -1,duck_amount=120 if j in (6,9,15) else 0,duck_release=500,
          intent=intent,introduced='2.0',sound_family='Warm Rooms')
        for i,ix in enumerate(ids):
            tr=b['tracks'][i];tr.update({k:tones[ix][k] for k in tone_fields})
            tr.update(note=notes[i],level=levels[i],hits=hits[i],length=[16,15,16,17,23,19][i],rotate=0 if i in (0,1) else (j+i*3)%13,
              prob=1000 if i<2 else 870 if bpm>=60 else 1000,speed=speeds[i],ratchet=1,accent=100,
              pan=[430,650,240,510,760,320][i],send=space if i<4 else space//2,mute=int(levels[i]==0))
            if bpm<60 and i<2:tr.update(length=8,rotate=0,hits=1)
            b['tones'][i]=ix
        biomes.append(b)
    assert len(tones)==452 and len(biomes)==213
