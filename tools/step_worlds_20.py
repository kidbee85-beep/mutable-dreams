"""Twelve hand-arranged Biomes that demonstrate step conditions."""
from copy import deepcopy

FAMILIES = ["Bowed Rooms", "Dub Foundations", "Hand & Clay", "Granular Horizons"]
SHORT = ["BOWED", "DUB", "HAND & CLAY", "GRAIN WORLDS"]
FELT_FAMILY = "Felt & Fired Clay"
FELT_SHORT = "FELT & CLAY"

# Each voice is tone, level, pan, grain send, clock division, followed by
# (step, note, velocity, gate, rule). The last two fields describe the step
# expression and its cycle rule. Omitted steps are rests.
ALWAYS, FIRST, SECOND, FILL = range(4)

def voice(tone, level, pan, grain, speed, events, length=16):
    return tone, level, pan, grain, speed, length, events

SCENES = [
    ("Waxed Strings", 52, 0, (250, 790, 330, 160, 0), "A cello and viola trade long notes while a low wooden pipe holds the room.", [
        voice(492, 325, 430, 150, 6, [(0,48,82,100,FIRST),(8,48,70,100,ALWAYS),(13,55,67,96,SECOND)]),
        voice(493, 190, 680, 160, 6, [(2,55,67,100,ALWAYS),(11,57,59,100,SECOND)]),
        voice(470, 160, 300, 60, 8, [(0,36,73,100,ALWAYS),(10,43,64,100,ALWAYS)]),
        voice(462, 135, 740, 70, 4, [(3,60,67,88,ALWAYS),(12,67,65,70,FILL)]),
        voice(485, 90, 210, 90, 4, [(10,64,56,91,SECOND)]),
        voice(476, 88, 530, 310, 8, [(1,55,68,100,ALWAYS)]),
    ]),
    ("Winter Rosin", 60, 2, (205, 720, 296, 155, 10), "Close rosin, widely spaced open fifths and felt keys answering a low floor.", [
        voice(494, 250, 350, 170, 4, [(0,50,82,95,ALWAYS),(4,57,64,92,ALWAYS),(10,60,68,85,SECOND)]),
        voice(495, 190, 670, 250, 6, [(2,62,60,100,ALWAYS),(9,69,57,100,SECOND)]),
        voice(455, 196, 500, 10, 8, [(0,38,76,100,ALWAYS),(9,45,61,91,ALWAYS)]),
        voice(468, 102, 650, 125, 6, [(5,65,69,100,ALWAYS)]),
        voice(462, 95, 225, 130, 3, [(8,72,59,74,SECOND),(14,74,51,72,FILL)]),
        voice(421, 118, 410, 140, 8, [(4,45,66,95,FIRST),(13,48,65,100,ALWAYS)]),
    ]),
    ("Violet Bow Arc", 47, 9, (305, 835, 345, 185, 12), "A low bow rises through soft air and felt. The high phrase opens on alternating cycles.", [
        voice(425, 290, 485, 195, 8, [(0,45,75,100,ALWAYS),(10,52,68,100,ALWAYS)]),
        voice(423, 215, 235, 240, 6, [(3,57,65,100,ALWAYS),(12,60,61,100,SECOND)]),
        voice(499, 123, 730, 170, 8, [(6,64,65,100,ALWAYS),(14,69,58,95,SECOND)]),
        voice(463, 145, 600, 180, 4, [(3,72,57,100,FIRST),(9,67,62,100,ALWAYS)]),
        voice(474, 135, 395, 315, 8, [(2,57,70,100,ALWAYS)]),
        voice(486, 90, 265, 80, 4, [(6,55,61,95,ALWAYS),(13,64,58,85,FILL)]),
    ]),
    ("Sub Lantern", 110, 0, (252, 435, 320, 326, 0), "A dry rubberwood sub leaves room for offbeat voltage stabs and a clay fill.", [
        voice(448, 260, 500, 0, 1, [(i,29,80,61,ALWAYS) for i in (0,4,8,12)]),
        voice(453, 335, 500, 0, 1, [(0,36,81,90,ALWAYS),(3,36,68,61,ALWAYS),(6,43,74,78,ALWAYS),(9,34,70,76,ALWAYS),(12,36,83,90,ALWAYS),(15,31,63,55,SECOND)]),
        voice(456, 200, 560, 95, 1, [(2,63,77,50,ALWAYS),(6,67,65,60,ALWAYS),(10,63,67,55,ALWAYS),(14,70,59,53,SECOND)]),
        voice(513, 140, 320, 40, 1, [(7,53,63,67,ALWAYS),(13,55,71,75,FILL),(15,58,62,50,FILL)]),
        voice(450, 100, 740, 15, 1, [(2,61,50,40,ALWAYS),(6,61,48,50,ALWAYS),(10,61,55,40,ALWAYS),(14,61,44,50,ALWAYS)]),
        voice(461, 122, 245, 150, 2, [(4,72,65,65,SECOND),(13,67,56,61,ALWAYS)]),
    ]),
    ("Clay Dub Step", 94, 5, (225, 570, 315, 290, 18), "Half-time low notes and roomy felt chords with a dry Udu answering the bass.", [
        voice(452, 365, 500, 5, 1, [(0,29,86,95,ALWAYS),(5,36,73,82,ALWAYS),(8,29,80,97,ALWAYS),(14,32,67,74,SECOND)]),
        voice(514, 255, 330, 70, 1, [(2,41,73,90,ALWAYS),(7,45,62,71,ALWAYS),(10,43,69,85,ALWAYS),(13,48,64,65,FILL),(15,41,60,64,FILL)]),
        voice(461, 195, 670, 240, 2, [(2,65,72,67,ALWAYS),(6,68,65,76,ALWAYS),(10,67,69,62,ALWAYS)]),
        voice(450, 142, 520, 35, 1, [(8,49,68,55,ALWAYS)]),
        voice(468, 128, 240, 135, 3, [(1,60,61,95,FIRST),(7,72,55,90,SECOND)]),
        voice(477, 65, 740, 285, 6, [(1,53,62,100,ALWAYS)]),
    ]),
    ("Night Valve", 122, 7, (190, 405, 244, 245, 0), "A steady low groove with rounded saw offbeats, palm slaps and a sparse octave reply.", [
        voice(448, 242, 500, 0, 1, [(i,25,84,62,ALWAYS) for i in (0,4,8,12)]),
        voice(438, 330, 500, 5, 1, [(2,31,77,70,ALWAYS),(6,43,69,65,ALWAYS),(9,29,80,78,ALWAYS),(11,31,75,69,ALWAYS),(14,38,72,66,SECOND)]),
        voice(459, 190, 660, 105, 2, [(1,55,71,50,ALWAYS),(5,58,64,65,ALWAYS),(9,62,68,60,SECOND)]),
        voice(410, 148, 305, 42, 1, [(5,53,60,56,ALWAYS),(13,55,66,50,ALWAYS),(15,58,65,50,FILL)]),
        voice(450, 112, 740, 30, 1, [(3,65,57,43,ALWAYS),(10,67,56,43,SECOND),(14,65,59,43,FILL)]),
        voice(484, 115, 205, 80, 2, [(4,67,68,71,ALWAYS),(11,70,63,65,ALWAYS)]),
    ]),
    ("Hands on Terracotta", 102, 2, (96, 310, 187, 115, 0), "Open conga and fingertip bongo trade gestures over brushed noise and a soft low floor.", [
        voice(408, 295, 360, 40, 1, [(0,45,84,85,ALWAYS),(5,48,68,80,ALWAYS),(9,45,78,85,ALWAYS),(12,43,70,72,ALWAYS),(14,48,65,55,FILL)]),
        voice(406, 225, 710, 35, 1, [(2,62,65,65,ALWAYS),(6,64,59,71,ALWAYS),(11,60,62,70,ALWAYS),(14,65,62,58,SECOND)]),
        voice(450, 165, 570, 40, 1, [(4,55,68,52,ALWAYS),(13,55,73,63,FILL),(15,58,69,54,FILL)]),
        voice(454, 163, 245, 60, 1, [(3,36,62,87,ALWAYS),(8,38,73,85,ALWAYS),(14,41,60,61,FILL)]),
        voice(448, 78, 795, 25, 1, [(7,55,52,51,ALWAYS),(10,55,45,54,ALWAYS)]),
        voice(465, 122, 495, 95, 2, [(1,62,65,70,ALWAYS),(12,69,59,68,SECOND)]),
    ]),
    ("Palm Circle", 112, 9, (115, 335, 232, 145, 0), "Frame drum and Bongo Hembra interlock with brush textures over unequal low cycles.", [
        voice(412, 286, 445, 45, 1, [(0,45,81,90,ALWAYS),(6,43,75,87,ALWAYS),(10,45,72,82,ALWAYS)],12),
        voice(405, 204, 670, 25, 1, [(1,57,61,70,ALWAYS),(4,55,70,71,ALWAYS),(9,57,58,64,SECOND),(14,60,65,60,FILL)]),
        voice(450, 161, 220, 70, 1, [(3,60,53,71,ALWAYS),(8,58,58,63,ALWAYS),(12,60,56,60,ALWAYS)],15),
        voice(454, 175, 540, 50, 1, [(0,33,82,91,ALWAYS),(7,36,63,90,SECOND),(13,38,65,85,FILL)]),
        voice(487, 108, 790, 72, 2, [(4,64,61,78,ALWAYS),(9,69,59,66,SECOND)]),
        voice(452, 95, 305, 40, 1, [(5,52,58,69,ALWAYS),(15,57,66,60,FILL)]),
    ]),
    ("Wood and Hide", 86, 4, (118, 340, 274, 138, 0), "Udu and soft frame drum hold the ground while hollow plucks and muted cedar cross it.", [
        voice(514, 265, 505, 50, 1, [(0,40,83,89,ALWAYS),(7,43,72,84,ALWAYS),(12,40,78,88,ALWAYS)]),
        voice(416, 170, 745, 42, 1, [(3,52,61,60,ALWAYS),(8,55,64,70,ALWAYS),(13,57,64,59,FILL),(15,52,72,59,FILL)]),
        voice(454, 185, 275, 90, 2, [(0,64,72,86,FIRST),(5,67,67,80,ALWAYS),(10,71,63,74,SECOND)]),
        voice(484, 150, 650, 80, 2, [(1,52,65,70,ALWAYS),(6,59,67,78,ALWAYS),(12,62,60,70,ALWAYS)]),
        voice(450, 135, 190, 30, 1, [(4,48,60,72,ALWAYS),(11,45,61,70,ALWAYS),(14,50,62,60,FILL)]),
        voice(452, 95, 550, 145, 4, [(2,71,60,88,ALWAYS),(9,74,55,83,SECOND)]),
    ]),
    ("Silt in Glass", 57, 2, (310, 795, 350, 225, 38), "Warm spectral dust and close bamboo feed a long granular memory over an open low fifth.", [
        voice(472, 235, 355, 570, 6, [(0,50,70,100,ALWAYS),(9,57,67,100,SECOND)]),
        voice(496, 165, 680, 490, 8, [(4,62,65,100,ALWAYS),(13,69,60,90,SECOND)]),
        voice(455, 155, 500, 15, 8, [(0,38,68,100,ALWAYS),(10,45,61,95,ALWAYS)]),
        voice(450, 122, 245, 590, 3, [(3,67,60,73,ALWAYS),(11,69,56,72,SECOND),(14,74,53,64,FILL)]),
        voice(476, 105, 750, 620, 8, [(0,62,70,100,FIRST),(8,57,64,100,ALWAYS)]),
        voice(465, 95, 560, 350, 4, [(5,65,58,87,ALWAYS),(15,72,57,77,FILL)]),
    ]),
    ("Salt Film", 45, 0, (345, 825, 333, 205, 44), "Shore wash and a woven fifth pad surround soft partials that drift down an octave in the tape.", [
        voice(476, 178, 700, 580, 8, [(0,60,65,100,ALWAYS),(10,67,64,98,SECOND)]),
        voice(458, 228, 385, 420, 8, [(0,48,71,100,ALWAYS),(7,55,66,100,ALWAYS)]),
        voice(474, 134, 755, 600, 6, [(3,67,65,98,ALWAYS),(12,76,57,85,SECOND)]),
        voice(482, 135, 230, 290, 8, [(5,64,63,100,ALWAYS)]),
        voice(463, 102, 510, 475, 5, [(3,60,60,96,FIRST),(10,72,56,85,ALWAYS)]),
        voice(464, 82, 440, 475, 4, [(7,79,50,72,SECOND),(15,76,56,68,FILL)]),
    ]),
    ("Mica Drift", 68, 11, (265, 750, 320, 210, 28), "Reverse grains scatter tiny string plucks and felt notes around a low breath and steady sub.", [
        voice(499, 193, 265, 585, 6, [(0,59,74,100,ALWAYS),(9,66,63,95,SECOND)]),
        voice(484, 150, 735, 560, 3, [(4,74,57,80,ALWAYS),(10,71,65,75,ALWAYS),(14,78,57,68,FILL)]),
        voice(453, 165, 500, 10, 8, [(0,35,78,98,ALWAYS),(9,42,64,92,ALWAYS)]),
        voice(475, 136, 650, 495, 4, [(2,66,67,83,FIRST),(8,69,60,83,ALWAYS),(13,74,56,80,SECOND)]),
        voice(478, 97, 380, 620, 8, [(1,62,66,99,ALWAYS)]),
        voice(462, 83, 570, 400, 4, [(5,71,56,73,ALWAYS),(13,78,53,71,FILL)]),
    ]),
]

