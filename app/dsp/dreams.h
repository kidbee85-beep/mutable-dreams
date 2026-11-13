/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { MD_TRACKS=6, MD_MODELS=24, MD_RATE=44100, MD_QUANTUM=64, MD_V13_BIOMES=109, MD_V15_BIOMES=169, MD_WARM_FIRST_BIOME=213, MD_WARM_FIRST_TONE=452, MD_RULE_FIRST_BIOME=233, MD_MALLET_FIRST_BIOME=245, MD_ENSEMBLE_FIRST_BIOME=248, MD_VOWEL_FIRST_BIOME=257, MD_SOFT_FIRST_BIOME=261, MD_LAYERS_FIRST_BIOME=269, MD_BIOMES=272, MD_PATCH_VERSION=12, MD_FELT_FIRST_TONE=516, MD_TONE_PRESETS=632, MD_BLANK_BIOME=64, MD_STEPS=32 };
enum { MD_KICK, MD_SNARE, MD_HAT, MD_FM, MD_VA, MD_RINGS, MD_HARMONIC, MD_CHOIR, MD_BASS, MD_ELEMENTS, MD_MELLOW, MD_SATIN, MD_VELVET_KEYS, MD_SPECTRAL, MD_BREATH, MD_CEDAR, MD_REED, MD_SKIN, MD_BOWED, MD_AERO, MD_FELT, MD_SILK, MD_COPPER, MD_VOWEL };
enum { MD_MODEL, MD_TONE, MD_HARM, MD_DECAY, MD_LEVEL, MD_SEND, MD_HITS,
 MD_LENGTH, MD_ROTATE, MD_PROB, MD_NOTE, MD_PAN, MD_MUTE, MD_SPEED,
 MD_MACRO, MD_TEMPO, MD_PLAY, MD_LOCK, MD_MUTATE, MD_RETURN, MD_SCENE,
 MD_FREEZE, MD_GRAIN_SIZE, MD_GRAIN_POS, MD_FEEDBACK, MD_QUALITY, MD_TRIGGER, MD_PANIC,
 MD_ATTACK,MD_SWING,MD_RATCHET,MD_ROOT,MD_SCALE,MD_TRANSPOSE,MD_GRAIN_PITCH,
 MD_REVERSE,MD_DUCK_SOURCE,MD_DUCK,MD_DUCK_RELEASE,MD_ACCENT,MD_SHAPE,MD_CUTOFF,MD_RESONANCE,MD_FILTER,MD_BREATHE,MD_TONE_PRESET,MD_DELAY_SEND,MD_REVERB_SEND,MD_DELAY_TIME,MD_DELAY_FB,MD_REVERB_TIME,MD_FX_LEVEL,MD_BASS_MODE,MD_BASS_PATTERN,MD_GLIDE,MD_MARBLES,MD_TIDES,MD_TIDES_RATE,MD_WARPS,MD_BASS_STEP,MD_BASS_VALUE,MD_CLOCK_SOURCE,MD_MIDI_QUANTIZE,MD_GRAIN_NOISE,MD_SEQ_ENABLE,MD_SEQ_STEP,MD_SEQ_NOTE,MD_SEQ_VELOCITY,MD_SEQ_CHANCE,MD_SEQ_GATE,MD_SEQ_GENERATE,MD_SEQ_LENGTH,MD_RECORD,MD_CHORD_PATTERN,MD_CHORD_BARS,MD_CHORD_TRACKS,MD_NOTE_OFF,MD_CHORD_FOLLOW,MD_SCENE_CONFIRM,MD_KIT_MODE,MD_KIT_BANK,MD_KIT_PAD,MD_KIT_STEP,MD_KIT_GATE,MD_KIT_TUNE,MD_KIT_LEVEL,MD_KIT_CUTOFF,MD_KIT_SEND,MD_KIT_DECAY,MD_KIT_HISS,MD_KIT_GENERATE,MD_KIT_LENGTH,MD_KIT_TRIGGER,MD_SEQ_DIRECTION,MD_SEQ_FEEL,MD_SEQ_SHIFT,MD_LFO_TARGET,MD_LFO_SHAPE,MD_CHORD_COMPACT,MD_LFO2_TARGET=120,MD_LFO2_SHAPE,MD_LFO2_DEPTH,MD_LFO2_RATE,MD_MIDI_ROUTE=125,MD_MIDI_MACRO,MD_JOURNEY_MODE=128,MD_JOURNEY_LENGTH,MD_JOURNEY_BIOME,MD_JOURNEY_BARS,MD_JOURNEY_REPEATS,MD_JOURNEY_TRANSPOSE,MD_JOURNEY_KEEP,MD_JOURNEY_KEEP_TRACK,MD_LEARN_TARGET,MD_LEARN_TRACK,MD_LEARN_ARM,MD_LEARN_CLEAR,MD_LEARN_PORT,MD_LEARN_CHANNEL,MD_LEARN_CC,MD_LEARN_ENABLE,MD_LINK_SOURCE,MD_LINK_SIGNAL,MD_LINK_DEST,MD_LINK_TARGET,MD_LINK_DEPTH,MD_LINK_RELEASE,MD_DRIFT_MODE,MD_DRIFT_POSITION,MD_DRIFT_RATE,MD_DRIFT_CAPTURE,MD_DRIFT_DEPTH,MD_SEQ_COPY,MD_SEQ_PASTE,MD_SEQ_TRANSPOSE,MD_SEQ_MIRROR,MD_SEQ_RULE,MD_FILL,MD_SEQ_EXPRESSION,MD_ITINERA_START,MD_COMMAND_COUNT };
