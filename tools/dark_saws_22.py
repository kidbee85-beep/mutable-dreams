"""2.2 development: warm, dark saw pads after the polysynth-pad idea (#9).

Built on the existing Satin Morph engine ("Rounded Saw", tone 211): HARMONICS near
the saw end of the morph, SHAPE as the slow two-oscillator drift that gives the
chorused width, a low Velvet low-pass and long envelopes. Append only: every
2.1 tone and Biome keeps its ID and data. No new DSP, buffers or RAM.
"""
from copy import deepcopy

from step_worlds_20 import ALWAYS, FIRST, SECOND, FILL, add_voices, voice

FAMILY = "Dark Saw Pads"
SHORT = "DARK SAWS"

# Name, model, tone, harmonics, shape, attack, decay, cutoff, resonance, breathe.
# Resonance stays below 128 like every 2.1 tone: the packed factory rows keep their width
# (flash: the 2.1 image had 4.4 KB left in its app slot).
# Model 11 is Satin Morph. Distinct by brightness, drift, attack and filter so the
# eight remain audibly different in a six-track mix (see tools/check_dark_saws.py).
TONES = (
    ("Night Saw Pad",    11, 880, 1000,  900, 420, 940, 545,  90, 210),  # 632 the core pad
    ("Velvet Saw Choir", 11, 760,  940, 1000, 520, 970, 500,  60, 300),  # 633 widest, slowest
    ("Smoked Strings",   11, 920, 1000,  720, 380, 900, 600, 120, 170),  # 634 brighter, string-like
    ("Low Saw Bed",      11, 650,  980,  820, 450, 960, 455,  40, 250),  # 635 low, rounded floor
    ("Ember Brass Pad",  11, 880,  860,  600, 300, 870, 565, 120, 140),  # 636 firmer front, brass-like
    ("Dusk Saw Bass",    11, 700, 1000,  380,  40, 640, 470, 120,  40),  # 637 short, plucked bass
    ("Hollow Pulse Pad", 11, 800,  760,  940, 460, 950, 490,  70, 330),  # 638 between odd and saw: hollow
    ("Far Saw Haze",     11, 600, 1000, 1000, 560, 985, 430,  30, 380),  # 639 darkest air layer
)
# Sequenced notes sustain for their gate and then release quickly (dreams.cpp, the
# voice envelope), so the pads play legato on every step of a slow track (speed 8,
# gate 100): the envelope continues across retriggers instead of swelling again.
T = {name: 632 + index for index, (name, *_) in enumerate(TONES)}
# Existing 2.1 tones used as contrast (IDs unchanged).
HUSH_PIANO, LOW_WOOL_KEYS, BOWED_AMBER, VELVET_SUB, SMOKED_SUB, UNDER_ICE = 524, 539, 526, 520, 537, 629
MIST_TICK, FINE_RAIN, OPEN_AIR, DEEP_PILLOW = 33, 38, 37, 1

# CPU: the admission guard (dreams.cpp, quantum) takes ECO above 85 points. With the
# shared effects a Biome has 63 for six voices; Satin Morph weighs 12. Each Biome keeps
# three Satin pads plus a Round Bass (8), one 10-point voice and one 8-point voice: 84.
# Settings: tempo, root (0=C), scale (1 minor, 3 dorian, 7 phrygian, 9 harm. minor),
# grain space macro, grain size, grain pitch, swing, (mix, room, delay ms, feedback, noise).
def legato(notes, velocity=70, rules=None):
    """One sustained note a step; rules marks steps that alternate between cycles."""
    rules = rules or {}
    return [(i, n, velocity - (i % 4) * 3, 100, rules.get(i, ALWAYS)) for i, n in enumerate(notes) if n]

