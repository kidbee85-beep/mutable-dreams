"""Flash-only presets and eight independently arranged Biomes."""
from copy import deepcopy

from step_worlds_20 import ALWAYS, FILL, SECOND, add_voices

FAMILY = "Soft Terrains"
SHORT = "SOFT TERRAINS"

# Name, model, tone, harmonics, shape, attack, decay, cutoff, breath.
TONES = [
    ("Parchment Tumba", 0, 128, 132, 720, 0, 405, 562, 0),
    ("Linen Bongo", 17, 272, 350, 320, 0, 240, 617, 0),
    ("Moss Conga", 17, 188, 241, 540, 0, 315, 593, 0),
    ("Hemp Shaker", 2, 122, 260, 265, 0, 185, 549, 0),
    ("Frame Drum", 0, 110, 94, 670, 0, 395, 575, 0),
    ("Soft Heel", 0, 152, 206, 255, 0, 271, 526, 0),
    ("Cork Rim", 2, 225, 548, 190, 0, 160, 575, 0),
    ("Calabash Brush", 1, 98, 245, 305, 0, 230, 530, 0),
    ("Thimble Wood", 20, 146, 210, 155, 9, 300, 599, 0),
    ("Dry Seed Pod", 15, 164, 590, 575, 10, 295, 635, 0),
    ("Lantern Sub", 8, 74, 774, 290, 31, 663, 492, 30),
    ("Cocoa Bass", 8, 138, 478, 455, 25, 468, 518, 35),
    ("Rounded Pulse", 8, 212, 308, 570, 52, 570, 534, 45),
    ("Dusk Bass", 8, 91, 911, 445, 75, 760, 486, 105),
    ("Wool Pedal", 7, 78, 140, 190, 322, 890, 520, 90),
    ("Cellar Reed", 14, 117, 370, 198, 230, 735, 574, 123),
    ("Loam String", 18, 110, 302, 655, 523, 835, 556, 210),
    ("Felt Fundamental", 20, 71, 178, 130, 72, 640, 498, 25),
    ("Saltglass Air", 19, 192, 377, 515, 615, 925, 648, 413),
    ("Clearwater Silk", 21, 199, 572, 740, 688, 942, 641, 420),
    ("Opal Halo", 11, 145, 409, 340, 494, 840, 617, 229),
    ("Blue Lichen", 14, 109, 427, 210, 746, 940, 585, 321),
    ("Ember Choir", 23, 377, 340, 790, 645, 896, 606, 360),
    ("Ash Paper Air", 19, 91, 228, 695, 751, 959, 543, 440),
    ("Smoke Silk", 21, 102, 395, 873, 719, 966, 574, 355),
    ("Orchard Cello", 18, 132, 275, 740, 580, 928, 585, 247),
    ("Twilit Ooh", 23, 115, 269, 645, 582, 934, 587, 300),
    ("Distant Bronze", 22, 181, 172, 502, 395, 847, 604, 184),
    ("Linen Keys", 12, 189, 282, 191, 90, 690, 635, 107),
    ("Mosswood Pluck", 15, 137, 300, 308, 19, 500, 611, 56),
    ("Velvet Drops", 20, 209, 405, 282, 34, 652, 661, 87),
    ("Sundial Chord", 12, 243, 320, 257, 128, 738, 655, 122),
]

# Note lanes use tone index within this collection. An event is step, note,
# velocity, gate, and optionally a cycle condition.
def lane(tone, level, pan, grain, division, events, length=16):
    return 588 + tone, level, pan, grain, division, length, [
        (*event, ALWAYS) if len(event) == 4 else event for event in events
    ]