def add_voices(scene, voices, tones, tone_fields, gain):
    for track, (tone, level, pan, grain, speed, length, events) in enumerate(voices):
        source = tones[tone]
        scene['tones'][track] = tone
        scene['tracks'][track].update({k:source[k] for k in tone_fields})
        scene['tracks'][track].update(level=round(level*gain), pan=pan, send=grain, speed=speed,
                                      hits=0, length=length, note=events[0][1], prob=1000, accent=0)
        scene['rule_lengths'].append(length)
        assert len({step for step, *_ in events}) == len(events)
        for step, note, velocity, gate, rule in events:
            assert 0 <= step < length and 24 <= note <= 96 and 1 <= velocity <= 100 and 1 <= gate <= 100 and 0 <= rule <= 3
            scene['rule_events'].append((track,step,note,velocity,gate,rule))

def extend_step_worlds(tones, biomes, tone_fields):
    assert len(biomes) == 233 and len(tones) == 516
    # Measured from a 20-second stereo render with one queued fill.
    # These are authored fader balances. Playback uses no normalizer.
    level_gains = (1.908, 1.398, 1.12, 0.723, 0.681, 0.834, 1.221, 1.157, 1.434, 2.3, 4.11, 1.935)
    for i, (name, tempo, root, space, intent, voices) in enumerate(SCENES):
        family = i // 3
        mix, room, delay, feedback, noise = space
        scene = deepcopy(biomes[64])
        scene.update(id=len(biomes), name=name, family=10+family, seed=0x51A17001+i*8191,
                     tempo=tempo, root=root, scale=0, macro=[500,0,500,[200,150,70,475][family]],
                     grain_size=[825,670,530,925][family], grain_pos=[220,430,320,210][family]+i*11,
                     feedback=[135,165,90,230][family], grain_pitch=-12 if i==10 else 0, reverse=int(i==11),
                     swing=70 if family==1 else 30 if family==2 else 0,
                     locked=1, quality=1, intent=intent, introduced='2.0', sound_family=FAMILIES[family],
                     rule_fx=space, rule_lengths=[], rule_events=[])
        add_voices(scene, voices, tones, tone_fields, level_gains[i])
        biomes.append(scene)

