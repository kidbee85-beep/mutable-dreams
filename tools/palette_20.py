"""Private FM-1 2.0 palette: authored ensembles, no changes to existing IDs."""
import copy


def extend_palette(tones, biomes, tone_fields):
    assert len(tones) == 364 and len(biomes) == 169
    # name, engine, tone, harmonics, contour, attack, decay, cutoff, filter, breath
    voices = [
        ('Felt Pebble',12,260,350,370,55,660,550,1,90),
        ('Linen Tine',12,420,240,560,180,790,590,2,160),
        ('Deep Velour',8,115,160,240,15,730,390,2,50),
        ('Dusky Rubber',8,300,260,430,0,410,510,2,30),
        ('Willow Breath',10,480,250,420,370,910,550,1,320),
        ('Low Reed',10,260,390,650,190,780,450,2,190),
        ('Brushed Triangle',11,800,300,320,160,820,600,1,210),
        ('Silk Undertow',11,640,50,160,420,950,420,2,260),
        ('Paper Mallet',9,160,230,170,0,490,620,1,40),
        ('Felt Conga',9,290,180,110,0,360,510,2,60),
        ('Halo Air',14,320,580,720,490,950,690,1,390),
        ('Warm Reed Noise',14,520,300,540,230,790,510,2,270),
        ('Violet Glass',13,670,450,520,310,880,620,1,250),
        ('Cloud Partials',13,430,620,380,520,980,490,2,340),
        ('Low Vowel',7,220,0,160,450,950,470,2,260),
        ('Open Vowel',7,310,300,220,560,980,610,1,310),
    ]
    for name,model,tone,harm,shape,attack,decay,cutoff,fil,breathe in voices:
        tones.append(dict(id=len(tones),name=name,model=model,tone=tone,harm=harm,
                          shape=shape,attack=attack,decay=decay,cutoff=cutoff,
                          filter=fil,resonance=0,breathe=breathe))
    # Every row has a deliberate foreground, support and rhythmic identity.
    # Resting voices remain editable. They are not six competing lead lines.
    worlds = [
        dict(name='Felt Observatory',family=7,tones=[364,366,372,374,370,376],
             tempo=63,root=2,scale=3,motion=25,colour=420,space=220,
             levels=[465,375,225,120,210,145],hits=[3,1,2,0,1,1],
             speeds=[3,8,5,8,8,7],lengths=[16,16,19,16,23,29],
             notes=[60,36,55,74,62,69],meaning='Felt notes, a low anchor and a distant paper reply.'),
        dict(name='Willow Cloister',family=1,tones=[368,378,371,375,366,377],
             tempo=41,root=0,scale=4,motion=0,colour=350,space=310,
             levels=[380,155,200,100,200,120],hits=[1,1,1,1,1,0],
             speeds=[7,8,8,8,8,8],lengths=[19,23,29,31,16,17],
             notes=[48,60,55,67,36,72],meaning='Slow reeds and soft vowels; no percussion or busy melody.'),
        dict(name='Lowland Dub',family=3,tones=[367,3,370,35,365,375],
             tempo=116,root=5,scale=3,motion=45,colour=470,space=110,
             levels=[380,390,190,100,150,65],hits=[5,4,2,7,2,0],
             speeds=[1,1,2,1,4,8],lengths=[16,16,16,16,16,23],
             notes=[29,29,53,72,65,77],meaning='Dry low drums and a rubber bass, with spacious chord answers.'),
        dict(name='Paper Constellation',family=2,tones=[372,373,364,376,34,366],
             tempo=91,root=7,scale=8,motion=80,colour=600,space=80,
             levels=[570,340,360,180,140,340],hits=[5,3,2,1,4,1],
             speeds=[2,3,4,7,2,8],lengths=[13,17,19,23,29,16],
             notes=[55,62,67,74,72,31],meaning='Close, dry paper and wood in unequal rhythmic orbits.'),
        dict(name='Violet Harmonics',family=0,tones=[376,377,379,374,371,364],
             tempo=52,root=9,scale=11,motion=15,colour=610,space=260,
             levels=[455,340,210,170,285,190],hits=[1,1,1,1,1,0],
             speeds=[5,7,8,8,6,8],lengths=[17,23,29,31,19,16],
             notes=[57,69,57,81,45,76],meaning='Higher spectral halos over a soft continuous undertow.'),
        dict(name='Dustlight Caravan',family=9,tones=[366,365,369,3,18,34],
             tempo=79,root=3,scale=1,motion=35,colour=390,space=170,
             levels=[290,260,200,620,0,0],hits=[2,4,1,0,0,0],
             speeds=[2,2,8,1,1,1],lengths=[16,16,23,16,16,16],
             notes=[27,63,58,39,51,75],meaning='A broken dusty kit behind a warm, unhurried keys phrase.'),
        dict(name='Woolen Steps',family=5,tones=[370,367,368,373,374,377],
             tempo=97,root=10,scale=3,motion=55,colour=450,space=190,
             levels=[280,260,170,140,65,90],hits=[4,3,1,3,0,1],
             speeds=[2,2,7,3,8,8],lengths=[16,16,23,13,29,31],
             notes=[58,34,65,70,77,82],meaning='Rounded repeated notes and a walking low counter-line.'),
        dict(name='Tidepool Choir',family=0,tones=[378,379,375,366,371,365],
             tempo=36,root=5,scale=5,motion=0,colour=370,space=350,
             levels=[270,130,95,180,160,75],hits=[1,1,1,1,1,0],
             speeds=[8,7,8,8,8,8],lengths=[29,31,23,16,19,17],
             notes=[53,65,72,29,60,77],meaning='Separated choral registers and breathing air, without a beat.'),
    ]
    for j, world in enumerate(worlds):
        b=copy.deepcopy(biomes[0])
        b.update(id=len(biomes),name=world['name'],family=world['family'],
                 tempo=world['tempo'],root=world['root'],scale=world['scale'],
                 seed=0x200000+j*3571,macro=[500,world['motion'],world['colour'],world['space']],
                 grain_size=780 if j in (1,4,7) else 520+j*25,
                 grain_pos=240+j*47,feedback=140 if j in (1,4,7) else 100,
                 swing=110 if j in (2,5,6) else 0,grain_pitch=0,reverse=0,
                 duck_source=1 if j==2 else 3 if j==5 else -1,
                 duck_amount=200 if j in (2,5) else 0,
                 intent=world['meaning'],introduced='2.0')
        for i,ix in enumerate(world['tones']):
            tone=tones[ix];tr=b['tracks'][i]
            tr.update({k:tone[k] for k in tone_fields})
            tr.update(note=world['notes'][i],level=world['levels'][i],
                      hits=world['hits'][i],speed=world['speeds'][i],
                      length=world['lengths'][i],rotate=0 if i==0 else (3*i+j)%world['lengths'][i],
                      send=55 if tone['model'] in (0,8) else 180 if j in (2,3,5) else 340,
                      pan=[500,380,660,220,760,520][i],ratchet=1,
                      accent=150 if j in (2,3,5,6) else 55,
                      prob=1000 if i==0 else 970,mute=0)
            # Drums and bass establish a calm centre; upper support is quieter.
            if tone['model']<3:tr.update(attack=0,decay=min(tr['decay'],350),resonance=0)
            if tone['model']==7:tr['harm']=300 if b['scale'] in (2,5,11,12) else 0
            b['tones'][i]=ix if all(tr[k]==tone[k] for k in tone_fields) else -1
        biomes.append(b)
    assert len(tones)==380 and len(biomes)==177
    assert len({x['name'] for x in tones[364:]})==16
    assert not ({x['name'] for x in tones[364:]} & {x['name'] for x in tones[:364]})
    assert len({x['name'] for x in biomes})==177
