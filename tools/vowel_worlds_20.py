"""Vowel Bloom tones and four contrasting six-track arrangements."""
from copy import deepcopy

from step_worlds_20 import ALWAYS, FIRST, SECOND, FILL, add_voices, voice

FAMILY = "Vowels & Undertones"
SHORT = "VOWELS & AIR"

# Name, engine, vowel/tone, focus/harmonics, swell/shape, attack, decay, cutoff, breath.
TONES = [
    ("Soft Ooh", 23, 95, 370, 340, 650, 890, 625, 210),
    ("Salt Ah", 23, 485, 560, 685, 550, 850, 690, 380),
    ("Moon Ee", 23, 915, 440, 420, 345, 745, 735, 160),
    ("Hush Vowel", 23, 140, 240, 790, 795, 960, 570, 435),
    ("Chapel Formant", 23, 450, 710, 490, 360, 890, 725, 250),
    ("Vowel Thumb", 23, 295, 630, 105, 36, 350, 650, 25),
    ("Muted Oo", 23, 50, 250, 135, 80, 490, 510, 85),
    ("Dawn Mouth", 23, 710, 520, 900, 430, 890, 665, 610),
    ("Brushed Calabash", 17, 145, 295, 620, 0, 320, 570, 0),
    ("Cocoon Sub", 8, 65, 740, 215, 40, 660, 480, 30),
    ("Sea Glass Tines", 20, 205, 330, 345, 42, 735, 660, 130),
    ("Salt Flute", 19, 155, 300, 545, 330, 755, 615, 180),
]

