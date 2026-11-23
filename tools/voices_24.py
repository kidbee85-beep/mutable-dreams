"""2.4 development: warmer synth voices, heavier bass voices and string voices, twelve tones and twenty Biomes.

Append-only data for the existing engines (Satin Morph, Plaits VA, Mellow Drift, Round Bass, Willow Bow,
Silk Ensemble, Cedar String): no DSP, buffer or saved-patch format change. Every 2.3 tone and Biome keeps its
ID and data. The parameters follow the neighbours in each model's existing range; the harness
(tests/audio_qa.cpp `tone`) measures them against the intent (warm: low centre of the spectrum; bass:
most energy below 100 Hz; strings: slow attack, mid centre).
"""
from copy import deepcopy
from step_worlds_20 import ALWAYS, FIRST, SECOND, FILL, add_voices, voice

FAMILY = "Warm Voices"
SHORT = "WARM VOICES"
FIRST_TONE = 652
FIRST_BIOME = 280
# name, model, tone, harm, shape, attack, decay, cutoff, resonance, breathe
TONES = (
    # warm synth voices
    ("Parlour Saw Pad",   11, 900, 760, 820, 520, 940, 520,  40, 200),  # 652 Satin saw, rounded
    ("Honeyed Poly",       4, 400, 640, 700, 120, 780, 470,   0, 160),  # 653 Plaits VA, soft poly
    ("Cedar Glow Pad",    10, 560, 520, 600, 480, 880, 640,   0, 220),  # 654 Mellow Drift
    ("Ember Organ Pad",   11, 700, 820, 260, 160, 900, 560,  30, 180),  # 655 organ-like warmth
    # bass
    ("Cellar Sub",         8, 130, 160, 140,   0, 300, 330,   0,   0),  # 656 very low, dark
    ("Hearth Bass",        8, 200, 360, 300,   0, 420, 430,   0,   0),  # 657 round, a little more body
    ("Slow Saw Bass",     11, 640, 900, 500,  60, 800, 430,  60,  60),  # 658 low saw, slow
    ("Tape Sub Hum",      10, 140, 100, 160, 380, 960, 420,   0,  90),  # 659 sustained low hum
    # strings
    ("Cello Parlour",     18, 300, 620, 380, 360, 930, 600,   0, 200),  # 660 low bowed
    ("Viola Haze",        18, 520, 450, 560, 440, 950, 680,   0, 260),  # 661 mid bowed, hazy
    ("Ensemble Glow",     21, 240, 620, 520, 520, 900, 640,   0, 240),  # 662 string ensemble
    ("Pizzicato Cedar",   15, 560, 300, 540,  20, 640, 760,   0, 110),  # 663 plucked
)
T = {name: FIRST_TONE + i for i, (name, *_) in enumerate(TONES)}
# existing tones used as contrast (IDs unchanged)
VELVET_SUB, SMOKED_SUB, HUSH_PIANO, LOW_WOOL_KEYS, MIST_TICK, FINE_RAIN, OPEN_AIR, DEEP_PILLOW, BOWED_AMBER = 520, 537, 524, 539, 33, 38, 37, 1, 526

def held(notes, velocity=66):
    return [(k, note, velocity - (k % 3) * 3, 100, ALWAYS) for k, note in enumerate(notes) if note]

