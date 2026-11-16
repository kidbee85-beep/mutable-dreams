"""First private expansion: twelve contours, four separately composed Biomes.

Append-only data for the existing engines; no DSP or saved-patch format change.
Levels and phrases are authored for complementary roles, not transposed copies.
"""
from copy import deepcopy
from step_worlds_20 import ALWAYS, FIRST, SECOND, FILL, add_voices, voice

FAMILY = "Bloom Studies"
SHORT = "BLOOM STUDIES"
# Name, model, tone, harmonics, shape, attack, decay, cutoff, breathe.
TONES = (
    ("Petal Strings",    11, 820,  665, 350, 505, 925, 610, 145),
    ("Velour Horizon",   11, 610,  980, 705, 675, 980, 490, 225),
    ("Silver Pollen",    21, 265,  480, 610, 585, 915, 665, 270),
    ("Silk Undertow",    21, 105,  325, 850, 735, 965, 545, 345),
    ("Open Petal Choir",  7, 165,    0, 155, 615, 945, 645, 180),
    ("Dawn Fifth Choir",  7,  90,  900, 110, 770, 975, 535, 245),
    ("Rosin Lantern",    18, 190,  345, 635, 465, 885, 615, 155),
    ("Willow Bow",       18,  95,  230, 790, 685, 960, 540, 285),
    ("Opal Breath",      19, 235,  430, 475, 565, 920, 675, 315),
    ("Night Seed Air",   19,  75,  205, 730, 790, 985, 510, 405),
    ("Mellow Dew",       10, 170,  310, 215, 335, 810, 620, 125),
    ("Honey Afterglow",  10,  70,  145, 590, 625, 950, 500, 210),
)
T = {name: 640+i for i, (name, *_) in enumerate(TONES)}

def held(notes, velocity=66):
    return [(k, note, velocity-(k%3)*3, 100, ALWAYS)
            for k, note in enumerate(notes) if note]

