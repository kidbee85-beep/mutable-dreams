"""Three arranged Biomes with twelve flash-resident tone presets."""
from copy import deepcopy

from step_worlds_20 import ALWAYS, FIRST, SECOND, FILL, add_voices, voice

FAMILY = "Quiet Habitats"
SHORT = "QUIET HABITATS"

# Name, model, tone, harmonics, shape, attack, decay, cutoff, breathe.
TONES = (
    ("Ivory Rain", 20, 106, 524, 235, 22, 628, 621, 35),
    ("Fern Stem", 15, 146, 711, 170, 18, 463, 602, 74),
    ("Brush Bongo", 17, 149, 781, 115, 0, 233, 548, 0),
    ("Rainwood Keys", 12, 207, 162, 378, 116, 640, 583, 82),
    ("Quiet Voltage", 10, 275, 538, 310, 65, 522, 561, 87),
    ("Sub Silt", 8, 81, 860, 427, 48, 720, 475, 58),
    ("Amber Relay", 22, 190, 387, 376, 45, 545, 598, 93),
    ("Palm Vessel", 17, 203, 147, 806, 0, 452, 578, 0),
    ("Frost Breath", 19, 149, 318, 637, 752, 943, 589, 330),
    ("Under-Ice Choir", 23, 204, 399, 752, 830, 978, 577, 372),
    ("Ice Thread", 21, 154, 570, 793, 667, 961, 604, 355),
    ("Deep Harmonic", 13, 496, 355, 715, 692, 948, 574, 213),
)

SCENES = (
    ("Pearl Fern", (78, 10, 3, 155, 668, 0, 20, (235, 642, 230, 188, 0)),
     "A twelve-step fern of felt, muted cedar and fingertip bongo in B-flat Dorian.",
     (
         voice(620, 445, 620, 142, 1, [(0, 58, 82, 76, ALWAYS), (3, 65, 68, 74, ALWAYS), (8, 63, 74, 79, ALWAYS)], 12),
         voice(621, 334, 300, 91, 2, [(1, 70, 66, 79, ALWAYS), (5, 68, 70, 75, ALWAYS), (10, 65, 63, 81, SECOND)], 12),
         voice(622, 364, 574, 31, 1, [(2, 53, 62, 56, ALWAYS), (5, 51, 78, 59, ALWAYS), (10, 55, 72, 63, ALWAYS)], 12),
         voice(623, 308, 724, 195, 3, [(0, 61, 72, 91, ALWAYS), (6, 65, 64, 95, ALWAYS)], 12),
         voice(625, 392, 505, 0, 2, [(0, 34, 87, 91, ALWAYS), (4, 41, 77, 83, ALWAYS), (8, 39, 76, 82, ALWAYS)], 12),
         voice(588, 279, 442, 23, 1, [(0, 46, 82, 62, ALWAYS), (6, 46, 75, 56, ALWAYS), (9, 48, 63, 61, FILL)], 12),
     )),
    ("Silt Telegraph", (103, 4, 9, 102, 555, 0, 126, (310, 695, 196, 275, 0)),
     "A round E harmonic-minor bass and brushed palm drums below short copper replies.",
     (
         voice(625, 560, 499, 0, 1, [(0, 28, 93, 91, ALWAYS), (5, 35, 78, 81, ALWAYS), (8, 28, 88, 90, ALWAYS), (14, 39, 74, 78, SECOND)]),
         voice(627, 405, 335, 0, 1, [(0, 40, 85, 71, ALWAYS), (7, 47, 74, 62, ALWAYS), (12, 40, 77, 67, ALWAYS)]),
         voice(624, 363, 654, 210, 1, [(2, 64, 75, 45, ALWAYS), (6, 67, 68, 49, ALWAYS), (10, 66, 73, 46, ALWAYS), (14, 71, 60, 54, ALWAYS)]),
         voice(626, 325, 760, 201, 2, [(3, 72, 69, 72, ALWAYS), (9, 75, 67, 64, ALWAYS), (13, 71, 63, 68, FILL)]),
         voice(591, 270, 571, 34, 1, [(1, 61, 53, 49, ALWAYS), (5, 61, 63, 56, ALWAYS), (9, 61, 55, 48, ALWAYS), (13, 61, 61, 58, ALWAYS)]),
         voice(606, 242, 258, 130, 6, [(0, 59, 71, 100, ALWAYS), (8, 64, 65, 100, ALWAYS)]),
     )),
    ("Beneath the Ice", (46, 2, 6, 418, 905, -12, 0, (340, 779, 342, 207, 12)),
     "Low whole-tone swells, glassy flute, softly bowed silk and octave-down grains.",
     (
         voice(628, 445, 287, 515, 6, [(0, 62, 79, 100, ALWAYS), (6, 66, 68, 100, ALWAYS), (11, 70, 63, 100, ALWAYS)]),
         voice(629, 430, 651, 470, 8, [(0, 50, 76, 100, ALWAYS), (5, 54, 68, 100, ALWAYS), (10, 56, 65, 100, ALWAYS)]),
         voice(630, 408, 745, 492, 7, [(1, 58, 74, 100, ALWAYS), (5, 62, 69, 100, ALWAYS), (9, 66, 64, 100, ALWAYS)]),
         voice(631, 382, 520, 47, 8, [(0, 38, 81, 100, ALWAYS), (4, 38, 73, 100, ALWAYS), (8, 44, 69, 100, ALWAYS)]),
         voice(619, 258, 394, 294, 4, [(2, 68, 64, 92, ALWAYS), (9, 72, 60, 94, ALWAYS), (13, 74, 56, 89, FIRST)]),
         voice(615, 257, 669, 320, 5, [(3, 60, 62, 100, ALWAYS), (8, 62, 60, 98, ALWAYS), (14, 70, 55, 92, SECOND)]),
     )),
)

def extend_living_layers(tones, biomes, tone_fields, family):
    assert len(tones) == 620 and len(biomes) == 269
    first_tone = len(tones)
    for name, model, tone, harm, shape, attack, decay, cutoff, breathe in TONES:
        tones.append(dict(id=len(tones), name=name, model=model, tone=tone,
                          harm=harm, shape=shape, attack=attack, decay=decay,
                          cutoff=cutoff, breathe=breathe, filter=1, resonance=0))

    for index, (name, settings, intent, voices) in enumerate(SCENES):
        tempo, root, scale, space, size, pitch, swing, fx = settings
        mix, room, delay, feedback, noise = fx
        assert 40 <= delay <= 350 and 0 <= feedback <= 800
        assert 0 <= room <= 850 and 0 <= mix <= 1000 and 0 <= noise <= 1000
        scene = deepcopy(biomes[64])
        scene.update(id=len(biomes), name=name, family=family, tempo=tempo,
                     seed=0x5EEDB257+index*32749, root=root, scale=scale,
                     macro=[500, 0, 500, space], grain_size=size, grain_pos=245+index*113,
                     grain_pitch=pitch, feedback=137+index*49, reverse=0, swing=swing,
                     locked=1, quality=1, intent=intent, introduced="2.0", sound_family=FAMILY,
                     rule_fx=fx, rule_lengths=[], rule_events=[])
        add_voices(scene, voices, tones, tone_fields, 1)
        biomes.append(scene)
    assert {tone for scene in biomes[-len(SCENES):] for tone in scene['tones']
            if tone >= first_tone} == set(range(first_tone, len(tones)))