# (tempo, tonic, scale, grain amount, grain size, grain pitch, swing, (FX level, room time, delay ms, feedback, noise)).
SCENES = (
    ("Parlour Saw Evening", (60, 0, 1, 250, 780, 0, 0, (300, 760, 300, 190, 0)),
     "C minor: warm saws and an organ-like pad over a heavy floor, a hushed piano between them.", (
        voice(T["Parlour Saw Pad"], 330, 340, 140, 8, held([60, 60, 63, 63, 58, 58, 63, 62], 72), 8),
        voice(T["Ember Organ Pad"], 250, 660, 160, 8, held([67, 67, 70, 67, 65, 65, 67, 67], 62), 8),
        voice(T["Hearth Bass"], 480, 480, 0, 4, [(0, 36, 80, 90, ALWAYS), (4, 39, 72, 90, ALWAYS), (8, 34, 76, 90, ALWAYS), (12, 31, 70, 86, SECOND)]),
        voice(VELVET_SUB, 230, 500, 0, 8, [(0, 36, 78, 100, ALWAYS), (4, 32, 70, 100, ALWAYS)], 8),
        voice(HUSH_PIANO, 205, 280, 210, 2, [(3, 72, 58, 80, ALWAYS), (10, 75, 52, 74, SECOND), (13, 79, 48, 70, FILL)]),
        voice(MIST_TICK, 65, 600, 240, 2, [(2, 60, 38, 30, ALWAYS), (7, 60, 32, 30, ALWAYS), (11, 60, 36, 30, SECOND)]),
     )),
    ("Honeyed Poly Room", (66, 2, 3, 230, 740, 0, 0, (290, 730, 310, 200, 0)),
     "D dorian: a soft poly pad and a glowing pad answer each other above a slow saw bass.", (
        voice(T["Honeyed Poly"], 320, 330, 120, 8, held([62, 62, 65, 65, 69, 69, 67, 65], 68), 8),
        voice(T["Cedar Glow Pad"], 255, 680, 170, 8, held([57, 57, 60, 60, 62, 64, 62, 60], 60), 8),
        voice(T["Slow Saw Bass"], 450, 500, 0, 4, [(0, 38, 82, 80, ALWAYS), (6, 41, 70, 76, ALWAYS), (10, 45, 74, 76, SECOND)]),
        voice(VELVET_SUB, 210, 470, 0, 8, [(0, 38, 76, 100, ALWAYS), (4, 36, 70, 100, ALWAYS)], 8),
        voice(HUSH_PIANO, 200, 290, 200, 2, [(2, 74, 58, 80, ALWAYS), (7, 77, 52, 72, ALWAYS), (12, 81, 50, 70, SECOND)]),
        voice(FINE_RAIN, 55, 700, 200, 1, [(2, 60, 38, 30, ALWAYS), (6, 60, 30, 28, ALWAYS), (10, 60, 34, 30, SECOND), (14, 60, 28, 28, ALWAYS)]),
     )),
    ("Cedar Glow Hours", (56, 5, 2, 260, 820, 0, 0, (310, 790, 330, 170, 0)),
     "F major: a slow glow of mellow pads over a sustained low hum, felt keys in the gaps.", (
        voice(T["Cedar Glow Pad"], 320, 320, 150, 8, held([53, 53, 57, 57, 58, 58, 57, 55], 70), 8),
        voice(T["Honeyed Poly"], 235, 690, 130, 8, held([65, 65, 64, 64, 65, 65, 60, 60], 58), 8),
        voice(T["Tape Sub Hum"], 280, 500, 0, 8, held([29, 29, 29, 29, 34, 34, 31, 31], 70), 8),
        voice(T["Hearth Bass"], 220, 460, 0, 8, [(0, 41, 74, 100, ALWAYS), (4, 46, 66, 100, ALWAYS)], 8),
        voice(LOW_WOOL_KEYS, 215, 270, 150, 2, [(3, 72, 58, 70, ALWAYS), (8, 77, 52, 66, ALWAYS), (13, 81, 48, 62, FILL)]),
        voice(OPEN_AIR, 55, 780, 290, 4, [(6, 60, 34, 40, ALWAYS), (14, 60, 30, 40, SECOND)]),
     )),
    ("Cellar Sub Garden", (52, 9, 1, 300, 900, -12, 0, (330, 830, 340, 180, 0)),
     "A minor: three low voices carry the floor, a soft saw pad and a distant piano float above.", (
        voice(T["Cellar Sub"], 640, 500, 0, 4, [(0, 33, 84, 90, ALWAYS), (4, 33, 72, 90, ALWAYS), (8, 36, 78, 90, ALWAYS), (12, 31, 70, 86, SECOND)]),
        voice(T["Tape Sub Hum"], 560, 450, 0, 8, held([33, 33, 33, 33, 31, 31, 28, 28], 70), 8),
        voice(T["Slow Saw Bass"], 460, 560, 0, 8, [(0, 33, 76, 100, ALWAYS), (4, 36, 68, 100, ALWAYS)], 8),
        voice(T["Parlour Saw Pad"], 110, 340, 200, 8, held([57, 57, 60, 60, 62, 62, 60, 59], 58), 8),
        voice(HUSH_PIANO, 90, 280, 240, 2, [(3, 69, 56, 80, ALWAYS), (9, 72, 50, 74, SECOND), (13, 76, 46, 70, FILL)]),
        voice(MIST_TICK, 55, 740, 260, 2, [(5, 60, 30, 30, ALWAYS), (13, 60, 26, 30, SECOND)]),
     )),
    ("Hearth Bass Lullaby", (58, 7, 3, 235, 760, 0, 20, (300, 760, 300, 210, 0)),
     "G dorian: a round bass line walks slowly under string glow and a felt piano lullaby.", (
        voice(T["Hearth Bass"], 620, 480, 0, 2, [(0, 31, 82, 80, ALWAYS), (3, 31, 64, 60, ALWAYS), (6, 34, 72, 70, ALWAYS), (10, 29, 70, 70, ALWAYS), (14, 26, 62, 60, SECOND)]),
        voice(T["Ensemble Glow"], 190, 330, 160, 8, held([55, 55, 58, 58, 57, 57, 55, 53], 64), 8),
        voice(T["Cello Parlour"], 235, 670, 120, 6, [(0, 50, 66, 100, ALWAYS), (6, 53, 60, 100, ALWAYS), (12, 55, 58, 100, SECOND)], 16),
        voice(HUSH_PIANO, 205, 290, 190, 2, [(2, 67, 62, 80, ALWAYS), (7, 70, 54, 72, ALWAYS), (11, 74, 56, 78, SECOND), (15, 70, 46, 70, FILL)]),
        voice(MIST_TICK, 55, 720, 220, 2, [(2, 60, 34, 30, ALWAYS), (6, 60, 28, 28, ALWAYS), (10, 60, 32, 30, ALWAYS)]),
        voice(DEEP_PILLOW, 300, 500, 0, 1, [(0, 36, 66, 60, ALWAYS), (8, 36, 52, 60, SECOND)]),
     )),
    ("Cello Parlour Night", (50, 3, 1, 280, 880, 0, 0, (320, 820, 330, 180, 0)),
     "E-flat minor: a low cello and a hazy viola carry slow arches over a deep floor.", (
        voice(T["Cello Parlour"], 330, 320, 130, 8, held([51, 51, 54, 54, 49, 49, 54, 53], 70), 8),
        voice(T["Viola Haze"], 255, 680, 170, 8, held([63, 63, 66, 66, 65, 65, 63, 61], 60), 8),
        voice(T["Ensemble Glow"], 220, 500, 150, 8, held([58, 58, 61, 61, 56, 56, 61, 60], 56), 8),
        voice(T["Cellar Sub"], 280, 490, 0, 8, [(0, 39, 78, 100, ALWAYS), (4, 34, 70, 100, ALWAYS)], 8),
        voice(T["Pizzicato Cedar"], 180, 260, 200, 2, [(3, 75, 60, 70, ALWAYS), (9, 78, 54, 66, SECOND), (14, 73, 50, 64, FILL)]),
        voice(OPEN_AIR, 55, 770, 290, 4, [(7, 60, 32, 40, ALWAYS), (15, 60, 28, 40, SECOND)]),
     )),
    ("Ensemble Glow Morning", (72, 0, 2, 220, 700, 0, 0, (280, 700, 280, 170, 0)),
     "C major: a bright-warm string ensemble and a viola answer, a plucked motif and a round bass below.", (
        voice(T["Ensemble Glow"], 320, 340, 120, 8, held([60, 60, 64, 64, 67, 67, 65, 64], 70), 8),
        voice(T["Viola Haze"], 245, 680, 150, 8, held([55, 55, 59, 59, 60, 60, 59, 57], 60), 8),
        voice(T["Cello Parlour"], 225, 500, 90, 8, [(0, 48, 70, 100, ALWAYS), (4, 52, 64, 100, ALWAYS)], 8),
        voice(T["Pizzicato Cedar"], 210, 270, 180, 1, [(0, 72, 72, 70, ALWAYS), (3, 76, 60, 64, ALWAYS), (6, 79, 66, 68, ALWAYS), (10, 76, 58, 62, ALWAYS), (14, 72, 54, 60, SECOND)]),
        voice(T["Hearth Bass"], 250, 480, 0, 4, [(0, 36, 76, 90, ALWAYS), (6, 43, 66, 80, ALWAYS), (10, 41, 68, 80, SECOND)]),
        voice(T["Honeyed Poly"], 150, 740, 200, 8, held([72, 72, 76, 76, 74, 74, 72, 71], 46), 8),
     )),
    ("Slow Saw Strings", (62, 4, 3, 270, 840, 0, 0, (310, 790, 320, 190, 0)),
     "E dorian: a slow saw bass, a warm saw pad and a low cello with a hazy viola above.", (
        voice(T["Slow Saw Bass"], 310, 490, 0, 4, [(0, 40, 82, 80, ALWAYS), (4, 43, 70, 76, ALWAYS), (8, 47, 74, 76, ALWAYS), (12, 45, 68, 72, SECOND)]),
        voice(T["Parlour Saw Pad"], 285, 340, 170, 8, held([64, 64, 67, 67, 71, 71, 69, 67], 66), 8),
        voice(T["Cello Parlour"], 245, 660, 120, 8, held([52, 52, 55, 55, 59, 59, 57, 55], 62), 8),
        voice(T["Viola Haze"], 210, 520, 160, 8, held([59, 59, 62, 62, 64, 64, 62, 60], 56), 8),
        voice(VELVET_SUB, 235, 470, 0, 8, [(0, 40, 76, 100, ALWAYS), (4, 36, 70, 100, ALWAYS)], 8),
        voice(MIST_TICK, 55, 750, 250, 2, [(2, 60, 32, 30, ALWAYS), (9, 60, 28, 28, SECOND), (13, 60, 30, 30, ALWAYS)]),
     )),
    ("Velvet Cello Cellar", (54, 2, 1, 270, 860, 0, 0, (320, 800, 330, 180, 0)),
     "D minor: a low cello and a hazy viola turn slowly over a cellar floor, a plucked note now and then.", (
        voice(T["Cello Parlour"], 330, 330, 120, 8, held([50, 50, 53, 53, 48, 48, 53, 52], 70), 8),
        voice(T["Viola Haze"], 250, 680, 160, 8, held([62, 62, 65, 65, 60, 60, 65, 64], 58), 8),
        voice(T["Cellar Sub"], 520, 490, 0, 4, [(0, 38, 82, 90, ALWAYS), (6, 34, 72, 90, ALWAYS), (10, 41, 74, 86, SECOND)]),
        voice(T["Pizzicato Cedar"], 175, 270, 200, 2, [(3, 74, 58, 70, ALWAYS), (9, 77, 52, 66, SECOND), (14, 72, 48, 62, FILL)]),
        voice(VELVET_SUB, 220, 470, 0, 8, [(0, 38, 76, 100, ALWAYS), (4, 36, 68, 100, ALWAYS)], 8),
        voice(OPEN_AIR, 55, 770, 290, 4, [(7, 60, 30, 40, ALWAYS), (15, 60, 26, 40, SECOND)]),
     )),
    ("Saw Lantern Hours", (64, 7, 2, 250, 780, 0, 0, (300, 760, 310, 190, 0)),
     "G major: a warm saw pad and a soft poly glow answer each other over a round bass.", (
        voice(T["Parlour Saw Pad"], 325, 340, 150, 8, held([55, 55, 59, 59, 62, 62, 60, 59], 68), 8),
        voice(T["Honeyed Poly"], 240, 690, 130, 8, held([67, 67, 71, 71, 69, 69, 67, 66], 58), 8),
        voice(T["Hearth Bass"], 440, 480, 0, 4, [(0, 31, 80, 90, ALWAYS), (4, 38, 70, 80, ALWAYS), (8, 36, 74, 80, ALWAYS), (12, 33, 68, 76, SECOND)]),
        voice(LOW_WOOL_KEYS, 205, 280, 170, 2, [(2, 79, 56, 70, ALWAYS), (8, 83, 50, 66, SECOND), (13, 86, 46, 62, FILL)]),
        voice(MIST_TICK, 60, 640, 230, 2, [(3, 60, 34, 30, ALWAYS), (11, 60, 30, 30, SECOND)]),
        voice(DEEP_PILLOW, 280, 500, 0, 1, [(0, 31, 62, 60, ALWAYS), (8, 31, 50, 60, SECOND)]),
     )),
    ("Low Organ Embers", (56, 7, 1, 260, 830, 0, 0, (310, 790, 320, 180, 0)),
     "G minor: an organ-like pad glows over a slow saw bass and a deep hum.", (
        voice(T["Ember Organ Pad"], 330, 330, 130, 8, held([55, 55, 58, 58, 53, 53, 58, 57], 70), 8),
        voice(T["Cedar Glow Pad"], 245, 680, 160, 8, held([67, 67, 70, 70, 65, 65, 70, 69], 58), 8),
        voice(T["Slow Saw Bass"], 430, 500, 0, 4, [(0, 31, 80, 80, ALWAYS), (6, 34, 70, 76, ALWAYS), (10, 29, 72, 76, SECOND)]),
        voice(T["Tape Sub Hum"], 300, 460, 0, 8, held([31, 31, 31, 31, 29, 29, 26, 26], 68), 8),
        voice(HUSH_PIANO, 190, 290, 210, 2, [(3, 70, 56, 80, ALWAYS), (9, 74, 50, 74, SECOND), (14, 67, 46, 70, FILL)]),
        voice(OPEN_AIR, 55, 780, 290, 4, [(6, 60, 30, 40, ALWAYS), (14, 60, 26, 40, SECOND)]),
     )),
    ("Pizzicato Rain Room", (70, 9, 3, 230, 720, 0, 0, (290, 720, 290, 200, 0)),
     "A dorian: plucked strings fall like drops over a string ensemble and a soft low floor.", (
        voice(T["Pizzicato Cedar"], 270, 270, 160, 1, [(0, 69, 70, 70, ALWAYS), (3, 72, 58, 64, ALWAYS), (6, 76, 64, 66, ALWAYS), (9, 74, 56, 62, ALWAYS), (13, 72, 52, 60, SECOND)]),
        voice(T["Ensemble Glow"], 300, 340, 120, 8, held([57, 57, 60, 60, 64, 64, 62, 60], 62), 8),
        voice(T["Cello Parlour"], 240, 520, 100, 8, [(0, 45, 68, 100, ALWAYS), (4, 48, 62, 100, ALWAYS)], 8),
        voice(T["Hearth Bass"], 360, 470, 0, 4, [(0, 33, 74, 90, ALWAYS), (6, 40, 64, 80, ALWAYS), (11, 38, 66, 80, SECOND)]),
        voice(FINE_RAIN, 60, 700, 210, 1, [(1, 60, 36, 30, ALWAYS), (5, 60, 30, 28, ALWAYS), (9, 60, 34, 30, SECOND), (13, 60, 28, 28, ALWAYS)]),
        voice(HUSH_PIANO, 180, 290, 190, 2, [(4, 81, 50, 74, SECOND), (11, 84, 46, 70, FILL)]),
     )),
    ("Deep Floor Choir", (50, 0, 1, 290, 900, -12, 0, (330, 840, 340, 170, 0)),
     "C minor: a sub, a hum and a string ensemble stand in a long low chord, a viola above.", (
        voice(T["Cellar Sub"], 620, 500, 0, 4, [(0, 36, 84, 90, ALWAYS), (4, 36, 72, 90, ALWAYS), (8, 39, 78, 90, ALWAYS), (12, 34, 70, 86, SECOND)]),
        voice(T["Tape Sub Hum"], 520, 450, 0, 8, held([36, 36, 36, 36, 34, 34, 31, 31], 70), 8),
        voice(T["Ensemble Glow"], 200, 340, 160, 8, held([48, 48, 51, 51, 46, 46, 51, 50], 62), 8),
        voice(T["Viola Haze"], 130, 680, 200, 8, held([60, 60, 63, 63, 58, 58, 63, 62], 52), 8),
        voice(VELVET_SUB, 250, 470, 0, 8, [(0, 36, 74, 100, ALWAYS), (4, 34, 66, 100, ALWAYS)], 8),
        voice(MIST_TICK, 55, 740, 260, 2, [(5, 60, 28, 30, ALWAYS), (13, 60, 24, 30, SECOND)]),
     )),
    ("Tape Hum Evening", (60, 10, 2, 250, 800, 0, 0, (300, 770, 320, 190, 0)),
     "B-flat major: a soft hum and warm pads close an evening, felt keys in the gaps.", (
        voice(T["Cedar Glow Pad"], 320, 330, 140, 8, held([58, 58, 62, 62, 65, 65, 63, 62], 68), 8),
        voice(T["Honeyed Poly"], 235, 690, 130, 8, held([70, 70, 74, 74, 72, 72, 70, 69], 56), 8),
        voice(T["Tape Sub Hum"], 340, 480, 0, 8, held([34, 34, 34, 34, 39, 39, 36, 36], 70), 8),
        voice(T["Hearth Bass"], 230, 470, 0, 8, [(0, 46, 72, 100, ALWAYS), (4, 41, 66, 100, ALWAYS)], 8),
        voice(LOW_WOOL_KEYS, 210, 270, 160, 2, [(3, 77, 56, 70, ALWAYS), (8, 82, 50, 66, ALWAYS), (13, 86, 46, 62, FILL)]),
        voice(OPEN_AIR, 55, 780, 290, 4, [(7, 60, 30, 40, ALWAYS), (15, 60, 26, 40, SECOND)]),
     )),
    ("Bowed Dawn Haze", (66, 5, 2, 240, 740, 0, 0, (290, 730, 300, 180, 0)),
     "F major: a hazy viola and a low cello open slowly, an ensemble swell and a round bass underneath.", (
        voice(T["Viola Haze"], 320, 340, 130, 8, held([65, 65, 69, 69, 72, 72, 70, 69], 66), 8),
        voice(T["Cello Parlour"], 250, 680, 120, 8, held([53, 53, 57, 57, 60, 60, 58, 57], 62), 8),
        voice(T["Ensemble Glow"], 200, 520, 160, 8, held([60, 60, 64, 64, 67, 67, 65, 64], 54), 8),
        voice(T["Hearth Bass"], 300, 480, 0, 4, [(0, 41, 74, 90, ALWAYS), (6, 48, 64, 80, ALWAYS), (11, 46, 66, 80, SECOND)]),
        voice(T["Pizzicato Cedar"], 175, 270, 180, 2, [(2, 77, 60, 68, ALWAYS), (9, 81, 52, 62, SECOND)]),
        voice(OPEN_AIR, 55, 770, 280, 4, [(6, 60, 30, 40, ALWAYS), (14, 60, 26, 40, SECOND)]),
     )),
    ("Round Bass Lanterns", (58, 4, 3, 255, 790, 0, 20, (300, 760, 310, 200, 0)),
     "E dorian: a round bass line walks under a saw pad and a felt piano, lanterns of plucked notes above.", (
        voice(T["Hearth Bass"], 600, 480, 0, 2, [(0, 40, 82, 80, ALWAYS), (3, 40, 64, 60, ALWAYS), (6, 43, 72, 70, ALWAYS), (10, 38, 70, 70, ALWAYS), (14, 35, 62, 60, SECOND)]),
        voice(T["Parlour Saw Pad"], 190, 340, 160, 8, held([52, 52, 55, 55, 59, 59, 57, 55], 62), 8),
        voice(T["Cedar Glow Pad"], 150, 690, 170, 8, held([64, 64, 67, 67, 71, 71, 69, 67], 54), 8),
        voice(HUSH_PIANO, 205, 290, 190, 2, [(2, 71, 60, 80, ALWAYS), (7, 74, 54, 72, ALWAYS), (11, 78, 56, 76, SECOND), (15, 74, 46, 70, FILL)]),
        voice(T["Pizzicato Cedar"], 150, 260, 200, 2, [(4, 83, 52, 62, ALWAYS), (10, 86, 46, 60, SECOND)]),
        voice(VELVET_SUB, 230, 470, 0, 8, [(0, 40, 72, 100, ALWAYS), (4, 36, 66, 100, ALWAYS)], 8),
     )),
    ("Honey Bass Waltz", (68, 8, 2, 240, 760, 0, 20, (300, 740, 310, 190, 0)),
     "A-flat major: a round walking bass and a string glow, plucked notes fall like a slow waltz above.", (
        voice(T["Hearth Bass"], 560, 480, 0, 2, [(0, 32, 82, 80, ALWAYS), (4, 39, 70, 70, ALWAYS), (8, 37, 74, 70, ALWAYS), (12, 34, 68, 66, SECOND)]),
        voice(T["Ensemble Glow"], 250, 340, 130, 8, held([56, 56, 60, 60, 63, 63, 61, 60], 64), 8),
        voice(T["Cello Parlour"], 210, 520, 110, 8, held([44, 44, 48, 48, 51, 51, 49, 48], 58), 8),
        voice(T["Pizzicato Cedar"], 190, 270, 180, 2, [(1, 75, 64, 64, ALWAYS), (5, 79, 56, 60, ALWAYS), (9, 72, 60, 62, SECOND), (13, 75, 50, 60, FILL)]),
        voice(HUSH_PIANO, 175, 290, 200, 2, [(3, 80, 56, 72, ALWAYS), (11, 84, 48, 68, SECOND)]),
        voice(MIST_TICK, 55, 740, 240, 2, [(2, 60, 30, 30, ALWAYS), (10, 60, 26, 28, SECOND)]),
     )),
    ("Slow Organ Prayer", (46, 11, 1, 280, 880, 0, 0, (320, 820, 330, 170, 0)),
     "B minor: an organ-like pad and a glow answer in long prayers over a saw bass and a deep hum.", (
        voice(T["Ember Organ Pad"], 330, 330, 120, 8, held([59, 59, 62, 62, 55, 55, 62, 61], 70), 8),
        voice(T["Cedar Glow Pad"], 240, 680, 160, 8, held([71, 71, 74, 74, 67, 67, 74, 73], 56), 8),
        voice(T["Slow Saw Bass"], 400, 500, 0, 4, [(0, 35, 80, 80, ALWAYS), (6, 31, 70, 76, ALWAYS), (10, 38, 72, 76, SECOND)]),
        voice(T["Tape Sub Hum"], 330, 460, 0, 8, held([35, 35, 35, 35, 31, 31, 38, 38], 68), 8),
        voice(HUSH_PIANO, 180, 290, 210, 2, [(3, 78, 54, 76, ALWAYS), (9, 74, 50, 70, SECOND), (14, 71, 46, 66, FILL)]),
        voice(OPEN_AIR, 55, 780, 290, 4, [(7, 60, 28, 40, ALWAYS), (15, 60, 24, 40, SECOND)]),
     )),
    ("Viola Lantern Dusk", (62, 2, 2, 250, 780, 0, 0, (300, 760, 310, 190, 0)),
     "D major: a hazy viola, a soft poly glow and a low cello light the dusk, a round bass underneath, rain far off.", (
        voice(T["Viola Haze"], 320, 340, 140, 8, held([62, 62, 66, 66, 69, 69, 67, 66], 66), 8),
        voice(T["Honeyed Poly"], 235, 690, 130, 8, held([74, 74, 78, 78, 76, 76, 74, 73], 56), 8),
        voice(T["Cello Parlour"], 225, 520, 100, 8, held([50, 50, 54, 54, 57, 57, 55, 54], 60), 8),
        voice(T["Hearth Bass"], 300, 480, 0, 4, [(0, 38, 76, 90, ALWAYS), (6, 45, 66, 80, ALWAYS), (10, 43, 68, 80, SECOND)]),
        voice(HUSH_PIANO, 185, 290, 190, 2, [(2, 81, 54, 72, ALWAYS), (9, 85, 48, 66, SECOND)]),
        voice(FINE_RAIN, 60, 700, 210, 1, [(1, 60, 32, 30, ALWAYS), (6, 60, 28, 28, ALWAYS), (11, 60, 30, 30, SECOND)]),
     )),
    ("Cedar Sub Chorale", (54, 6, 3, 270, 860, 0, 0, (310, 790, 320, 180, 0)),
     "F-sharp dorian: a cedar glow and a string ensemble sing a slow chorale over a sub and a low hum, felt keys between.", (
        voice(T["Cedar Glow Pad"], 320, 330, 140, 8, held([54, 54, 57, 57, 61, 61, 59, 57], 68), 8),
        voice(T["Ensemble Glow"], 240, 680, 150, 8, held([66, 66, 69, 69, 73, 73, 71, 69], 58), 8),
        voice(T["Tape Sub Hum"], 300, 470, 0, 8, held([30, 30, 30, 30, 35, 35, 33, 33], 68), 8),
        voice(T["Cellar Sub"], 420, 490, 0, 4, [(0, 30, 80, 90, ALWAYS), (4, 30, 70, 90, ALWAYS), (8, 33, 74, 90, ALWAYS), (12, 28, 66, 86, SECOND)]),
        voice(LOW_WOOL_KEYS, 200, 270, 160, 2, [(3, 78, 56, 70, ALWAYS), (8, 81, 50, 66, ALWAYS), (13, 85, 46, 62, FILL)]),
        voice(OPEN_AIR, 55, 780, 290, 4, [(6, 60, 28, 40, ALWAYS), (14, 60, 24, 40, SECOND)]),
     )),
)