SCENES = (
    ("Velour Nightfall", (62, 0, 1, 260, 780, 0, 0, (330, 760, 300, 240, 6)),
     "Slow C-minor saw chords over a sub floor; a hushed piano answers between them.",
     (
         voice(T["Night Saw Pad"], 330, 380, 150, 8, legato([60, 60, 63, 63, 58, 58, 63, 62], 72), 8),
         voice(T["Hollow Pulse Pad"], 280, 640, 170, 8, legato([67, 67, 70, 67, 65, 65, 67, 67], 64, {7: SECOND}), 8),
         voice(T["Far Saw Haze"], 240, 720, 320, 8, legato([72, 75, 75, 74, 72, 70, 72, 74], 58), 8),
         voice(VELVET_SUB, 300, 500, 0, 4, [(0, 36, 80, 90, ALWAYS), (8, 32, 74, 90, ALWAYS), (12, 31, 70, 86, SECOND)]),
         voice(HUSH_PIANO, 220, 300, 220, 2, [(3, 72, 58, 80, ALWAYS), (10, 75, 52, 74, SECOND), (13, 79, 48, 70, FILL)]),
         voice(MIST_TICK, 70, 560, 260, 2, [(2, 60, 40, 30, ALWAYS), (7, 60, 34, 30, ALWAYS), (11, 60, 38, 30, SECOND), (15, 60, 30, 30, ALWAYS)]),
     )),
    ("Saw Cathedral", (54, 2, 3, 300, 880, 0, 0, (360, 820, 340, 260, 4)),
     "Wide D-dorian saw choirs, smoked strings and a brass-like swell in long arches.",
     (
         voice(T["Velvet Saw Choir"], 320, 300, 210, 8, legato([62, 62, 65, 65, 69, 69, 67, 65], 72), 8),
         voice(T["Smoked Strings"], 260, 700, 160, 8, legato([57, 57, 60, 60, 62, 64, 62, 60], 62), 8),
         voice(T["Ember Brass Pad"], 210, 520, 120, 8, legato([0, 74, 0, 72, 0, 76, 0, 77], 58, {7: SECOND}), 8),
         voice(VELVET_SUB, 240, 500, 0, 8, [(0, 38, 78, 100, ALWAYS), (4, 41, 70, 100, ALWAYS)], 8),
         voice(BOWED_AMBER, 210, 240, 200, 4, [(2, 50, 62, 100, ALWAYS), (10, 53, 58, 100, SECOND)]),
         voice(OPEN_AIR, 60, 780, 300, 4, [(6, 60, 34, 40, ALWAYS), (14, 60, 30, 40, SECOND)]),
     )),
    ("Ember Tape Dusk", (76, 5, 9, 210, 640, 0, 40, (280, 690, 260, 300, 0)),
     "A plucked saw bass walks in F harmonic minor below brass-like pads and wool keys.",
     (
         voice(T["Dusk Saw Bass"], 360, 500, 0, 1, [(0, 41, 86, 62, ALWAYS), (3, 41, 64, 50, ALWAYS), (6, 48, 72, 58, ALWAYS), (10, 44, 70, 60, ALWAYS), (14, 40, 62, 52, SECOND)]),
         voice(T["Ember Brass Pad"], 290, 360, 150, 8, legato([65, 65, 68, 68, 64, 64, 68, 67], 70), 8),
         voice(T["Hollow Pulse Pad"], 250, 660, 180, 8, legato([72, 72, 76, 75, 72, 71, 72, 75], 60, {7: FIRST}), 8),
         voice(LOW_WOOL_KEYS, 220, 280, 140, 2, [(2, 72, 60, 70, ALWAYS), (7, 77, 54, 66, ALWAYS), (13, 80, 50, 64, FILL)]),
         voice(DEEP_PILLOW, 180, 500, 0, 1, [(0, 36, 70, 60, ALWAYS), (8, 36, 56, 60, ALWAYS), (11, 36, 44, 50, SECOND)]),
         voice(FINE_RAIN, 55, 720, 120, 1, [(2, 60, 40, 30, ALWAYS), (6, 60, 32, 30, ALWAYS), (10, 60, 38, 30, ALWAYS), (14, 60, 30, 30, ALWAYS)]),
     )),
    ("Deep Saw Tide", (48, 9, 7, 380, 920, -12, 0, (380, 850, 350, 220, 10)),
     "A dark A-phrygian tide: hazy saws, a low bed and octave-down grains, almost still.",
     (
         voice(T["Far Saw Haze"], 300, 320, 330, 8, legato([69, 69, 70, 70, 72, 70, 69, 69], 70), 8),
         voice(T["Low Saw Bed"], 300, 500, 70, 8, legato([45, 45, 46, 46, 45, 45, 48, 46], 72), 8),
         voice(T["Velvet Saw Choir"], 250, 680, 240, 8, legato([57, 60, 60, 57, 57, 64, 60, 58], 62, {5: SECOND}), 8),
         voice(SMOKED_SUB, 280, 500, 0, 8, [(0, 33, 80, 100, ALWAYS), (4, 33, 72, 100, ALWAYS)], 8),
         voice(UNDER_ICE, 200, 240, 280, 8, [(2, 76, 58, 100, ALWAYS), (6, 72, 54, 100, SECOND)], 8),
         voice(MIST_TICK, 55, 760, 300, 4, [(5, 60, 30, 30, ALWAYS), (13, 60, 26, 30, SECOND)]),
     )),
)


def extend_dark_saws(tones, biomes, tone_fields, family):
    assert len(tones) == 632 and len(biomes) == 272, "2.1 catalog must stay unchanged"
    first_tone = len(tones)
    for name, model, tone, harm, shape, attack, decay, cutoff, resonance, breathe in TONES:
        tones.append(dict(id=len(tones), name=name, model=model, tone=tone,
                          harm=harm, shape=shape, attack=attack, decay=decay,
                          cutoff=cutoff, breathe=breathe, filter=1, resonance=resonance))
    for index, (name, settings, intent, voices) in enumerate(SCENES):
        tempo, root, scale, space, size, pitch, swing, fx = settings
        mix, room, delay, feedback, noise = fx
        assert 40 <= delay <= 350 and 0 <= feedback <= 800
        assert 0 <= room <= 850 and 0 <= mix <= 1000 and 0 <= noise <= 1000
        scene = deepcopy(biomes[64])
        scene.update(id=len(biomes), name=name, family=family, tempo=tempo,
                     seed=0x5A3D2200 + index * 40503, root=root, scale=scale,
                     macro=[500, 0, 420, space], grain_size=size, grain_pos=280 + index * 97,
                     grain_pitch=pitch, feedback=150 + index * 37, reverse=0, swing=swing,
                     locked=1, quality=1, intent=intent, introduced="2.2", sound_family=FAMILY,
                     rule_fx=fx, rule_lengths=[], rule_events=[])
        add_voices(scene, voices, tones, tone_fields, 1)
        biomes.append(scene)
    assert {tone for scene in biomes[-len(SCENES):] for tone in scene['tones']
            if tone >= first_tone} == set(range(first_tone, len(tones)))
