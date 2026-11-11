"""Authored warm string/organ worlds. Existing preset and Biome IDs stay intact."""
import copy

def extend_atlas(tones,biomes,tone_fields):
    assert len(tones)==380 and len(biomes)==177
    voices=[
      ('Willow Nylon',15,580,260,650,25,760,720,1,90),
      ('Close Cedar',15,720,160,480,15,560,790,1,35),
      ('Felt Harp',15,420,330,730,75,890,670,1,180),
      ('Honey String',15,650,470,390,30,810,740,1,120),
      ('Palmwood',15,760,140,180,5,410,820,1,25),
      ('Long Willow',15,510,420,670,85,970,680,1,270),
      ('River Plectrum',15,840,320,460,18,690,850,1,100),
      ('Dusty Nylon',15,350,210,540,45,570,570,2,80),
      ('Night Harmonium',16,540,750,520,440,940,690,1,270),
      ('Quiet Drawbars',16,220,280,170,390,980,570,1,140),
      ('Honey Pipes',16,680,160,420,180,810,770,1,190),
      ('Violet Reed',16,760,820,660,310,920,740,1,300),
      ('Low Chapel',16,350,420,260,290,950,580,2,150),
      ('Sunlit Reed',16,850,680,350,120,760,820,1,150),
      ('Slow Bellows',16,490,930,810,620,990,640,1,380),
      ('Linen Organ',16,410,100,630,80,650,660,1,120),
      ('Round Amber Pulse',4,290,320,150,60,680,600,2,210),
      ('Soft Copper Tine',3,180,90,220,90,770,670,1,140),
      ('Moss Pedal',8,160,450,280,20,880,470,2,60),
      ('Paper Velvet',12,330,300,320,80,780,660,1,140),
      ('Warm Horizon',11,670,200,610,490,970,610,1,340),
      ('Porcelain Breath',14,330,410,610,460,950,680,1,400),
      ('Low Lantern',10,390,170,300,380,930,550,2,220),
      ('Woolen Partials',13,420,370,290,420,930,620,1,260),
    ]
    for name,m,t,h,s,a,d,c,fil,b in voices:
        tones.append(dict(id=len(tones),name=name,model=m,tone=t,harm=h,shape=s,
                          attack=a,decay=d,cutoff=c,filter=fil,resonance=0,breathe=b))
    # name, family, presets, bpm, root, scale, levels, hits, speeds, notes, intent
    worlds=[
      ('Cedar After Rain',1,[380,398,388,401,364,400],64,2,3,
       [590,360,190,100,135,110],[3,1,1,1,1,0],[3,8,8,8,6,8],[62,38,57,74,69,81],
       'A close nylon melody, a low wooden anchor and distant reed breath.'),
      ('Midnight Harmonium',0,[388,392,402,401,385,400],38,0,4,
       [420,230,170,95,140,100],[1,1,1,1,1,0],[8,8,8,8,7,8],[48,36,60,72,67,79],
       'A low breathing harmonium; long held registers with no busy top line.'),
      ('Nylon Courtyard',2,[381,384,398,390,364,3],96,7,8,
       [510,250,320,150,120,180],[5,3,3,1,2,4],[2,3,2,7,4,1],[55,67,31,74,62,36],
       'Dry fingerpicked wood in Ukrainian Dorian over quiet hand percussion.'),
      ('Lantern Current',3,[393,398,396,380,3,34],114,5,3,
       [280,410,240,190,330,100],[2,5,1,3,4,7],[2,1,5,3,1,1],[65,29,53,72,36,75],
       'Warm reed stabs and an analogue-like bass pulse; a spacious late-night groove.'),
      ('The Quiet Library',1,[399,389,400,385,401,402],42,9,5,
       [340,190,180,150,70,120],[1,1,1,1,1,0],[8,8,8,8,8,8],[57,45,69,64,81,52],
       'Separated felt notes, organ air and slow strings, with generous silence.'),
      ('Cassette Orchard',9,[398,387,399,3,18,34],82,3,1,
       [300,490,230,560,0,0],[3,4,2,0,0,0],[2,2,4,1,1,1],[27,63,70,36,51,75],
       'A dusty broken kit, rounded bass and dry nylon; tape is seasoning, not a blanket.'),
      ('Saltwind Cathedral',0,[391,385,403,401,392,364],48,5,11,
       [310,230,180,110,165,85],[1,1,1,1,1,0],[8,7,8,8,8,8],[53,65,60,77,41,72],
       'Reed harmonics and long willow strings open into a restrained wide room.'),
      ('Palmwood Steps',5,[384,398,373,390,34,380],103,10,3,
       [470,330,220,160,95,140],[5,3,4,1,7,2],[2,2,3,5,1,4],[58,34,65,70,79,77],
       'A tactile wooden pattern, felt percussion and a nimble low counter-line.'),
      ('Dawn Without Edges',1,[400,394,385,401,402,399],34,2,5,
       [280,230,190,90,155,80],[1,1,1,1,1,0],[8,8,8,8,8,8],[50,62,69,81,38,74],
       'Slow bellows and smooth string colour; a beatless world for long listening.'),
      ('Cedar Comets',2,[386,397,388,398,401,372],78,0,6,
       [490,250,170,290,85,130],[4,2,1,1,1,2],[3,5,8,8,8,4],[60,67,55,36,79,72],
       'Bright wooden orbits answer a soft copper tine, with one clear melodic foreground.'),
      ('Warm Circuit',9,[398,395,383,3,18,34],108,7,3,
       [370,320,220,580,0,0],[5,2,3,0,0,0],[1,2,3,1,1,1],[31,67,74,36,51,75],
       'A compact lo-fi drum rack and warm drawbar groove, kept dry and punchy.'),
      ('Reed And River',0,[394,380,400,402,401,392],40,5,4,
       [300,220,180,150,75,120],[1,1,1,1,1,0],[8,8,8,8,8,8],[53,65,72,41,84,60],
       'A slow river of reed, plucked wood and low sine warmth; no competing lead voices.'),
    ]
    for j,(name,family,ids,bpm,root,scale,levels,hits,speeds,notes,intent) in enumerate(worlds):
        b=copy.deepcopy(biomes[64])
        b.update(id=len(biomes),name=name,family=family,seed=0x2a7100+j*4051,
          tempo=bpm,root=root,scale=scale,macro=[500,15 if bpm<55 else 60,460 if bpm<55 else 570,220 if bpm<55 else 95],
          locked=1,freeze=0,quality=0,grain_size=810 if bpm<55 else 590,grain_pos=320+j*31,
          feedback=135,swing=100 if j in (3,5,7,10) else 0,grain_pitch=0,reverse=0,
          duck_source=1 if j in (3,7) else 3 if j in (5,10) else -1,
          duck_amount=160 if j in (3,5,7,10) else 0,duck_release=450,
          intent=intent,introduced='2.0',sound_family='Living Atlas')
        for i,ix in enumerate(ids):
            t=tones[ix];tr=b['tracks'][i];tr.update({k:t[k] for k in tone_fields})
            tr.update(note=notes[i],level=levels[i],hits=hits[i],speed=speeds[i],
              length=[16,23,19,29,16,31][i] if bpm<90 else [16,16,13,23,16,16][i],
              rotate=0 if i==0 else (j+3*i)%13,send=45 if t['model'] in (0,8) else 160 if bpm>=90 else 290,
              pan=[480,500,670,250,760,410][i],ratchet=1,accent=120 if bpm>=90 else 45,
              prob=1000,mute=0)
            tr['rotate']%=tr['length']
            if t['model']<3:tr.update(attack=0,decay=min(tr['decay'],370),resonance=0)
            b['tones'][i]=ix if all(tr[k]==t[k] for k in tone_fields) else -1
        biomes.append(b)
    assert len(tones)==404 and len(biomes)==189