typedef struct { int32_t model,tone,harm,decay,level,send,hits,length,rotate,prob,note,pan,mute,speed,attack,ratchet,accent,shape,cutoff,resonance,filter,breathe; } md_track;
typedef struct { int32_t delay_send,reverb_send,bass_mode,pattern,glide,marbles,tides,tides_rate,warps,edit_step; int32_t notes[16]; } md_extension;
/* v12 preserves the four-byte step: chance and gate each supply one high bit.
 * Notes and the full 0..100 percent controls retain their original resolution. */
typedef struct { uint8_t note,velocity,chance,gate; } md_step;
enum { MD_RULE_ALWAYS, MD_RULE_FIRST, MD_RULE_SECOND, MD_RULE_FILL };
static inline unsigned md_step_chance(const md_step *s){return s->chance&127u;}
static inline unsigned md_step_gate(const md_step *s){return s->gate&127u;}
static inline unsigned md_step_rule(const md_step *s){return (s->chance>>7)|((s->gate>>6)&2u);}
static inline void md_step_set_rule(md_step *s,unsigned rule){
 s->chance=md_step_chance(s)|((rule&1u)<<7);
 s->gate=md_step_gate(s)|((rule&2u)<<6);
}
typedef struct { uint8_t enabled,length,edit,generator; md_step steps[MD_STEPS]; } md_sequence;
typedef struct { int16_t tune; uint16_t level,cutoff,send,decay; } md_pad;
typedef struct { uint8_t enabled,bank,pad,step,length,generator,hiss,reserved; md_pad pads[8]; uint32_t gates[8]; } md_kit;
typedef struct { uint8_t direction[6],feel[6],shift[6],target[6],wave[6],compact,reserved; uint8_t target2[6],wave2[6]; uint16_t depth2[6],rate2[6]; } md_motion;
/* v10: eight sections. 65535 means the complete home User Biome.
 * keep is an incoming-section bit mask. No audio or flash addresses are stored. */
