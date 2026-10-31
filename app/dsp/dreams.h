/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { MD_TRACKS=6, MD_MODELS=15, MD_RATE=44100, MD_QUANTUM=64, MD_V13_BIOMES=109, MD_BIOMES=157, MD_TONE_PRESETS=352, MD_BLANK_BIOME=64, MD_STEPS=32 };
enum { MD_KICK, MD_SNARE, MD_HAT, MD_FM, MD_VA, MD_RINGS, MD_HARMONIC, MD_CHOIR, MD_BASS, MD_ELEMENTS, MD_MELLOW, MD_SATIN, MD_VELVET_KEYS, MD_SPECTRAL, MD_BREATH };
enum { MD_MODEL, MD_TONE, MD_HARM, MD_DECAY, MD_LEVEL, MD_SEND, MD_HITS,
 MD_LENGTH, MD_ROTATE, MD_PROB, MD_NOTE, MD_PAN, MD_MUTE, MD_SPEED,
 MD_MACRO, MD_TEMPO, MD_PLAY, MD_LOCK, MD_MUTATE, MD_RETURN, MD_SCENE,
 MD_FREEZE, MD_GRAIN_SIZE, MD_GRAIN_POS, MD_FEEDBACK, MD_QUALITY, MD_TRIGGER, MD_PANIC,
 MD_ATTACK,MD_SWING,MD_RATCHET,MD_ROOT,MD_SCALE,MD_TRANSPOSE,MD_GRAIN_PITCH,
 MD_REVERSE,MD_DUCK_SOURCE,MD_DUCK,MD_DUCK_RELEASE,MD_ACCENT,MD_SHAPE,MD_CUTOFF,MD_RESONANCE,MD_FILTER,MD_BREATHE,MD_TONE_PRESET,MD_DELAY_SEND,MD_REVERB_SEND,MD_DELAY_TIME,MD_DELAY_FB,MD_REVERB_TIME,MD_FX_LEVEL,MD_BASS_MODE,MD_BASS_PATTERN,MD_GLIDE,MD_MARBLES,MD_TIDES,MD_TIDES_RATE,MD_WARPS,MD_BASS_STEP,MD_BASS_VALUE,MD_CLOCK_SOURCE,MD_MIDI_QUANTIZE,MD_GRAIN_NOISE,MD_SEQ_ENABLE,MD_SEQ_STEP,MD_SEQ_NOTE,MD_SEQ_VELOCITY,MD_SEQ_CHANCE,MD_SEQ_GATE,MD_SEQ_GENERATE,MD_SEQ_LENGTH,MD_RECORD,MD_CHORD_PATTERN,MD_CHORD_BARS,MD_CHORD_TRACKS,MD_NOTE_OFF,MD_CHORD_FOLLOW,MD_SCENE_CONFIRM,MD_KIT_MODE,MD_KIT_BANK,MD_KIT_PAD,MD_KIT_STEP,MD_KIT_GATE,MD_KIT_TUNE,MD_KIT_LEVEL,MD_KIT_CUTOFF,MD_KIT_SEND,MD_KIT_DECAY,MD_KIT_HISS,MD_KIT_GENERATE,MD_KIT_LENGTH,MD_KIT_TRIGGER,MD_SEQ_DIRECTION,MD_SEQ_FEEL,MD_SEQ_SHIFT,MD_LFO_TARGET,MD_LFO_SHAPE,MD_CHORD_COMPACT,MD_LFO2_TARGET=120,MD_LFO2_SHAPE,MD_LFO2_DEPTH,MD_LFO2_RATE,MD_MIDI_ROUTE=125,MD_MIDI_MACRO,MD_COMMAND_COUNT };
typedef struct { int32_t model,tone,harm,decay,level,send,hits,length,rotate,prob,note,pan,mute,speed,attack,ratchet,accent,shape,cutoff,resonance,filter,breathe; } md_track;
typedef struct { int32_t delay_send,reverb_send,bass_mode,pattern,glide,marbles,tides,tides_rate,warps,edit_step; int32_t notes[16]; } md_extension;
/* Separate compact sequence storage: sound slots never carry these 32 steps.
 * note 0 = rest; otherwise absolute MIDI 24..96. Percent fields are 0..100. */