SCENES = [
    ("Chapel of Fog", 47, 2, 3, (375, 825, 340, 175, 16), 410, 860, 0,
     "A slow Dorian vowel choir, flute and felt notes move above an open low fifth.", [
        voice(576, 410, 380, 270, 8, [(0, 50, 81, 100, ALWAYS), (9, 57, 65, 100, SECOND)]),
        voice(577, 335, 650, 310, 8, [(2, 65, 68, 100, ALWAYS), (11, 64, 65, 100, SECOND)]),
        voice(579, 255, 525, 470, 8, [(5, 69, 65, 100, ALWAYS), (14, 71, 56, 100, FILL)]),
        voice(585, 250, 495, 0, 7, [(0, 38, 71, 100, ALWAYS), (8, 45, 62, 100, ALWAYS)]),
        voice(586, 275, 735, 310, 4, [(4, 62, 64, 86, ALWAYS), (13, 69, 59, 89, SECOND)]),
        voice(587, 230, 255, 480, 6, [(3, 57, 69, 100, FIRST), (10, 60, 59, 100, ALWAYS)]),
     ]),
    ("Clay Skipping", 116, 7, 1, (150, 425, 209, 160, 0), 115, 610, 105,
     "Calabash and paper shaker sit against clipped vowel notes and a round bass line.", [
        voice(584, 405, 495, 30, 1, [(0, 43, 91, 68, ALWAYS), (5, 46, 75, 58, ALWAYS), (13, 43, 73, 57, SECOND)], 15),
        voice(571, 230, 690, 45, 1, [(2, 60, 57, 62, ALWAYS), (6, 60, 61, 62, ALWAYS), (10, 60, 50, 56, ALWAYS), (14, 60, 62, 58, FILL)]),
        voice(585, 435, 470, 0, 1, [(0, 31, 89, 88, ALWAYS), (3, 38, 76, 82, ALWAYS), (8, 34, 81, 89, ALWAYS), (13, 41, 69, 79, SECOND)]),
        voice(581, 345, 300, 105, 1, [(1, 58, 83, 48, ALWAYS), (7, 62, 67, 48, ALWAYS), (14, 65, 64, 47, FILL)]),
        voice(586, 235, 755, 130, 2, [(3, 67, 67, 60, ALWAYS), (9, 65, 59, 61, SECOND)]),
        voice(550, 220, 390, 65, 1, [(4, 55, 71, 55, ALWAYS), (11, 58, 64, 55, ALWAYS)]),
     ]),
    ("Afterimage Dub", 128, 5, 7, (245, 455, 234, 305, 0), 153, 625, 56,
     "A Phrygian sub, quiet kicks and short vowel stabs with echoes that fill the rests.", [
        voice(557, 360, 500, 0, 1, [(0, 29, 91, 63, ALWAYS), (4, 29, 86, 62, ALWAYS), (8, 29, 88, 63, ALWAYS), (12, 29, 84, 63, ALWAYS)]),
        voice(585, 450, 505, 0, 1, [(0, 29, 90, 94, ALWAYS), (3, 30, 71, 77, ALWAYS), (7, 36, 79, 79, ALWAYS), (10, 34, 83, 89, ALWAYS), (14, 37, 70, 79, SECOND)]),
        voice(582, 340, 710, 75, 1, [(2, 65, 84, 48, ALWAYS), (6, 66, 69, 54, ALWAYS), (10, 68, 77, 48, ALWAYS), (14, 65, 67, 56, ALWAYS)]),
        voice(578, 215, 235, 130, 2, [(1, 72, 66, 65, ALWAYS), (9, 73, 59, 65, SECOND), (14, 75, 56, 58, FILL)]),
        voice(558, 190, 565, 35, 1, [(2, 60, 56, 50, ALWAYS), (6, 60, 52, 54, ALWAYS), (10, 60, 57, 52, ALWAYS), (14, 60, 50, 52, ALWAYS)]),
        voice(573, 230, 390, 155, 1, [(3, 56, 71, 61, ALWAYS), (11, 58, 63, 60, ALWAYS), (15, 60, 60, 58, FILL)]),
     ]),
    ("Murmur Reef", 70, 9, 8, (325, 805, 310, 215, 26), 380, 820, 0,
     "An uneven Mi Sheberach melody surfaces between slow vowel sweeps and granular flute.", [
        voice(583, 350, 700, 355, 5, [(0, 57, 75, 95, ALWAYS), (4, 63, 66, 85, ALWAYS), (9, 64, 71, 92, SECOND), (12, 66, 59, 78, FILL)], 13),
        voice(580, 320, 290, 315, 7, [(2, 60, 68, 100, ALWAYS), (10, 66, 59, 95, SECOND)], 15),
        voice(587, 275, 660, 570, 6, [(3, 69, 65, 96, ALWAYS), (13, 71, 60, 91, SECOND)]),
        voice(574, 240, 405, 365, 7, [(1, 52, 67, 100, ALWAYS), (11, 57, 62, 100, SECOND)]),
        voice(586, 245, 250, 180, 3, [(5, 75, 65, 75, ALWAYS), (12, 72, 58, 76, SECOND), (15, 76, 56, 72, FILL)]),
        voice(585, 285, 505, 0, 8, [(0, 33, 78, 100, ALWAYS), (9, 40, 63, 99, ALWAYS)]),
     ]),
]

def extend_vowel(tones, biomes, tone_fields, family):
    assert len(tones) == 576 and len(biomes) == 257
    for name, model, tone, harm, shape, attack, decay, cutoff, breathe in TONES:
        tones.append(dict(id=len(tones), name=name, model=model, tone=tone, harm=harm, shape=shape,
                          attack=attack, decay=decay, cutoff=cutoff, breathe=breathe,
                          filter=1, resonance=0))
    for index, (name, tempo, root, scale, fx, grain, size, swing, intent, voices) in enumerate(SCENES):
        scene = deepcopy(biomes[64])
        scene.update(id=len(biomes), name=name, family=family, seed=0x38C75413 + index * 104729,
                     tempo=tempo, root=root, scale=scale, macro=[500, 0, 500, grain],
                     grain_size=size, grain_pos=225 + index * 156, feedback=150 + index * 29,
                     grain_pitch=-12 if index == 3 else 0, reverse=0, swing=swing,
                     locked=1, quality=1, intent=intent, introduced="2.0", sound_family=FAMILY,
                     rule_fx=fx, rule_lengths=[], rule_events=[])
        add_voices(scene, voices, tones, tone_fields, 1)
        biomes.append(scene)
