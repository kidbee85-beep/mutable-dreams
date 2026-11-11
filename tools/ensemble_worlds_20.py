"""Silk Ensemble and Copper Bloom presets, with six playable arrangements."""
from copy import deepcopy

from step_worlds_20 import ALWAYS, FIRST, SECOND, FILL, add_voices, voice

FAMILIES = ["Silk & Air", "Copper & Current"]
SHORT = ["SILK & AIR", "COPPER"]

TONES = [
    ("Silk Estuary",21,190,425,340,460,845,625,220),
    ("Blue Hour Silk",21,90,590,740,680,940,555,290),
    ("Mothwing Ensemble",21,295,810,495,300,860,740,135),
    ("Close Silk",21,230,125,145,65,575,685,55),
    ("Low Voltage Silk",21,75,210,115,105,740,470,35),
    ("Soft Velvet Unison",21,390,335,210,115,520,620,100),
    ("Ribbon Swell",21,310,685,875,775,905,710,320),
    ("Still Glass Silk",21,485,70,0,180,720,745,0),
    ("Copper Bell",22,420,333,570,45,690,670,0),
    ("Low Bronze Gong",22,235,133,755,140,820,580,0),
    ("Saffron Voltage",22,290,615,240,45,440,620,110),
    ("Glass Strand",22,530,873,330,260,840,710,220),
    ("Copper Pluck",22,145,455,685,18,390,630,0),
    ("Soft Alloy",22,240,0,135,500,865,550,175),
    ("Crystalline Rain",22,570,947,480,85,755,755,95),
    ("Muted Thumb Bell",22,110,233,400,24,470,520,0),
    ("Estuary Sub",8,55,870,260,55,740,465,0),
    ("Parchment Kick",0,120,180,180,0,265,530,0),
    ("Sable Brush",2,115,430,120,0,225,640,0),
    ("Cypress Thumb",15,175,440,245,22,640,680,0),
    ("Lichen Choir",7,125,335,410,560,850,575,230),
    ("Glass Air",14,195,875,110,540,900,655,360),
    ("Deep Clay Heel",17,135,115,740,0,445,595,0),
    ("Still Water Keys",12,155,155,260,80,770,580,160),
    ("Cedar Harmonics",15,145,210,280,60,830,645,65),
    ("Lichen Organ",7,30,375,160,490,870,570,110),
    ("Fog Reed",14,120,520,215,650,900,570,145),
    ("Felt Drip",20,125,215,110,18,430,595,35),
    ("Calabash Low",0,170,105,785,0,380,610,0),
    ("Palm Bongo",17,370,330,350,0,250,690,0),
    ("Choked Conga",17,235,620,290,0,175,580,0),
    ("Paper Shaker",2,80,365,255,0,205,570,0),
    ("Wool Sub",8,85,760,395,48,760,480,0),
    ("Copper Spark",22,215,620,315,30,355,625,15),
    ("Silk Offset",21,95,270,740,355,665,555,180),
    ("Glazed Sine",7,125,455,305,185,820,605,110),
]

