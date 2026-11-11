"""Append contrasting hand-drum scenes. Never rewrite the preceding 189 worlds."""
import copy
def extend_percussion(tones,biomes,tone_fields):
    assert len(tones)==404 and len(biomes)==189
    # Touch / rim content / body character / tail / filter brightness.
    sounds=[('Bongo Honey',560,250,180,310,860),('Bongo Hembra',430,160,260,470,790),
      ('Bongo Fingertip',740,420,100,180,920),('Muted Bongo',250,100,150,110,690),
      ('Conga Open Palm',440,220,340,570,810),('Conga Low Earth',310,120,420,730,700),
      ('Conga Slap',810,880,160,270,950),('Conga Muted',350,390,130,150,750),
      ('Frame Drum Wool',240,170,580,820,670),('Frame Drum Edge',630,650,460,640,870),
      ('Palmwood Skin',540,520,240,360,800),('Gourd Tap',380,300,320,440,720),
      ('Felt Hand Drum',170,90,610,520,610),('Skin And Air',720,750,810,680,900),
      ('Low Ritual Drum',190,140,520,960,650),('Copper Palm',650,940,420,470,890)]
    for name,t,h,s,d,c in sounds:
        tones.append(dict(id=len(tones),name=name,model=17,tone=t,harm=h,shape=s,
          attack=0,decay=d,cutoff=c,filter=1,resonance=0,breathe=0))
    # Keep intentional differences in tempo, pulse density, bass and ambience.
    scenes=[
      ('Palm Current',5,[404,408,398,380,388,401],92,2,3,[56,49,38,62,69,81],
       [470,350,360,280,140,75],[5,3,3,2,1,1],[1,2,2,4,8,8],
       'Two interlocking hand drums, a rounded bass and nylon replies. Dry touch, wide quiet air.'),
      ('Skin In The Mist',0,[412,418,394,400,385,401],38,9,4,[45,33,57,69,64,81],
       [300,250,230,190,170,80],[1,1,1,1,1,1],[8,8,8,8,8,8],
       'Low frame-drum breaths, harmonium and long willow; slow ritual without a busy beat.'),
      ('Courtyard Conversations',5,[406,407,408,411,381,398],108,7,8,[67,60,55,43,74,31],
       [350,300,370,260,220,330],[5,3,4,2,2,3],[1,1,2,3,4,2],
       'Four contrasting palms and fingertips answer each other around a wooden melody.'),
      ('Woolen Pulse',3,[418,410,396,398,395,401],116,5,3,[29,65,53,41,72,84],
       [430,240,250,330,190,70],[4,5,2,3,1,1],[1,2,4,2,4,8],
       'A deep skin pulse replaces the usual kick beneath warm electronic syncopation.'),
      ('Quiet Hands',1,[416,407,399,389,401,385],50,0,4,[48,60,67,55,79,72],
       [340,190,230,180,65,150],[1,2,1,1,1,1],[6,7,8,8,8,8],
       'Felt contact, muted bongo and separated keys; every note has room to disappear.'),
      ('Terracotta Orbit',2,[415,405,403,397,398,384],78,3,9,[63,55,70,75,39,67],
       [320,310,180,220,320,200],[4,3,1,2,2,3],[2,3,8,5,4,3],
       'Earthy membrane modes meet soft copper and spectral wool in uneven repeating circles.'),
      ('Hands Into Clouds',9,[409,417,380,388,398,401],68,10,3,[46,70,65,58,34,82],
       [360,260,330,180,280,80],[2,3,2,1,2,1],[3,4,5,8,4,8],
       'Open conga and airy touch feed the shared cloud; dry nylon remains close in front.'),
      ('Six Skins',6,[404,405,408,410,412,419],104,0,0,[65,58,53,46,41,72],
       [380,320,350,270,260,220],[5,3,4,2,3,7],[1,1,2,2,3,2],
       'Six freely assigned hand drums: high and low bongos, congas, frame body and rim colour.')]
    for j,(name,family,ids,bpm,root,scale,notes,levels,hits,speeds,intent) in enumerate(scenes):
        b=copy.deepcopy(biomes[64]);b.update(id=len(biomes),name=name,family=family,seed=0x517000+j*7919,
          tempo=bpm,root=root,scale=scale,macro=[500,25,490,420 if j==6 else 180 if bpm<60 else 80],
          locked=1,freeze=0,quality=0,grain_size=790,grain_pos=360+j*37,feedback=170,
          swing=80 if j in (0,2,3) else 0,grain_pitch=0,reverse=0,
          duck_source=0 if j==3 else -1,duck_amount=130 if j==3 else 0,duck_release=460,
          intent=intent,introduced='2.0',sound_family='Hand Percussion')
        for i,ix in enumerate(ids):
            tr=b['tracks'][i];tr.update({k:tones[ix][k] for k in tone_fields})
            tr.update(note=notes[i],level=levels[i],hits=hits[i],length=[16,15,16,19,23,16][i],
              rotate=0 if i==0 else (i*3+j)%13,prob=1000,speed=speeds[i],ratchet=1,
              pan=[380,650,500,240,730,440][i],send=440 if j==6 and i<2 else 100 if tones[ix]['model']==17 else 210,accent=160)
            # A slow environment should welcome the player immediately, not
            # wait nearly a minute for its first sustaining voice to rotate in.
            if j==1 and i==2:tr.update(rotate=0,length=8,hits=2,speed=8)
            if j==4 and i==2:tr.update(rotate=0,length=8,hits=1,speed=6)
            b['tones'][i]=ix
        biomes.append(b)
    assert len(tones)==420 and len(biomes)==197
