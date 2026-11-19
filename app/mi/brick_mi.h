/* SPDX-License-Identifier: GPL-3.0-only
 * Mutable Dreams for Trimui Brick: the C side of brick_mi.cpp (original Mutable Instruments DSP, MIT). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int mode;                                    /* 0 granular, 1 stretch, 2 looping delay, 3 spectral */
    int lofi, freeze;
    float position, size, pitch, density, texture, spread, feedback, reverb;   /* 0..1; pitch in semitones */
} mi_particles_params;
void mi_particles_init(void);
void mi_particles_process(const mi_particles_params *p, const float *in, float *out, unsigned n);   /* stereo */

typedef struct { float algorithm, timbre, drive_carrier, drive_modulator; } mi_crossmod_params;   /* 0..1 */
void mi_crossmod_init(void);
void mi_crossmod_process(const mi_crossmod_params *p, const float *carrier, const float *modulator, float *out,
                         unsigned n);    /* mono in, mono out */

typedef struct {
    int media, mono, freeze;                     /* media: 0 dirty tape, 1 clean tape, 2 vintage digital, 3 digital */
    float density, time, size, shape, pitch;     /* 0..1 (density 0.5 = no grains); pitch in semitones */
    float time_random, size_random, shape_random, pitch_random;   /* -1..1 */
    float feedback, reverb;                      /* 0..1 */
} mi_beads_params;
void mi_beads_init(void);
void mi_beads_process(const mi_beads_params *p, const float *in, float *out, unsigned n);   /* stereo */
float mi_beads_reserve(void);

typedef struct { float note, harmonics, timbre, morph, decay, lpg_colour; int engine; } mi_plaits_patch;   /* 0..1 */
void mi_plaits_init(void);
/* voice v (0..7): n frames of its main and aux outputs; level 0..1 (the envelope), gate: the note is held,
 * retrigger: a new note on a voice that was still held (one frame low first) */
void mi_plaits_render(int v, const mi_plaits_patch *p, float level, int gate, int retrigger, float *out, float *aux,
                      unsigned n);

typedef struct { float deja_vu, spread, bias, probability; int length; } mi_random_params;
void mi_random_init(uint32_t seed);
int mi_random_step(const mi_random_params *p, float *value);
int mi_random2_step(const mi_random_params *p, float *value);   /* a second, separate Marbles (VOICES) */

/* Rings: resonator i (0, 1 the FX RACKs, 2 SYNTH, 3 spare); in may be 0 (then only strums ring it) */
typedef struct {
    int model, polyphony, chord;                 /* model 0 modal, 1 sympathetic strings, 2 string, 3 FM voice,
                                                    4 sympathetic quantized, 5 string and reverb; chord 0..10 */
    float note;                                  /* MIDI note */
    float structure, brightness, damping, position;   /* 0..1 */
    int internal_exciter, strum_by_note, strike; /* strike: strum now (with strum_by_note) */
} mi_rings_params;
void mi_rings_process(int i, const mi_rings_params *p, const float *in, float *out_l, float *out_r, unsigned n);

/* Elements: the one modal voice (at 32 kHz inside) */
typedef struct {
    int model, gate;                             /* model 0 modal, 1 string, 2 strings */
    float note, strength;                        /* MIDI note; 0..1 */
    float contour, bow, bow_timbre, blow, flow, blow_timbre, strike, mallet, strike_timbre;   /* 0..1 */
    float geometry, brightness, damping, position, space;   /* 0..1 */
} mi_elements_params;
void mi_elements_process(const mi_elements_params *p, float *out_l, float *out_r, unsigned n);

/* Tides 2: the looping slope generator (control range) as a modulator, 0..1; hz its frequency; slope, shape,
   smooth 0..1 as on the module */
float mi_tides_render(int voice, float hz, float slope, float shape, float smooth, unsigned n);
void mi_tides_preview(float slope, float shape, float smooth, float *pts, int npts);   /* one cycle, for drawing */

#ifdef __cplusplus
}
#endif