# An arrangement specifies the shared space and six independent note lanes.
# Quiet voices leave room for the shared tails; the denser grooves keep low
# notes out of the grain bus and use the conditional fill for short responses.
SCENES = [
    ("Silk Estuary",58,2,0,(345,810,305,190,0),325,830,0,
     "Low cello and a slow silk ensemble open onto a warm flute and distant plucked wood.", [
        voice(540,420,440,255,6,[(0,50,79,100,ALWAYS),(5,57,66,100,ALWAYS),(11,53,63,97,SECOND)]),
        voice(541,290,650,330,8,[(2,65,63,100,ALWAYS),(10,64,60,100,SECOND)]),
        voice(526,350,305,180,7,[(0,38,70,100,ALWAYS),(8,45,64,100,ALWAYS)]),
        voice(559,295,770,145,3,[(4,62,66,82,ALWAYS),(11,69,62,82,ALWAYS),(14,72,55,67,FILL)]),
        voice(527,215,240,240,5,[(1,57,60,100,ALWAYS),(9,60,57,94,SECOND)]),
        voice(561,200,535,475,8,[(0,62,65,100,ALWAYS),(12,65,57,100,SECOND)]),
     ]),
    ("Noctiluca",82,4,0,(320,760,273,210,15),365,875,28,
     "Tiny copper bells alternate with soft, slowly beating tones and a shadow of the grain return.", [
        voice(548,365,335,410,3,[(0,64,75,93,ALWAYS),(5,67,62,81,ALWAYS),(11,71,60,86,SECOND)]),
        voice(554,265,735,555,4,[(2,76,61,80,FIRST),(8,74,58,81,ALWAYS),(14,79,54,66,FILL)]),
        voice(542,360,540,245,6,[(0,52,75,100,ALWAYS),(7,59,66,100,SECOND)]),
        voice(556,290,500,0,6,[(0,40,83,100,ALWAYS),(8,35,73,96,ALWAYS)]),
        voice(558,220,290,95,2,[(3,69,53,52,ALWAYS),(9,66,52,62,ALWAYS),(14,69,51,58,SECOND)]),
        voice(527,200,605,395,7,[(2,55,59,100,ALWAYS),(9,62,57,100,SECOND)]),
     ]),
    ("Saffron Current",112,5,0,(260,565,289,315,0),122,570,85,
     "Rounded voltage stabs, a walking F-minor sub and a brushed dub pocket.", [
        voice(557,390,500,0,1,[(0,29,88,59,ALWAYS),(6,29,80,58,ALWAYS),(8,29,85,58,ALWAYS),(14,29,79,54,SECOND)]),
        voice(556,500,500,6,1,[(0,29,91,93,ALWAYS),(3,29,75,81,ALWAYS),(6,36,80,86,ALWAYS),(10,32,80,88,ALWAYS),(13,39,76,76,SECOND)]),
        voice(550,320,680,125,1,[(2,65,84,63,ALWAYS),(6,68,70,59,ALWAYS),(10,72,76,52,ALWAYS),(14,70,69,60,ALWAYS)]),
        voice(558,240,255,25,1,[(2,58,57,52,ALWAYS),(6,58,56,53,ALWAYS),(10,58,57,50,ALWAYS),(14,58,51,53,ALWAYS)]),
        voice(562,290,385,38,1,[(4,41,78,76,ALWAYS),(11,43,70,62,SECOND),(13,46,66,58,FILL),(15,48,64,54,FILL)]),
        voice(543,245,650,160,2,[(1,60,71,74,ALWAYS),(7,63,67,67,ALWAYS),(12,65,58,69,SECOND)]),
     ]),
    ("Under Glass",66,9,0,(360,785,323,230,20),440,910,0,
     "A hollow bronze note fades into inhaling air, a porcelain answer and two quiet keys.", [
        voice(549,370,485,290,7,[(0,45,76,98,ALWAYS),(8,52,66,100,SECOND)]),
        voice(551,340,700,625,5,[(2,69,68,100,ALWAYS),(10,72,60,100,ALWAYS)]),
        voice(536,275,325,560,3,[(5,76,65,84,ALWAYS),(11,71,55,93,SECOND),(14,79,51,81,FILL)]),
        voice(563,340,510,185,6,[(0,57,72,100,ALWAYS),(9,60,63,100,SECOND)]),
        voice(546,335,270,375,8,[(1,64,65,100,ALWAYS),(10,62,60,100,ALWAYS)]),
        voice(561,285,610,625,6,[(3,69,61,100,ALWAYS),(11,67,55,100,SECOND)]),
     ]),
    ("Copper Step",122,7,0,(215,480,230,250,0),85,490,70,
     "A skippy low-end groove with an unpolished thumb bell, dry clay and a ghost-step fill.", [
        voice(557,445,500,0,1,[(0,31,90,60,ALWAYS),(4,31,83,58,ALWAYS),(8,31,89,62,ALWAYS),(12,31,84,63,ALWAYS)]),
        voice(520,530,500,5,1,[(0,31,90,82,ALWAYS),(3,31,80,66,ALWAYS),(6,38,83,80,ALWAYS),(9,34,75,67,ALWAYS),(12,31,88,91,ALWAYS),(15,29,69,62,SECOND)]),
        voice(552,380,685,115,1,[(1,58,81,53,ALWAYS),(6,62,75,55,ALWAYS),(9,60,72,55,ALWAYS),(14,65,72,49,FILL)]),
        voice(558,230,700,30,1,[(2,62,59,50,ALWAYS),(6,62,58,50,ALWAYS),(10,62,59,50,ALWAYS),(14,62,53,50,ALWAYS)],15),
        voice(538,305,325,45,1,[(4,43,77,69,ALWAYS),(12,43,71,58,ALWAYS),(13,46,70,53,FILL),(15,48,66,54,FILL)]),
        voice(555,235,325,110,2,[(3,67,65,68,ALWAYS),(10,65,63,70,SECOND),(14,70,57,64,ALWAYS)]),
     ]),
    ("Blue Hour Choir",50,0,0,(380,840,325,180,12),365,895,0,
     "A slow C-minor choir with a low silk octave, muted bell and long breaths between phrases.", [
        voice(541,415,680,290,8,[(0,48,78,100,ALWAYS),(6,55,67,100,ALWAYS),(12,51,60,100,SECOND)]),
        voice(560,360,465,360,8,[(0,60,68,100,ALWAYS),(8,58,64,100,ALWAYS)]),
        voice(544,295,360,55,8,[(1,36,75,100,ALWAYS),(10,43,64,100,SECOND)]),
        voice(553,250,730,460,6,[(2,67,62,100,ALWAYS),(11,63,55,94,SECOND)]),
        voice(527,235,245,390,5,[(3,72,65,100,ALWAYS),(9,70,61,96,ALWAYS),(14,75,53,85,FILL)]),
        voice(561,205,540,525,8,[(2,55,59,100,ALWAYS),(11,60,55,100,SECOND)]),
     ]),
    ("Cedar Rainfall",52,2,0,(315,835,330,172,22),404,904,0,
     "Cedar harmonics and a soft reed emerge from a D-minor organ pedal and sparse felt droplets.", [
        voice(565,410,480,180,8,[(0,38,75,100,ALWAYS),(8,50,61,100,SECOND)]),
        voice(564,315,280,285,5,[(1,62,77,92,ALWAYS),(6,57,67,89,ALWAYS),(13,65,58,90,SECOND)]),
        voice(566,260,715,420,8,[(2,65,63,100,ALWAYS),(10,64,58,100,SECOND)]),
        voice(567,215,365,535,4,[(3,69,63,85,ALWAYS),(11,74,56,80,FILL)]),
        voice(575,255,665,265,8,[(0,57,65,100,ALWAYS),(9,60,58,100,SECOND)]),
        voice(541,230,480,185,7,[(4,53,68,98,ALWAYS),(12,57,60,100,SECOND)]),
     ]),
    ("Terracotta Relay",106,7,0,(110,430,200,135,0),78,610,94,
     "Dry conga and bongo calls circle a calabash, a soft shaker and a grounded G-minor bass.", [
        voice(568,420,460,10,1,[(0,39,94,62,ALWAYS),(6,39,79,64,ALWAYS),(10,41,73,54,SECOND)]),
        voice(569,340,730,24,1,[(2,56,79,68,ALWAYS),(7,55,72,62,ALWAYS),(12,58,74,58,FILL)],15),
        voice(570,365,230,35,1,[(4,48,84,58,ALWAYS),(9,46,77,62,ALWAYS),(14,48,70,55,SECOND)]),
        voice(571,200,645,55,1,[(1,60,51,57,ALWAYS),(5,60,62,51,ALWAYS),(11,60,56,60,ALWAYS),(15,60,63,51,FILL)]),
        voice(572,370,490,0,1,[(0,31,84,92,ALWAYS),(3,38,71,84,ALWAYS),(8,34,78,89,ALWAYS),(13,31,66,90,SECOND)]),
        voice(573,205,360,140,3,[(5,67,67,73,ALWAYS),(11,70,58,69,SECOND)]),
     ]),
    ("Tidal Semaphore",94,9,0,(285,730,320,284,10),280,780,48,
     "An uneven bell phrase and syncopated sub leave space for two interlocked silk tones.", [
        voice(573,340,290,245,2,[(0,69,76,76,ALWAYS),(4,72,67,68,ALWAYS),(9,76,68,68,SECOND),(13,71,61,60,FILL)],15),
        voice(574,340,690,225,4,[(2,60,77,94,ALWAYS),(7,64,63,94,ALWAYS),(11,59,61,95,SECOND)],13),
        voice(572,450,475,0,1,[(0,33,85,90,ALWAYS),(6,40,78,90,ALWAYS),(12,36,71,93,SECOND)]),
        voice(575,255,365,350,6,[(0,57,70,100,ALWAYS),(9,64,60,100,SECOND)]),
        voice(567,275,615,215,1,[(3,64,68,70,ALWAYS),(11,67,63,66,SECOND),(15,72,58,59,FILL)]),
        voice(571,160,720,65,1,[(2,62,55,56,ALWAYS),(6,62,49,62,ALWAYS),(10,62,51,57,ALWAYS),(14,62,54,57,ALWAYS)]),
     ]),
]

def extend_ensemble(tones, biomes, tone_fields):
    assert len(tones)==540 and len(biomes)==248
    for name,model,tone,harm,shape,attack,decay,cutoff,breathe in TONES:
        tones.append(dict(id=len(tones),name=name,model=model,tone=tone,harm=harm,
                          shape=shape,attack=attack,decay=decay,cutoff=cutoff,
                          breathe=breathe,filter=1,resonance=0))
    for i,(name,tempo,root,scale,fx,grain,size,swing,intent,voices) in enumerate(SCENES):
        family=0 if i in (0,5,6) else 1
        scene=deepcopy(biomes[64])
        scene.update(id=len(biomes),name=name,family=15+family,seed=0xA74112C5+i*104729,
                     tempo=tempo,root=root,scale=scale,macro=[500,0,500,grain],grain_size=size,
                     grain_pos=180+i*92,feedback=120+i*24,grain_pitch=0,reverse=0,swing=swing,
                     locked=1,quality=1,intent=intent,introduced='2.0',sound_family=FAMILIES[family],
                     rule_fx=fx,rule_lengths=[],rule_events=[])
        add_voices(scene,voices,tones,tone_fields,1)
        biomes.append(scene)