# Tempo, tonic, scale, macro send, grain size, grain pitch, swing, shared FX.
SCENES = [
    ("Parchment Circle", (104, 7, 3, 55, 520, 0, 62, (105, 465, 185, 95, 0)),
     "Close palm drums in a dry G Dorian circle, with interlocking bongo and conga replies.",
     [
         lane(0, 575, 420, 12, 1, [(0, 43, 88, 68), (6, 43, 75, 57), (10, 45, 79, 64), (13, 43, 67, 58)]),
         lane(1, 490, 705, 18, 1, [(2, 55, 77, 62), (7, 57, 68, 59), (11, 55, 82, 62), (14, 58, 64, 54, FILL)]),
         lane(2, 505, 280, 22, 1, [(4, 48, 86, 66), (8, 46, 72, 63), (12, 48, 82, 60), (15, 50, 61, 57, SECOND)]),
         lane(3, 260, 630, 18, 1, [(1, 60, 56, 53), (5, 60, 62, 59), (9, 60, 55, 54), (15, 60, 64, 57)]),
         lane(11, 315, 505, 0, 1, [(0, 31, 86, 86), (6, 38, 70, 84), (10, 34, 76, 81)]),
         lane(9, 280, 415, 38, 2, [(3, 67, 72, 65), (9, 64, 64, 70), (13, 65, 63, 65, SECOND)]),
     ]),
    ("Low Lantern Dub", (118, 5, 1, 130, 595, 0, 88, (315, 650, 321, 399, 0)),
     "F-minor sub in front, a low heartbeat and clipped keyboard offbeats with a spacious delay.",
     [
         lane(10, 555, 500, 0, 1, [(0, 29, 92, 91), (3, 29, 75, 82), (7, 36, 80, 88), (10, 32, 80, 90), (14, 39, 72, 71, SECOND)]),
         lane(5, 418, 500, 0, 1, [(0, 29, 87, 62), (4, 29, 79, 62), (8, 29, 85, 61), (12, 29, 81, 62)]),
         lane(28, 410, 665, 130, 1, [(2, 65, 84, 49), (6, 68, 75, 45), (10, 63, 75, 49), (14, 65, 69, 48)]),
         lane(6, 270, 280, 24, 1, [(4, 48, 67, 58), (9, 50, 58, 58, SECOND), (12, 48, 73, 60)]),
         lane(7, 275, 580, 24, 1, [(2, 60, 54, 52), (6, 60, 62, 56), (10, 60, 53, 52), (14, 60, 59, 56)]),
         lane(30, 260, 320, 185, 2, [(3, 72, 62, 73), (9, 70, 61, 74), (15, 75, 56, 70, FILL)]),
     ]),
    ("Saltglass Drift", (52, 2, 2, 415, 890, 0, 0, (348, 790, 310, 205, 8)),
     "A bright, beatless D-major swell with slowly crossing flute, silk and upper harmonics.",
     [
         lane(18, 432, 280, 405, 6, [(0, 69, 75, 100), (8, 73, 67, 100)]),
         lane(19, 410, 698, 380, 8, [(0, 57, 74, 100), (4, 57, 66, 100), (8, 61, 68, 100), (12, 64, 65, 100)]),
         lane(20, 380, 570, 360, 6, [(1, 66, 73, 100), (5, 69, 67, 100), (9, 69, 63, 100), (13, 64, 65, 100)]),
         lane(14, 365, 470, 76, 8, [(0, 38, 77, 100), (3, 38, 69, 100), (8, 45, 70, 100), (12, 38, 66, 100)]),
         lane(21, 275, 350, 510, 7, [(3, 64, 65, 100), (7, 61, 63, 100), (10, 66, 59, 98, SECOND), (14, 69, 60, 100)]),
         lane(30, 262, 745, 248, 4, [(3, 76, 62, 91), (10, 74, 55, 92), (15, 81, 55, 76, FILL)]),
     ]),
    ("Moss in Seven", (84, 9, 1, 182, 700, 0, 26, (245, 722, 275, 180, 0)),
     "A 14-step wood-and-felt piece grouped 6+4+4, with answering palm hits.",
     [
         lane(29, 465, 315, 145, 1, [(0, 57, 85, 86), (3, 64, 65, 72), (6, 60, 81, 84), (10, 55, 79, 79), (12, 59, 61, 71)], 14),
         lane(8, 329, 725, 85, 1, [(2, 72, 65, 69), (7, 69, 72, 73), (11, 76, 60, 66), (13, 74, 54, 66, FILL)], 14),
         lane(17, 423, 510, 0, 1, [(0, 33, 90, 89), (6, 40, 75, 83), (10, 36, 77, 84)], 14),
         lane(2, 355, 410, 52, 1, [(0, 45, 77, 67), (6, 45, 65, 64), (10, 48, 69, 58), (13, 45, 58, 58)], 14),
         lane(6, 282, 625, 65, 1, [(4, 52, 66, 55), (9, 53, 56, 57), (12, 52, 58, 55, SECOND)], 14),
         lane(15, 329, 550, 160, 4, [(0, 45, 71, 100), (7, 52, 63, 93), (12, 48, 62, 90, SECOND)], 14),
     ]),
    ("Ember Veil", (67, 0, 7, 515, 917, -12, 0, (335, 780, 340, 218, 15)),
     "A dark C Phrygian cloud built around a quiet pulse, vowel swells and octave-down grain.",
     [
         lane(22, 488, 600, 625, 6, [(0, 48, 75, 100), (6, 49, 68, 100), (12, 55, 62, 97)]),
         lane(23, 395, 335, 720, 7, [(1, 60, 68, 100), (4, 55, 63, 100), (8, 61, 62, 100), (12, 60, 61, 100)]),
         lane(24, 405, 680, 540, 8, [(0, 43, 75, 100), (3, 43, 69, 100), (7, 48, 63, 100), (11, 48, 60, 100)]),
         lane(13, 399, 495, 0, 8, [(0, 36, 80, 100), (8, 31, 66, 100)]),
         lane(27, 335, 230, 530, 4, [(2, 67, 64, 90), (11, 61, 61, 94), (15, 65, 56, 78, FILL)]),
         lane(4, 300, 500, 62, 4, [(0, 36, 68, 71), (8, 36, 59, 71)]),
     ]),
    ("Ochre Procession", (96, 4, 1, 92, 580, 0, 34, (145, 545, 210, 130, 0)),
     "A twelve-step procession in four groups of three, with a low frame drum and answering woody notes.",
     [
         lane(4, 473, 500, 10, 1, [(0, 40, 88, 70), (3, 40, 71, 64), (6, 40, 83, 69), (9, 43, 72, 65)], 12),
         lane(0, 395, 325, 25, 1, [(2, 47, 78, 63), (5, 45, 70, 65), (10, 47, 79, 67)], 12),
         lane(7, 265, 665, 52, 1, [(1, 60, 51, 57), (4, 60, 60, 53), (7, 60, 55, 52), (11, 60, 61, 55, FILL)], 12),
         lane(12, 485, 498, 0, 1, [(0, 28, 91, 89), (4, 35, 74, 75), (7, 31, 80, 79), (10, 33, 73, 74)], 12),
         lane(9, 318, 738, 82, 1, [(3, 59, 78, 64), (6, 62, 67, 64), (8, 64, 72, 60), (11, 55, 63, 64, SECOND)], 12),
         lane(16, 326, 440, 164, 5, [(0, 47, 77, 100), (6, 55, 65, 100)], 12),
     ]),
    ("Velvet Sundial", (77, 0, 2, 155, 730, 0, 111, (264, 710, 250, 225, 0)),
     "A warm C-major pocket with keyboard voicings, rounded bass and brushed syncopation.",
     [
         lane(31, 460, 590, 145, 2, [(0, 64, 79, 89), (4, 67, 69, 78), (8, 65, 75, 91), (12, 62, 69, 91)]),
         lane(28, 345, 290, 104, 2, [(0, 71, 66, 88), (4, 72, 61, 78), (8, 69, 65, 88), (12, 69, 61, 88)]),
         lane(11, 487, 492, 0, 2, [(0, 36, 88, 91), (3, 40, 67, 72), (4, 33, 85, 90), (8, 41, 86, 90), (12, 43, 87, 91), (15, 35, 65, 65)]),
         lane(3, 290, 692, 26, 1, [(2, 60, 57, 54), (6, 60, 64, 54), (10, 60, 55, 53), (14, 60, 63, 54)]),
         lane(1, 330, 375, 47, 1, [(0, 48, 71, 62), (5, 50, 63, 59), (8, 48, 69, 61), (13, 52, 60, 59, SECOND)]),
         lane(20, 277, 770, 160, 4, [(2, 76, 62, 93), (6, 74, 58, 88), (11, 72, 60, 90), (15, 79, 56, 80, FILL)]),
     ]),
    ("Night Orchard", (45, 9, 3, 315, 854, 0, 0, (349, 795, 330, 166, 0)),
     "A low bowed A Dorian drone with muted felt and a distant bronze answer.",
     [
         lane(25, 515, 350, 315, 8, [(0, 45, 81, 100), (3, 45, 75, 100), (7, 52, 70, 100), (11, 48, 66, 100), (14, 45, 68, 100)]),
         lane(26, 429, 687, 395, 8, [(0, 57, 73, 100), (5, 60, 67, 100), (8, 54, 69, 100), (12, 57, 67, 100)]),
         lane(16, 344, 525, 190, 7, [(1, 40, 77, 100), (4, 40, 68, 100), (9, 45, 70, 100), (13, 40, 65, 100)]),
         lane(15, 330, 410, 96, 8, [(0, 33, 76, 100), (4, 33, 67, 100), (8, 40, 68, 100), (12, 33, 67, 100)]),
         lane(27, 312, 780, 432, 5, [(3, 64, 64, 100), (10, 67, 61, 92, SECOND)]),
         lane(17, 250, 255, 112, 5, [(2, 60, 70, 92), (11, 57, 63, 91), (15, 64, 58, 78, FILL)]),
     ]),
]