typedef struct { uint16_t biome; uint8_t bars,repeats,keep; int8_t transpose; uint8_t reserved[2]; } md_journey_section;
typedef struct { uint8_t mode,length,reserved[2]; md_journey_section section[8]; } md_journey;
enum { MD_MIDI_BINDINGS=16, MD_LINKS=4 };
/* Global controller preferences; not part of a Biome. port3/channel0 mean Any. */
typedef struct { uint8_t enabled,port,channel,cc,target,track,reserved[2]; } md_midi_mapping;
/* Bounded, control-rate links: Hit / Envelope / LFO A / LFO B. */
typedef struct { uint8_t source,signal,destination,target; int16_t depth; uint16_t release; } md_link;
/* Captured controls: tone, cutoff, decay, level, pan, grain, delay, room. */
typedef struct { uint16_t macro[4],track[6][8]; } md_drift_scene;
typedef struct { uint16_t position,rate,depth; uint8_t mode,captured; md_drift_scene scene[2]; } md_drift;
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
 /* v10 retains the complete 2288-byte v9 prefix. */
 md_journey journey;
 /* v11 retains the complete 2356-byte v10 prefix. */
 md_link links[MD_LINKS]; md_drift drift;
} md_patch;
typedef struct { uint32_t frames,step,events[6],dropped_commands,nonfinite; float peak; uint32_t grain_writes,voice_quanta,skipped_quanta; int32_t last_note[6]; uint32_t quality_reductions,element_modes; float track_peak[6]; } md_stats;
void md_init(void);
int md_command(int command,int track,int value);
unsigned md_command_space(void);
int md_command_valid(int command);
int md_command_allowed(int command); /* temporary performance controls remain live */
const md_patch *md_project_state(void); /* canonical project, even during Journey */
enum { MD_ITINERA_PRESETS=14 };
typedef struct { unsigned remaining, section, progress, preset, chapter, energy; int biome, active; } md_itinera_view;
void md_itinera_read(md_itinera_view *view);
const char *md_itinera_name(unsigned preset);
unsigned md_itinera_defaults(unsigned preset);
int md_journey_active(void);
int md_journey_position(void); /* zero-based section, -1 while stopped */
unsigned md_journey_bar(void); /* one-based bar in this repeat */
unsigned md_journey_repeat(void); /* one-based repeat */
unsigned md_journey_step(void); /* zero-based step within the current section */
int md_preview_biome_kit(md_patch *scratch,int biome); /* UI-owned scratch */
typedef struct { uint32_t seed; uint8_t tempo,root,scale,family; } md_biome_card;
int md_biome_card_info(int id,md_biome_card *card);
void md_grain_positions(uint8_t position[4],uint8_t window[4]); /* caller holds audio lock */
void md_midi(unsigned status,unsigned a,unsigned b,int port); /* audio owner only */
/* F-2: audio owner. A callback's block begins (frame 0); then each message at
 * the frame of that block where it belongs (its arrival, one block later):
 * md_render plays it exactly there. Frame 0 or a full list: at once. */
void md_block_begin(void);
void md_midi_at(unsigned status,unsigned a,unsigned b,int port,unsigned frame);
void md_midi_lost(int port); /* release notes held by a disconnected transport */
int md_clock_source(void);
int md_midi_quantize(void);
int md_midi_route(unsigned track); /* 0 = Off, 1..16 = channel */
int md_midi_held_port(unsigned track); /* -1 for none; transport of the current held note */
int md_midi_macro_enabled(void);
float md_midi_bend_range(unsigned track); /* RPN 0,0; 0..24 semitones, default 2 */
void md_midi_performance_state(unsigned track,int32_t out[7]); /* bounded read-only editor diagnostics */
float md_midi_bend(unsigned track); /* current target bend, before pitch smoothing */
int md_mapping_valid(const md_midi_mapping *m);
const md_midi_mapping *md_mapping(unsigned slot);
int md_set_mapping(unsigned slot,const md_midi_mapping *m); /* stopped boot/restore */
unsigned md_mapping_generation(void);
int md_learn_armed(void); /* slot or -1 */
int md_learn_picked(unsigned slot);
const char *md_learn_target_name(int target);
int md_next_learn_cc(int current,int delta);
int md_drift_ready(void);
int md_drift_position(void);
float md_link_value(unsigned link);
const char *md_link_target_name(int target);
const char *md_link_signal_name(int signal);
void md_set_midi_route(unsigned track,unsigned channel); /* boot/restore with transport stopped */
void md_set_midi_macro_enabled(int enabled);
int md_record_mode(void);
int md_record_track(void);
const char *md_chord_name(int pattern);
int md_chord_degree(int pattern,int position);
unsigned md_sequence_position(unsigned track,unsigned local_step);
unsigned md_fill_status(void); /* bit 0 active, bit 1 queued for the next bar */
int md_sequence_playhead(unsigned track,int *hit); /* caller holds audio lock */
enum { MD_SEQ_COPIED=1,MD_SEQ_PASTED,MD_SEQ_TRANSPOSED,MD_SEQ_MIRRORED,MD_SEQ_WRONG_CLIP,MD_SEQ_NOTE_LIMIT,MD_SEQ_NO_NOTES,MD_SEQ_UNAVAILABLE,MD_SEQ_ACCENTED,MD_SEQ_ARTICULATED,MD_SEQ_NO_CHANGE };
enum { MD_EXPRESSION_ACCENT, MD_EXPRESSION_GATE, MD_EXPRESSION_GATE_OFFSET=128 };
int md_phrase_value(const md_step *step,unsigned index,int mode,int amount);
unsigned md_sequence_clipboard(void); /* 0 empty, 1 step, 2 phrase */
uint32_t md_sequence_notice(void); /* serial in high bits, result in low four */