def extend_felt_worlds(tones, biomes, tone_fields):
    assert len(biomes) == 245 and len(tones) == 540
    scenes = [
        ("Wool Lanterns", 66, 0, (330,800,295,190,0), 285, 820, 0,
         "Warm felt overtones and bowed fifths with a flute entering on alternate cycles.", [
             voice(532,435,400,145,2,[(0,48,86,79,ALWAYS),(3,55,70,72,ALWAYS),(7,60,66,84,SECOND),(10,57,75,89,ALWAYS),(14,55,61,67,ALWAYS)]),
             voice(533,345,685,220,2,[(2,63,67,76,ALWAYS),(6,67,63,90,FIRST),(11,60,66,76,ALWAYS),(14,72,54,80,FILL)]),
             voice(526,380,530,160,5,[(0,48,73,100,ALWAYS),(8,43,66,100,ALWAYS)]),
             voice(537,255,300,70,8,[(0,36,79,100,ALWAYS),(8,31,76,100,SECOND)]),
             voice(527,220,735,165,4,[(2,67,64,100,ALWAYS),(9,62,57,100,SECOND)]),
             voice(528,215,515,300,7,[(0,55,64,100,ALWAYS),(12,60,57,100,SECOND)]),
         ]),
        ("Kiln Dub", 108, 5, (275,525,278,340,0), 115, 560, 100,
         "A soft sub and a hand-drum groove with short ceramic answers and a delayed offbeat stab.", [
             voice(537,515,500,5,1,[(0,29,90,100,ALWAYS),(3,29,74,85,ALWAYS),(6,36,82,89,ALWAYS),(8,29,91,100,ALWAYS),(12,32,77,96,ALWAYS),(14,36,69,66,SECOND)]),
             voice(535,320,680,115,1,[(2,60,83,63,ALWAYS),(6,63,70,59,ALWAYS),(10,67,77,53,ALWAYS),(14,63,64,59,ALWAYS)]),
             voice(538,290,370,36,1,[(0,41,85,76,ALWAYS),(7,43,73,67,ALWAYS),(8,41,84,73,ALWAYS),(11,44,65,64,FILL),(15,48,68,53,FILL)]),
             voice(533,290,730,25,1,[(4,55,63,63,ALWAYS),(8,51,70,75,ALWAYS),(12,58,71,62,ALWAYS),(15,55,53,54,SECOND)]),
             voice(450,175,250,20,1,[(2,65,55,51,ALWAYS),(6,65,53,50,ALWAYS),(10,65,58,48,ALWAYS),(14,65,51,53,ALWAYS)]),
             voice(521,215,540,105,2,[(1,65,71,66,ALWAYS),(7,67,72,56,SECOND),(11,63,70,71,ALWAYS)]),
         ]),
        ("Porcelain Tide", 54, 7, (370,825,327,225,20), 435, 925, 0,
         "Wide porcelain partials dissolve into a long grain tide, with sparse low keys below.", [
             voice(534,380,305,540,3,[(0,67,72,100,ALWAYS),(5,74,66,82,ALWAYS),(12,71,61,90,SECOND)]),
             voice(536,355,710,615,4,[(2,79,62,95,ALWAYS),(9,76,56,100,ALWAYS),(13,74,55,88,FILL)]),
             voice(539,390,490,290,6,[(0,43,75,100,ALWAYS),(8,45,71,100,ALWAYS)]),
             voice(527,305,340,430,5,[(1,62,67,100,ALWAYS),(11,67,62,100,SECOND)]),
             voice(523,330,650,410,8,[(0,55,70,100,ALWAYS),(8,57,62,100,SECOND)]),
             voice(528,280,550,550,6,[(3,69,65,100,ALWAYS),(10,74,58,100,SECOND)]),
         ]),
    ]
    for i,(name,tempo,root,fx,grain,size,swing,intent,voices) in enumerate(scenes):
        scene=deepcopy(biomes[64])
        scene.update(id=len(biomes),name=name,family=14,seed=0x51A4E913+i*8191,
                     tempo=tempo,root=root,scale=0,macro=[500,0,500,grain],
                     grain_size=size,grain_pos=228+i*113,feedback=130+i*36,
                     grain_pitch=0,reverse=0,swing=swing,locked=1,quality=1,
                     intent=intent,introduced='2.0',sound_family=FELT_FAMILY,
                     rule_fx=fx,rule_lengths=[],rule_events=[])
        add_voices(scene,voices,tones,tone_fields,1)
        biomes.append(scene)