typedef struct { uint8_t note,velocity,chance,gate; } md_step;
typedef struct { uint8_t enabled,length,edit,generator; md_step steps[MD_STEPS]; } md_sequence;
typedef struct { int16_t tune; uint16_t level,cutoff,send,decay; } md_pad;
typedef struct { uint8_t enabled,bank,pad,step,length,generator,hiss,reserved; md_pad pads[8]; uint32_t gates[8]; } md_kit;
typedef struct { uint8_t direction[6],feel[6],shift[6],target[6],wave[6],compact,reserved; uint8_t target2[6],wave2[6]; uint16_t depth2[6],rate2[6]; } md_motion;
typedef struct {
 uint32_t magic,version,seed; int32_t macro[4],tempo,locked,freeze,grain_size,grain_pos,feedback,quality;
 md_track track[MD_TRACKS];
 int32_t root,scale,transpose,swing,grain_pitch,reverse,duck_source,duck_amount,duck_release;
 int32_t biome_id,tone_preset[6]; uint32_t edited_mask;
 md_extension ext[6]; int32_t delay_ms,delay_fb,reverb_time,fx_level;
 /* v7 tail; the complete 1292-byte v6 prefix remains readable. */
 md_sequence sequence[6]; int32_t grain_noise,chord_pattern,chord_bars,chord_tracks;
 /* v8 tail; preserve every byte of the 2100-byte v7 prefix. */
 md_kit kit;
 /* v9: full 2220-byte v8 prefix is retained. Defaults leave old sounds alone. */
 md_motion motion;
} md_patch;
typedef struct { uint32_t frames,step,events[6],dropped_commands,nonfinite; float peak; uint32_t grain_writes,voice_quanta,skipped_quanta; int32_t last_note[6]; uint32_t quality_reductions,element_modes; float track_peak[6]; } md_stats;
void md_init(void);
int md_command(int command,int track,int value);
unsigned md_command_space(void);
void md_midi(unsigned status,unsigned a,unsigned b,int port); /* audio owner only */
int md_clock_source(void);
int md_midi_quantize(void);
int md_midi_route(unsigned track); /* 0 = Off, 1..16 = channel */
int md_midi_macro_enabled(void);
void md_set_midi_route(unsigned track,unsigned channel); /* boot/restore with transport stopped */
void md_set_midi_macro_enabled(int enabled);
int md_record_mode(void);
int md_record_track(void);
const char *md_chord_name(int pattern);
int md_chord_degree(int pattern,int position);
unsigned md_sequence_position(unsigned track,unsigned local_step);
const char *md_generator_name(int generator);
const char *md_direction_name(int direction);
const char *md_lfo_target_name(int target);
const char *md_lfo_wave_name(int wave);
void md_render(float *stereo,unsigned n);
const md_patch *md_state(void);
const md_stats *md_status(void);
int md_playing(void);
int md_validate(const md_patch *p);
int md_load(const md_patch *p); /* only while transport stopped, at the audio owner */
int md_decode(md_patch *out,const void *p,unsigned bytes); /* pure version migration + validation */
int md_import(const void *p,unsigned bytes); /* v1 through v9 saved patches, stopped only */
int md_import_edit(const void *p,unsigned bytes); /* sound recall keeps unsaved Biome baseline */
unsigned md_memory_bytes(void);
int md_unsaved(void);
void md_mark_saved(void); /* caller holds control lock, only after successful write */
int md_pending_biome(void);
int md_active_tracks(void);
const char *md_kit_name(int bank);
const char *md_pad_name(int pad);
int md_has_envelope(int model);
int md_cutoff_hz(int value);
int md_hit(unsigned step,unsigned hits,unsigned length,unsigned rotate);
const char *md_model_name(int model);
const char *md_biome_name(int id);
int md_biome_step(int current,int delta); /* curated wheel order; stored IDs stay stable */
const char *md_biome_family(int id);
const char *md_tone_name(int id);
int md_tone_model(int id);
const char *md_scale_name(int scale);
int md_quantize(int note,int root,int scale);
#ifdef __cplusplus
}
#endif