# (tempo, tonic, scale, grain amount, grain size, grain pitch, swing,
#  (FX level, room time, delay ms, delay feedback, noise)).
# All four have different phrase lengths, contour, register and event topology.
SCENES = (
    ("Petals After Rain", (58, 2, 3, 215, 825, 0, 0, (285, 755, 310, 165, 0)),
     "D Dorian: bowed warmth and open choir answer a small felt melody, with long breaths between them.", (
        voice(T["Rosin Lantern"], 300, 330, 125, 6, held([50,50,57,57,60,60,57,55],70), 8),
        voice(T["Open Petal Choir"], 240, 680, 170, 8, held([62,62,65,65,64,64,62,62],60), 8),
        voice(T["Petal Strings"], 205, 550, 150, 7, [(1,69,59,100,ALWAYS),(6,67,55,100,SECOND),(11,64,57,100,ALWAYS)], 12),
        voice(520, 245, 500, 0, 8, [(0,38,74,100,ALWAYS),(4,45,67,100,ALWAYS)], 8),
        voice(524, 220, 265, 165, 2, [(2,74,66,84,ALWAYS),(5,76,54,72,ALWAYS),(9,81,58,90,SECOND),(13,76,55,76,ALWAYS),(15,74,48,82,FILL)]),
        voice(T["Opal Breath"], 115, 760, 245, 8, [(2,69,48,100,ALWAYS),(6,64,44,100,SECOND)], 8),
     )),
    ("Willow Waltz", (87, 9, 1, 125, 690, 0, 28, (235, 675, 276, 210, 0)),
     "A minor: a lilting twelve-step felt phrase, a soft palm pulse and bowed replies.", (
        voice(T["Mellow Dew"], 330, 370, 90, 2, [(0,69,74,88,ALWAYS),(2,72,61,72,ALWAYS),(4,76,65,83,ALWAYS),(7,74,59,80,ALWAYS),(10,71,63,86,SECOND)], 12),
        voice(T["Willow Bow"], 255, 690, 125, 6, [(0,57,66,100,ALWAYS),(6,60,61,100,ALWAYS)], 12),
        voice(T["Silver Pollen"], 170, 235, 185, 4, [(1,76,54,98,ALWAYS),(7,79,50,96,SECOND),(10,74,48,90,FILL)], 12),
        voice(520, 280, 500, 0, 2, [(0,33,79,94,ALWAYS),(4,40,65,85,ALWAYS),(8,36,71,91,ALWAYS)], 12),
        voice(592, 145, 500, 30, 1, [(0,45,59,64,ALWAYS),(6,45,49,57,ALWAYS),(9,48,44,53,SECOND)], 12),
        voice(33, 55, 745, 45, 1, [(2,60,35,32,ALWAYS),(5,60,30,28,ALWAYS),(8,60,33,31,ALWAYS),(11,60,28,28,FILL)], 12),
     )),
    ("Blue Hour Bloom", (46, 5, 2, 290, 905, -12, 0, (325, 805, 340, 180, 0)),
     "F major: a wide slow arc through silk, choir and dark rounded strings, with a falling upper answer.", (
        voice(T["Velour Horizon"], 305, 300, 190, 8, held([53,53,57,57,58,58,57,55],69), 8),
        voice(T["Silk Undertow"], 250, 700, 215, 8, held([65,65,64,64,65,65,60,60],60), 8),
        voice(T["Dawn Fifth Choir"], 190, 500, 135, 8, held([41,41,38,38,46,46,48,48],58), 8),
        voice(T["Honey Afterglow"], 235, 460, 75, 6, [(0,53,65,100,ALWAYS),(6,57,59,100,ALWAYS),(12,55,57,100,SECOND)], 16),
        voice(T["Night Seed Air"], 100, 765, 280, 8, [(1,77,46,100,ALWAYS),(5,76,43,100,SECOND)], 8),
        voice(524, 160, 245, 180, 4, [(3,81,57,93,ALWAYS),(7,79,53,88,ALWAYS),(11,77,50,95,SECOND),(15,72,48,91,FILL)]),
     )),
    ("Lanterns Upstream", (102, 7, 3, 155, 735, 0, 64, (250, 690, 294, 245, 0)),
     "G Dorian: a hopeful plucked motif and quiet syncopation under slow strings; space for an answering phrase.", (
        voice(T["Mellow Dew"], 345, 345, 95, 1, [(0,67,78,80,ALWAYS),(3,74,61,68,ALWAYS),(6,77,67,83,ALWAYS),(10,76,61,78,ALWAYS),(14,72,57,72,SECOND)]),
        voice(T["Rosin Lantern"], 230, 680, 110, 8, [(0,55,66,100,ALWAYS),(4,60,60,100,ALWAYS)], 8),
        voice(T["Silver Pollen"], 165, 765, 185, 4, [(1,79,54,98,ALWAYS),(6,81,51,94,SECOND),(12,77,50,96,ALWAYS)]),
        voice(520, 300, 500, 0, 1, [(0,31,81,91,ALWAYS),(6,38,68,84,ALWAYS),(10,36,74,88,ALWAYS),(14,29,64,78,SECOND)]),
        voice(592, 160, 455, 24, 1, [(0,43,65,60,ALWAYS),(5,43,49,55,ALWAYS),(8,43,58,58,ALWAYS),(13,45,46,53,FILL)]),
        voice(524, 205, 245, 125, 2, [(2,74,62,78,ALWAYS),(7,77,55,72,ALWAYS),(11,81,58,82,SECOND),(15,79,52,75,FILL)]),
     )),
)

def extend_bloom_studies(tones, biomes, tone_fields, family):
    assert len(tones)==640 and len(biomes)==276, "append after the tested 2.2 library"
    for name,model,tone,harm,shape,attack,decay,cutoff,breathe in TONES:
        tones.append(dict(id=len(tones),name=name,model=model,tone=tone,harm=harm,
                          shape=shape,attack=attack,decay=decay,cutoff=cutoff,
                          resonance=0,filter=1,breathe=breathe))
    for index,(name,settings,intent,voices) in enumerate(SCENES):
        tempo,root,scale,space,size,pitch,swing,fx=settings
        scene=deepcopy(biomes[64])
        scene.update(id=len(biomes),name=name,family=family,tempo=tempo,
                     seed=0xB1002300+index*65537,root=root,scale=scale,
                     macro=[500,0,440,space],grain_size=size,grain_pos=245+index*83,
                     grain_pitch=pitch,feedback=145+index*19,reverse=0,swing=swing,
                     locked=1,quality=1,intent=intent,introduced="private bloom study",
                     sound_family=FAMILY,rule_fx=fx,rule_lengths=[],rule_events=[])
        add_voices(scene,voices,tones,tone_fields,1)
        biomes.append(scene)
    assert len(tones)==652 and len(biomes)==280
    assert len({tuple(t[k] for k in tone_fields) for t in tones[-12:]})==12
    assert {i for b in biomes[-4:] for i in b['tones'] if i>=640}==set(range(640,652))