def extend_soft_collection(tones, biomes, tone_fields, family):
    assert len(tones) == 588 and len(biomes) == 261
    assert len(TONES) == 32 and len(SCENES) == 8
    for name, model, tone, harm, shape, attack, decay, cutoff, breathe in TONES:
        tones.append(dict(id=len(tones), name=name, model=model, tone=tone,
                          harm=harm, shape=shape, attack=attack, decay=decay,
                          cutoff=cutoff, breathe=breathe, filter=1, resonance=0))
    for index, (name, design, intent, voices) in enumerate(SCENES):
        tempo, root, scale, grain, size, pitch, swing, fx = design
        mix, room, delay, feedback, noise = fx
        assert 40 <= delay <= 350 and 0 <= feedback <= 800
        assert 0 <= room <= 850 and 0 <= mix <= 1000 and 0 <= noise <= 1000
        scene = deepcopy(biomes[64])
        scene.update(id=len(biomes), name=name, family=family,
                     seed=0x7631B827 + index*104729, tempo=tempo, root=root,
                     scale=scale, macro=[500, 0, 500, grain], grain_size=size,
                     grain_pos=180 + index*84, grain_pitch=pitch, feedback=105 + index*23,
                     reverse=0, swing=swing, locked=1, quality=1, intent=intent,
                     introduced="2.0", sound_family=FAMILY, rule_fx=fx,
                     rule_lengths=[], rule_events=[])
        add_voices(scene, voices, tones, tone_fields, 1)
        biomes.append(scene)
    assert {tone for scene in biomes[-8:] for tone in scene['tones']} == set(range(588, 620))