const char *md_generator_name(int generator);
const char *md_direction_name(int direction);
const char *md_lfo_target_name(int target);
const char *md_lfo_wave_name(int wave);
void md_render(float *stereo,unsigned n);
#if defined(MD_PORT_PERFORMANCE)
/* Audio-owner controls. These never change a saved patch or Journey. */
void md_performance_set(float room,float echo,float texture,float dusk);
#endif
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
/* Six mono post-fader, pre-pan/pre-bus USB stems at fixed level. */
typedef struct {
 int16_t *first, *wrapped;
 unsigned first_frames;
} md_usb_stems;
void md_render_stems(float *stereo,unsigned n,md_usb_stems stems);
#endif
const md_patch *md_state(void);
int md_tempo_now(void);      /* F-6: the tempo that times steps now: an external clock's, else the project's */
int md_quality_active(void); /* FULL now: the setting, unless the admission guard or an overload took ECO */
void md_overload(void);      /* audio owner: the render ran late; ECO until the project or quality changes */
const md_stats *md_status(void);
int md_playing(void);
int md_validate(const md_patch *p);
int md_load(const md_patch *p); /* only while transport stopped, at the audio owner */
int md_decode(md_patch *out,const void *p,unsigned bytes); /* pure version migration + validation */
int md_import(const void *p,unsigned bytes); /* v1 through v11 saved patches, stopped only */
int md_import_edit(const void *p,unsigned bytes); /* sound recall keeps unsaved Biome baseline */
unsigned md_memory_bytes(void);
int md_unsaved(void);
void md_mark_saved(void); /* caller holds control lock, only after successful write */
/* Mark exactly the image that was written as saved; edits that arrive while
 * flash is busy stay unsaved. Caller holds the control lock. */
void md_mark_saved_from(const md_patch *written);
int md_pending_biome(void);
int md_active_tracks(void);
const char *md_kit_name(int bank);
const char *md_pad_name(int pad);
int md_has_envelope(int model);
int md_model_first_tone(int model);
int md_cutoff_hz(int value);
int md_hit(unsigned step,unsigned hits,unsigned length,unsigned rotate);
const char *md_model_name(int model);
const char *md_biome_name(int id);
int md_biome_step(int current,int delta); /* curated wheel order; stored IDs stay stable */
int md_biome_at(unsigned position);
int md_biome_family_index(int id);
unsigned md_biome_family_count(void);
const char *md_biome_family_name(unsigned index);
void md_set_velocity_touch(unsigned percent); /* caller holds the audio lock */
const char *md_biome_family(int id);
const char *md_tone_name(int id);
int md_tone_model(int id);
const char *md_scale_name(int scale);
int md_quantize(int note,int root,int scale);
#ifdef __cplusplus
}
#endif