def extend_voices_24(tones, biomes, tone_fields, family):
    assert len(tones) == FIRST_TONE and len(biomes) == FIRST_BIOME, "append after the 2.3 library"
    for name, model, tone, harm, shape, attack, decay, cutoff, resonance, breathe in TONES:
        assert resonance < 128
        tones.append(dict(id=len(tones), name=name, model=model, tone=tone, harm=harm, shape=shape, attack=attack,
                          decay=decay, cutoff=cutoff, resonance=resonance, filter=1, breathe=breathe))
    for index, (name, settings, intent, voices) in enumerate(SCENES):
        tempo, root, scale, space, size, pitch, swing, fx = settings
        scene = deepcopy(biomes[64])
        scene.update(id=len(biomes), name=name, family=family, tempo=tempo, seed=0x24A10000 + index * 65537, root=root,
                     scale=scale, macro=[500, 0, 430, space], grain_size=size, grain_pos=(260 + index * 71) if index < 8 else (300 + (index - 8) * 83) % 900,
                     grain_pitch=pitch, feedback=150 + index * 23, reverse=0, swing=swing, locked=1, quality=1,
                     intent=intent, introduced="2.4", sound_family=FAMILY, rule_fx=fx, rule_lengths=[], rule_events=[])
        add_voices(scene, voices, tones, tone_fields, 1)
        biomes.append(scene)
    assert len(tones) == FIRST_TONE + 12 and len(biomes) == FIRST_BIOME + 20
    assert len({tuple(t[k] for k in tone_fields) for t in tones[-12:]}) == 12
    assert {i for b in biomes[-20:] for i in b['tones'] if i >= FIRST_TONE} == set(range(FIRST_TONE, FIRST_TONE + 12))
