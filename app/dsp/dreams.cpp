/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Mutable Dreams. Original Mutable Instruments DSP remains under its MIT notices.
 * Six interchangeable monophonic slots. No allocations in render. */
#include "dreams.h"
#include "elements/dsp/exciter.h"
#include "elements/dsp/resonator.h"
#include <math.h>
#include <string.h>
#include <new>
#include <stddef.h>
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/drums/analog_bass_drum.h"
#include "plaits/dsp/drums/analog_snare_drum.h"
#include "plaits/dsp/drums/hi_hat.h"
#include "rings/dsp/resonator.h"
#include "plaits/dsp/engine/additive_engine.h"
#include "plaits/dsp/oscillator/sine_oscillator.h"
#include "stmlib/dsp/filter.h"
#include "stmlib/utils/random.h"
#include "md_ram.h"
#if defined(MD_MIX_DEVICE) && defined(PLAT_HOST) && !defined(MD_MIX_DEVICE_TEST)
#undef MD_MIX_DEVICE
#endif
#ifdef MD_MIX_DEVICE
#define MD_MIX_PIPELINE 1
#if !defined(FM1B_UAC_CH) || FM1B_UAC_CH != 8
#error "FM-1 device mix backend requires the eight-channel stem and abort path"
#endif
extern "C" {
#include "plat.h" // platform implementation is C; do not inherit linkage from host mocks
}
#endif
#if defined(MD_MIX_PIPELINE) && defined(MD_PROFILE)
#error "Pipeline stage timing needs a separate profiler; legacy per-voice timing is not valid for overlap"
#endif
#ifdef MD_MIX_PIPELINE
#if !defined(PLAT_HOST) && !defined(MD_MIX_DEVICE)
#error "Device pipeline requires the actual FM-1 backend"
#endif
#ifdef MD_MIX_DEVICE
extern "C" {
int md_mix_worker_available_device(void);
int md_mix_worker_ready_device(void);
int md_mix_worker_submit_device(void *);
int md_mix_worker_wait_device(void);
int md_mix_worker_failed_device(void);
void md_mix_worker_reject_device(void);
void md_mix_worker_retire_device(void);
void md_mix_worker_test_begin(void);
void md_mix_worker_job_begin(void);
void md_mix_worker_job_end(void);
unsigned md_mix_worker_inject_device(void);
extern unsigned md_mix_min_frames;
}
static inline bool md_mix_join(){return md_mix_worker_wait_device()==0;}
#else
extern "C" int md_mix_worker_submit(void (*fn)(void *),void *arg);
extern "C" void md_mix_worker_wait(void);
static inline bool md_mix_join(){md_mix_worker_wait();return true;}
#endif
#endif
// Native UI/WASM builds use the serial reference; concurrency probes opt in
// to an explicit host backend, just as the existing device mixer does.
#if defined(MD_SOURCE_MIX_PIPELINE) && defined(PLAT_HOST) && !defined(MD_MIX_PIPELINE) && !defined(MD_MIX_DEVICE_TEST)
#undef MD_SOURCE_MIX_PIPELINE
#endif
#if defined(MD_SOURCE_MIX_PIPELINE) && !defined(MD_MIX_PIPELINE)
#error "MD_SOURCE_MIX_PIPELINE needs the ordered single-worker mix pipeline"
#endif
/* MD_PROFILE (measuring tree only, never in a release): real time per stage of every render callback, from
 * plat_ticks24 (24 ticks a microsecond). Outside the RAM functions. The peak callbacks are kept with their context. */
#ifdef MD_PROFILE
extern "C" uint32_t plat_ticks24(void);
struct ProfRec{uint32_t total,ctl,tick,pre,vox,misc,fx,fin,voice[6],c_scene,c_misc,c_step,c_part,c_tempo,reads,models;};
/* Ablation (measuring only; the sound changes): a runtime mask (SysEx 24) switches parts of the effects off to measure what they cost:
 * 1 grains, 2 echo, 4 room, 16 width, 32 chorus, 64 air, 128 trim. 0 = the normal sound. */
static volatile uint32_t prof_ablate;
#define ABL(b) ((prof_ablate>>(b))&1u)
static struct Prof{
 uint32_t acc_q,acc_tick,acc_pre,acc_vox,acc_misc,acc_fx,acc_fin,acc_voice[6],reads,quanta,frames,active_mask;
 uint32_t overhead,blocks,over90,scene,next_scene,part,top_used,cb0,calls;ProfRec top[4];
 uint64_t sum[8],vsum[6];   /* since the reset: total ctl pre vox misc fx fin tick, and each voice, in ticks */
}prof;
static inline uint32_t pt(){prof.reads++;return plat_ticks24();}
#define PT_START(v) const uint32_t v=pt()
#define PT_ADD(f,v) (prof.f+=pt()-(v))
#define PROF_NEXT(id) (prof.next_scene=(uint32_t)(id))
#define PROF_ENTER() (prof.scene=prof.next_scene)
#else
#define ABL(b) 0u
#define PT_START(v)
#define PT_ADD(f,v)
#define PROF_NEXT(id)
#define PROF_ENTER()
#endif
#ifndef PLAT_HOST
#define POOL __attribute__((section(".pool")))
#else
#define POOL
#endif
namespace {
#include "satin_tables.inc"
constexpr unsigned Q=64, GN=131072, GM=GN-1;
// Each slot owns one complete live engine. Common filters, envelopes and
// scratch retain their lifetime; the Elements exciter stays separately owned.
#if defined(MD_ELEMENTS_BODY_UNION) && !defined(MD_ENGINE_UNION)
#error "Elements body sharing requires the voice engine union"
#endif
#ifdef MD_ENGINE_UNION
using VoiceHat=plaits::HiHat<plaits::SquareNoise,plaits::SwingVCA,false,true>;
union VoiceEngine {
 plaits::AdditiveEngine additive;plaits::FMEngine fm;plaits::VirtualAnalogEngine va;
 plaits::AnalogBassDrum kick;plaits::AnalogSnareDrum snare;VoiceHat hat;rings::Resonator rings;
#ifdef MD_ELEMENTS_BODY_UNION
 elements::Resonator elements;
#endif
 VoiceEngine() {} // reset_voice constructs exactly one member with placement new.
 ~VoiceEngine() {} // destroy_voice_engine ends that member before storage reuse.
};
#define VOICE_ENGINE(v,name) ((v).engine.name)
#else
#define VOICE_ENGINE(v,name) ((v).name)
#endif
struct Voice {
#ifdef MD_ENGINE_UNION
 VoiceEngine engine;stmlib::Svf filter1,filter2;
#else
 plaits::AdditiveEngine additive; stmlib::Svf filter1,filter2;
 plaits::FMEngine fm; plaits::VirtualAnalogEngine va;
 plaits::AnalogBassDrum kick; plaits::AnalogSnareDrum snare;
 plaits::HiHat<plaits::SquareNoise,plaits::SwingVCA,false,true> hat;
 rings::Resonator rings;
#endif
 float satin_morph,satin_tone,noise_lp,noise_slow,grain_send_smooth;
 float colour_tone,colour_harm,colour_shape;
 float choir_phase[3],choir_frequency[3];
 float expression_smooth,pan_smooth,midi_gate,mellow_phase,mellow_drift;
 float strike_gate,target_note,sub_phase,tide_phase,tide2_phase,warp_phase,warp_smooth,delay_send_smooth,reverb_send_smooth;
 float mem[112],env,amp,note,excite,fade,velocity,cut_smooth,res_smooth,breath_phase,lpg,eco_previous,eco_next;
 int model,trigger,attacking,active,eco_phase;
};
alignas(Voice) static unsigned char voices_raw[sizeof(Voice)*6] POOL;
static Voice *voices;
#ifdef MD_ENGINE_UNION
// No tag in every Voice: model already identifies its live member. The bit is
// clear for custom models and mixer-only boot tests, which construct no engine.
static uint8_t voice_engine_live;
static void destroy_voice_engine(unsigned i) {
 if(!(voice_engine_live&(1u<<i)))return;
 VoiceEngine &e=voices[i].engine;
 switch(voices[i].model){
 case MD_FM:e.fm.~FMEngine();break;case MD_VA:e.va.~VirtualAnalogEngine();break;
 case MD_HARMONIC:e.additive.~AdditiveEngine();break;
 case MD_KICK:e.kick.~AnalogBassDrum();break;case MD_SNARE:e.snare.~AnalogSnareDrum();break;
 case MD_HAT:e.hat.~VoiceHat();break;case MD_RINGS:e.rings.~Resonator();break;
#ifdef MD_ELEMENTS_BODY_UNION
 case MD_ELEMENTS:e.elements.~Resonator();break;
#endif
 }
 voice_engine_live&=(uint8_t)~(1u<<i);
}
#endif
// All engine and exciter storage is in the packed SRAM region.
// Tape/effect buffers retain their independent size and lifetime.
#ifdef MD_ELEMENTS_BODY_UNION
#define MD_ELEMENTS_BODY_IN_VOICE 1
#define ELEMENT_BODY(i) (voices[(i)].engine.elements)
#ifndef PLAT_HOST
static_assert(sizeof(elements::Resonator)<=sizeof(rings::Resonator),"Elements body must fit the target engine storage");
static_assert(sizeof(VoiceEngine)==1336&&alignof(VoiceEngine)==4,"target engine layout");
static_assert(sizeof(Voice)==2004&&alignof(Voice)==4,"target voice stride unchanged");
static_assert(offsetof(Voice,filter1)==1336&&offsetof(Voice,filter2)==1356,"target filters unchanged");
static_assert(offsetof(Voice,mem)==1488&&offsetof(Voice,model)==1984,"target voice tail unchanged");
#endif
#else
static elements::Resonator element_body[6];
#define ELEMENT_BODY(i) (element_body[(i)])
#endif
static elements::Exciter element_strike[6];
static int16_t tape[GN] POOL;
// Independent effect storage: never borrow or shorten the granular tape.
static int16_t echo[16384] POOL;
// Each line keeps its original audible delay length; only unreachable tail storage is removed.
static constexpr unsigned room_lens[4]={887,1013,1151,1201};
static constexpr unsigned room_base[4]={0,room_lens[0],room_lens[0]+room_lens[1],room_lens[0]+room_lens[1]+room_lens[2]};
static constexpr unsigned room_cells=room_base[3]+room_lens[3];
static_assert(room_cells==4252,"packed room active cells");
static int16_t room[room_cells];
// Two short input allpasses soften discrete room echoes without more voices.
static constexpr unsigned diffusion_len[2]={113,181};
static constexpr unsigned diffusion_cells=diffusion_len[0]+diffusion_len[1];
static float room_diffusion[diffusion_cells];static unsigned diffusion_head[2];
static_assert(sizeof room==8504&&sizeof room_diffusion==1176,"exact active room/allpass storage");
static bool fx_active;
// K-4: the room lines' slow read movement during Itinera (phase 0..1 each).
static float room_mod_phase[4];
static unsigned echo_head,room_head[4];
static float echo_lp,room_lp[4],fx_smooth,delay_smooth=8820,fx_highpass_in,fx_highpass_out;
static float diffuse_room(float x){
 const unsigned *const length=diffusion_len;
 for(unsigned j=0;j<2;j++){float delayed=room_diffusion[(j?diffusion_len[0]:0)+diffusion_head[j]];float y=delayed-.62f*x;room_diffusion[(j?diffusion_len[0]:0)+diffusion_head[j]]=x+.62f*y;x=y;if(++diffusion_head[j]>=length[j])diffusion_head[j]=0;}
 return x;
}
/* O-3: with the effect return at zero and its smoothing below -140 dB, the
 * echo and room stop running: their lines are cleared (so a later return
 * starts from silence, never from an old tail) and the per-sample effect
 * work is skipped until a return is set again. The echo keeps its time. */
static void rest_fx(){
 fx_active=false;fx_smooth=0;
 memset(room_diffusion,0,sizeof room_diffusion);memset(echo,0,sizeof echo);memset(room,0,sizeof room);memset(room_lp,0,sizeof room_lp);
 echo_lp=fx_highpass_in=fx_highpass_out=0;
}
static void reset_fx(){fx_active=false;memset(room_diffusion,0,sizeof room_diffusion);memset(diffusion_head,0,sizeof diffusion_head);memset(echo,0,sizeof echo);memset(room,0,sizeof room);memset(room_head,0,sizeof room_head);memset(room_lp,0,sizeof room_lp);echo_head=0;echo_lp=fx_smooth=fx_highpass_in=fx_highpass_out=0;delay_smooth=8820;}
static void creative_defaults(md_patch &p){p.version=MD_PATCH_VERSION;memset(p.links,0,sizeof p.links);for(auto &l:p.links)l.release=300;memset(&p.drift,0,sizeof p.drift);p.drift.depth=1000;p.drift.rate=250;}
static void journey_defaults(md_patch &p){creative_defaults(p);memset(&p.journey,0,sizeof p.journey);p.journey.length=1;for(auto &s:p.journey.section){s.biome=65535;s.bars=4;s.repeats=1;}}
static void motion_defaults(md_patch &p){journey_defaults(p);memset(&p.motion,0,sizeof p.motion);}
static void kit_defaults(md_patch &p){
 p.version=8;memset(&p.kit,0,sizeof p.kit);p.kit.length=16;p.kit.gates[0]=0x01010101u;p.kit.gates[1]=0x10101010u;p.kit.gates[2]=0x44444444u;
 for(int j=0;j<8;j++)p.kit.pads[j]={0,700,650,200,850};motion_defaults(p);
}
static void next_defaults(md_patch &p){
 kit_defaults(p);p.grain_noise=0;p.chord_pattern=0;p.chord_bars=2;p.chord_tracks=63;
 memset(p.sequence,0,sizeof p.sequence);
 for(int i=0;i<6;i++){md_sequence &q=p.sequence[i];q.length=16;
  for(int j=0;j<MD_STEPS;j++){int off=p.ext[i].notes[j%16];q.steps[j]={(uint8_t)(off<0||!p.ext[i].bass_mode?0:(int64_t)p.track[i].note+off>96?96:(int64_t)p.track[i].note+off<24?24:p.track[i].note+off),80,100,85};}}
}
static void extension_defaults(md_patch &p){p.version=7;p.delay_ms=200;p.delay_fb=300;p.reverb_time=450;p.fx_level=0;memset(p.ext,0,sizeof p.ext);for(int i=0;i<6;i++){p.ext[i].tides_rate=250;for(int j=0;j<16;j++)p.ext[i].notes[j]=(j%4==0?0:j%4==2?7:-1);}next_defaults(p);}

struct Grain { float pos,phase,inc,pan,rate,gain; unsigned life,age; };
struct Repeat { float left,spacing,note,velocity; unsigned remaining; };
static Repeat repeats[6];
static Grain grains[4];
static md_patch patch,previous,journey_home;
static md_sequence sequence_clipboard;
static uint8_t sequence_clip_kind,sequence_head[MD_TRACKS],sequence_hit,fill_state;
static uint32_t sequence_notice;

static int journey_active,journey_at,journey_prepared;static unsigned journey_ticks,journey_origin[6],journey_chord_origin,journey_fade_mask;static float journey_fade_step,journey_dry_last[2],journey_release[2];
static bool creative_active();
static void creative_track(unsigned,md_track&,md_extension&);
static void creative_reset();
static void learn_reset();
static void learn_reset_port(int);
static void learn_cancel();
static void link_hit(unsigned,float);
static int drift_macro(unsigned);
static int creative_probability(unsigned);
static void journey_stop();
static void journey_start();
static bool journey_tick();
static void journey_prepare();
static float journey_gain(unsigned i,unsigned k,unsigned n);
static uint32_t saved_hash[2];static int pending_biome=-1;
// Canonical fingerprint ignores navigation cursors and the descriptive edit flag.
// Compute only on a change request or successful save, never in the sample loop.
// The same bytes as the former per-byte test, as ascending skipped ranges: one
// pass without six range checks per byte (it runs in the callback).
// The patch and full-word offsets are aligned. On the device, -fno-builtin
// made the previous four-byte memcpy call XIP code once for every word.
// Read the same native-endian word directly without changing the fingerprint.
typedef uint32_t __attribute__((__may_alias__)) PatchHashWord;
static_assert(alignof(md_patch)>=alignof(PatchHashWord),"aligned patch fingerprint");
static void patch_hash_of(const md_patch &q,uint32_t out[2]){
 unsigned skip[15][2],n=0;
 skip[n][0]=offsetof(md_patch,edited_mask);skip[n++][1]=offsetof(md_patch,ext);
 for(unsigned i=0;i<6;i++){const unsigned at=offsetof(md_patch,ext)+i*sizeof(md_extension)+offsetof(md_extension,edit_step);skip[n][0]=at;skip[n++][1]=at+4;}
 for(unsigned i=0;i<6;i++){const unsigned at=offsetof(md_patch,sequence)+i*sizeof(md_sequence)+offsetof(md_sequence,edit);skip[n][0]=at;skip[n++][1]=at+1;}
 skip[n][0]=offsetof(md_patch,kit)+offsetof(md_kit,pad);skip[n++][1]=offsetof(md_patch,kit)+offsetof(md_kit,pad)+1;
 skip[n][0]=offsetof(md_patch,kit)+offsetof(md_kit,step);skip[n++][1]=offsetof(md_patch,kit)+offsetof(md_kit,step)+1;
 // F-1: four bytes a step where a whole aligned word lies between the skipped
 // ranges (single bytes at a range's edges). The value is only ever compared
 // with itself (never saved), so the faster mix needs no compatibility.
 const uint8_t *b=(const uint8_t*)&q;uint32_t a=2166136261u,c=5381;unsigned j=0;
 for(unsigned r=0;r<=n;r++){
  const unsigned end=r<n?skip[r][0]:sizeof q;
  for(;j<end&&(j&3u);j++){a=(a^b[j])*16777619u;c=(c*33u)^b[j];}
  for(;j+4<=end;j+=4){const uint32_t w=*reinterpret_cast<const PatchHashWord*>(b+j);a=(a^w)*16777619u;c=(c*33u)^w;}
  for(;j<end;j++){a=(a^b[j])*16777619u;c=(c*33u)^b[j];}
  if(r<n&&skip[r][1]>j)j=skip[r][1];
 }out[0]=a;out[1]=c;
}
static void patch_hash(uint32_t out[2]){patch_hash_of(journey_active?journey_home:patch,out);}

static md_stats stats;
/* F-3: ECO taken by the admission guard or an overload is the render's own
 * state, not the project's: the saved quality stays as the player set it, the
 * project stays unchanged. A new or restored project, a Journey scene, RETURN or
 * an explicit quality setting start from the project's own setting again. */
static bool runtime_eco;
static bool quality_on(){return patch.quality&&!runtime_eco;}
static void eco_settle(){for(unsigned i=0;i<6;i++){voices[i].eco_phase=0;voices[i].eco_previous=voices[i].eco_next=0;voices[i].fade=0;}}
static unsigned writehead,grainclock,tape_valid;
static float feedback_lp,dcx[2],dcy[2],smooth[4],grain_noise_smooth,grain_noise_lp;
static float seq_gate[6];
static int record_mode,record_track,record_note=-1,record_step=-1;
static unsigned record_start;
static unsigned rng,seqrng,step;
static float untilstep,duckenv;
static int playing;
static int clock_source,midi_quantize,clock_pulse;
static unsigned clock_age,clock_last;static float clock_interval;
// F-2: the last beat of clock pulses (sample positions), for a steady tempo.
static uint32_t clock_hist[24];static unsigned clock_seen;
/* F-6: the tempo an external clock gives is the transport's, not the project's:
 * it times steps, swing, gates and fades while that clock leads, and the saved
 * tempo stays as set (taken over only by an explicit MD_TEMPO). 0: none yet. */
static int clock_tempo;
/* 2.4 VINTAGE: each voice drifts a few cents, slowly and differently, as old synths do: a new random target every 3 to 9 s, eased over about
 * 4 s (peak +- 6 or +- 14 cents). Off: no work at all, no random numbers drawn. */
static uint8_t finish_vintage;static float vint_off[6],vint_target[6];static uint16_t vint_wait[6];static uint32_t vint_rng=0x1f123bb5u;
static uint8_t tempo_shift;   /* 2.4 TEMPO RANGE: the own tempo is divided by 1, 2, 4 or 8 (the saved tempo stays 30..220); an outside clock is not divided */
static int tempo_now(){return clock_source&&clock_tempo?clock_tempo:patch.tempo>>tempo_shift;}
static float tempo_f(){return clock_source&&clock_tempo?(float)clock_tempo:(float)patch.tempo/(float)(1<<tempo_shift);}
// F-2: MIDI placed in this callback's block by its arrival time (frames from
// the block's start), played where it falls instead of at the block's start.
struct MidiAt{uint32_t frame;uint8_t st,a,b;int8_t port;};
static MidiAt midi_at[32];static unsigned midi_at_n,block_pos;
static int held[6],held_port[6],sustain[6],midi_bank[6];
static uint8_t midi_route[6],midi_macro_enabled,velocity_touch;
static float note_touch[MD_TRACKS];
static float bend[6],bend_smooth[6],expression[6];
// A removed transport releases only the transient controls it last supplied.
static int8_t bend_port[6],expression_port[6],sustain_port[6];
// MIDI performance state is transient, per transport/channel, outside saved sounds.
static uint8_t rpn_select[3][16][2],bend_semitones[6],bend_cents[6],macro_lsb_seen[3];
static uint16_t bend_wheel[6],macro14[3][4];
static float bend_range(unsigned i){return fminf(24.f,bend_semitones[i]+bend_cents[i]*.01f);}
static void update_bend(unsigned i){bend[i]=((int)bend_wheel[i]-8192)*(bend_range(i)/8192.f);}
struct Command { int c,t,v; };
static Command queue[64];
static volatile unsigned qread,qwrite;
static inline void fence() { __asm__ volatile("" ::: "memory"); }
/* 2.2 development: the engines' entries in RAM on the FM-1 (third_party/md_ram.h); XIP code reaches RAM
 * code only through a pointer the compiler cannot turn back into a direct call (plat.h PLAT_FAR). */
extern "C" {
void md_ram_rings_process(rings::Resonator *r, const float *in, float *out, float *aux, size_t n);
void md_ram_exciter_process(elements::Exciter *e, uint8_t flags, float *out, size_t n);
void md_ram_resonator_process(elements::Resonator *r, const float *bow, const float *in, float *center, float *sides, size_t n);
void md_ram_fm_render(plaits::FMEngine *e, const plaits::EngineParameters *p, float *out, float *aux, size_t n, bool *env);
void md_ram_va_render(plaits::VirtualAnalogEngine *e, const plaits::EngineParameters *p, float *out, float *aux, size_t n, bool *env);
void md_ram_additive_render(plaits::AdditiveEngine *e, const plaits::EngineParameters *p, float *out, float *aux, size_t n, bool *env);
}
template <typename F> static inline __attribute__((always_inline)) F md_far(F fn) { F volatile held = fn; return held; }
static int bound(int x,int a,int b) { return x<a?a:x>b?b:x; }
static float unit(int x) { return bound(x,0,1000)*.001f; }
// Fade finite partials before Nyquist instead of abruptly deleting them on a
// pitch bend. The full lower register is unchanged. Evaluated per audio tile.
static float partial_gain(float frequency){return fminf(1.0f,fmaxf(0.0f,(.45f-frequency)*10.0f));}
static unsigned random_word(unsigned &r) { r^=r<<13;r^=r>>17;r^=r<<5;return r; }
static float noise() { return (random_word(rng)>>8)*(1.0f/8388608.0f)-1.0f; }
// Linear at ordinary levels; only excess peaks enter the soft safety knee.
static float clip(float x) { float a=fabsf(x);if(a<=.8f)return x;float y=.8f+.2f*(a-.8f)/(.2f+a-.8f);return x<0?-y:y; }
// Keep existing warm settings up to 90%; open the final tenth to 18 kHz.
static float filter_q(float resonance) { return .707f+2.0f*resonance*resonance; }
static float cutoff_hz(float u) { return 60.0f*stmlib::SemitonesToRatio(u*84.0f+fmaxf(0.0f,u-.9f)*147.458f); }
// O-5: once the tape has been filled, every index is valid: no test per point.
static float tape_at(unsigned i){i&=GM;if(tape_valid==GN)return tape[i]*(1.0f/32768.0f);return (i<tape_valid?tape[i]:0)*(1.0f/32768.0f);}
static float tape_read(float pos){
 unsigned a=(unsigned)pos;float t=pos-a;
 float xm=tape_at(a-1),x0=tape_at(a),x1=tape_at(a+1),x2=tape_at(a+2);
 // Four-point Hermite interpolation: exact at stored samples, no linear droop.
 float c=.5f*(x1-xm),b=xm-2.5f*x0+2*x1-.5f*x2,d=.5f*(x2-xm)+1.5f*(x0-x1);
 return ((d*t+b)*t+c)*t+x0;
}
/* 2.4: the grain loop's reader. The same arithmetic as tape_read in the same order; for a full tape and a position with all four
 * neighbours inside the ring, the four samples are read straight (the masks and the tape_valid tests cannot change the result). */
static inline float tape_read_ring(float pos){
 const unsigned a=(unsigned)pos;
 if(tape_valid==GN&&a>=1&&a<=GN-3){
  const int16_t *q=tape+a;const float t=pos-a;
  const float xm=q[-1]*(1.0f/32768.0f),x0=q[0]*(1.0f/32768.0f),x1=q[1]*(1.0f/32768.0f),x2=q[2]*(1.0f/32768.0f);
  const float c=.5f*(x1-xm),b=xm-2.5f*x0+2*x1-.5f*x2,d=.5f*(x2-xm)+1.5f*(x0-x1);
  return ((d*t+b)*t+c)*t+x0;
 }
 return tape_read(pos);
}
#include "cedar.inc"
#include "skin.inc"
/* 2.2: frames each voice has stayed below -102 dBFS (outside Voice: the DSP pool keeps its 8 KB reserve) */
static uint32_t voice_quiet[6];
static void reset_voice(unsigned i) {
 voice_quiet[i]=0;
 sequence_head[i]=0;sequence_hit&=~(1u<<i);
 note_touch[i]=0;
#ifdef MD_ENGINE_UNION
 destroy_voice_engine(i); // every caller is outside an outstanding mix job.
#endif
 // Value-initialisation clears the common aggregate state. The inactive union
 // is cleared explicitly below; Elements keeps its original own object clear.
 Voice *v=new (&voices[i]) Voice();
 stmlib::BufferAllocator a(v->mem,sizeof v->mem);
 // Keep the shared allocator sequence (VA scratch, then harmonic amplitudes)
 // unchanged. Other engines own no shared allocation and are used only when
 // selected; a model change always comes through this reset first.
 const int model=patch.track[i].model;
#ifdef MD_ENGINE_UNION
 // The union constructor activates nothing. Clear only that inactive storage,
 // then begin the selected class's lifetime; constructors restore its vtable.
 memset(static_cast<void*>(&v->engine),0,sizeof v->engine);
 switch(model){
 case MD_FM:new (&v->engine.fm) plaits::FMEngine();v->engine.fm.Init(&a);break;
 case MD_VA:new (&v->engine.va) plaits::VirtualAnalogEngine();v->engine.va.Init(&a);break;
 case MD_HARMONIC:
  // Preserve the original VA-before-additive allocation layout, even though
  // VA itself is inactive. This prefix remains scratch, not another engine.
  a.Allocate<float>(plaits::kMaxBlockSize);
  new (&v->engine.additive) plaits::AdditiveEngine();v->engine.additive.Init(&a);v->engine.additive.Reset();break;
 case MD_KICK:new (&v->engine.kick) plaits::AnalogBassDrum();v->engine.kick.Init();break;
 case MD_SNARE:new (&v->engine.snare) plaits::AnalogSnareDrum();v->engine.snare.Init();break;
 case MD_HAT:new (&v->engine.hat) VoiceHat();v->engine.hat.Init();break;
 case MD_RINGS:new (&v->engine.rings) rings::Resonator();v->engine.rings.Init();break;
#ifdef MD_ELEMENTS_BODY_UNION
 case MD_ELEMENTS:new (&v->engine.elements) elements::Resonator();break;
#endif
 default:break;
 }
 if(model==MD_FM||model==MD_VA||model==MD_HARMONIC||model==MD_KICK||model==MD_SNARE||model==MD_HAT||model==MD_RINGS
#ifdef MD_ELEMENTS_BODY_UNION
    ||model==MD_ELEMENTS
#endif
 )voice_engine_live|=(uint8_t)(1u<<i);
#else
 v->fm.Init(&a);v->va.Init(&a);
 if(model==MD_KICK)v->kick.Init();
 if(model==MD_SNARE)v->snare.Init();
 if(model==MD_HAT)v->hat.Init();
 if(model==MD_RINGS)v->rings.Init();
 v->additive.Init(&a);v->additive.Reset();
#endif
 v->filter1.Init();v->filter2.Init();
 v->cut_smooth=patch.track[i].cutoff*.001f;v->res_smooth=patch.track[i].resonance*.001f;v->breath_phase=i*.137f;
 v->colour_tone=unit(patch.track[i].tone);v->colour_harm=unit(patch.track[i].harm);v->colour_shape=unit(patch.track[i].shape);
 v->expression_smooth=1;v->pan_smooth=unit(patch.track[i].pan);held[i]=held_port[i]=-1;sustain[i]=0;bend[i]=bend_smooth[i]=0;bend_wheel[i]=8192;expression[i]=1;bend_port[i]=expression_port[i]=sustain_port[i]=-1;
 v->model=patch.track[i].model;v->note=(float)patch.track[i].note;v->target_note=v->note;v->fade=0;v->velocity=.8f;
 // These bodies are read only by MD_ELEMENTS. Initialise them on entry to
 // that model; resetting any other voice must not rebuild unused resonators.
 if(v->model==MD_ELEMENTS){
  // This non-polymorphic body retains the original post-construction clear.
  // In particular Init does not set lfo_phase_; no pre-lifetime store is relied on.
  memset(static_cast<void*>(&ELEMENT_BODY(i)),0,sizeof(ELEMENT_BODY(i)));
  ELEMENT_BODY(i).Init();ELEMENT_BODY(i).set_modulation_frequency(.5f/MD_RATE);ELEMENT_BODY(i).set_modulation_offset(.25f);
  memset(static_cast<void*>(&element_strike[i]),0,sizeof(element_strike[i]));element_strike[i].Init();
 }
 if(v->model==MD_CEDAR||v->model==MD_BOWED)cedar_reset(i);
 if(v->model==MD_AERO)cedar_state[i].rng=patch.seed^(0x9e3779b9u+i*7919u);
 repeats[i].remaining=0;
}
#include "preset_bank.inc"
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
// K-6: the stems' own noise source; the mix never draws from it.
static uint32_t stem_rng=0x2545f491u;
// Same dither, clipping and rounding; retain global RNG order at each sample.
static inline int16_t stem_quantize(float sample,uint32_t &noise){
 uint32_t r=noise;r^=r<<13;r^=r>>17;r^=r<<5;const float a=(r>>8)*(1.f/16777216.f);
 r^=r<<13;r^=r>>17;r^=r<<5;const float b=(r>>8)*(1.f/16777216.f);noise=r;
 const float v=clip(sample*1.9f)*32767.f+a-b;
 const float y=v+.5f;int q=(int)y;q-=(y<(float)q);   /* floorf without the library call: v is finite and far inside int */
 return (int16_t)(q>32767?32767:q<-32768?-32768:q);
}
static inline void write_stem(const md_usb_stems &stems,unsigned frame,unsigned track,float sample){
 if(!stems.first)return;
 int16_t *out=frame<stems.first_frames?stems.first+frame*8:stems.wrapped+(frame-stems.first_frames)*8;
 out[track]=stem_quantize(sample,stem_rng);
}
#endif
// K-1: the kit's share of an Itinera scene's staggered end (1: none).
static float kit_scene_gain=1.f,kit_scene_step=0.f;
#include "grids.inc"
/* 2.4: the loudness trim of the factory Biomes (tools/gen_biome_trim.py). The Biomes differed by 12 LU between the quietest
 * and the loudest tenth; the trim lifts the quiet ones, never above the loudest true peak a Biome has had, never lowers one.
 * It depends only on the loaded Biome's number: nothing is saved, User Biomes and the Blank Biome are not trimmed. */
#include "biome_trim.inc"
static float trim_gain=-1.f;
static uint8_t level_match_off,finish_width,finish_air,finish_chorus;   /* 2.4: LEVEL MATCH off (the 2.3 levels), WIDTH 0..3 (a setting of the app, not of a patch) */
static float width_now=1.f;
static inline float parabola_sine(float p){p-=floorf(p);return p<.5f?16.f*p*(.5f-p):-16.f*(p-.5f)*(1.f-p);}   /* a cheap sine of the phase 0..1 */
/* 2.4 CHORUS: a slow two-voice chorus on everything above about 250 Hz (the bass stays where it is); 2 x 1024 samples of int16 */
static int16_t chorus_buf[2][1024];static uint16_t chorus_pos;static float chorus_lp[2];static bool chorus_clear,air_clear;static float chorus_amount,chorus_phase_l,chorus_phase_r,chorus_delay_l=573.f,chorus_delay_r=573.f;   /* integer inner loop: the device's floating point is slow */
static float air_amount,air_env,air_lp1[2],air_lp2[2];static uint32_t air_seed=0x2545F491u;   /* 2.4 AIR: a breath of noise above about 7 kHz that follows the sound */
static float trim_target_gain(){
 const int id=patch.biome_id;
 return level_match_off?1.f:id>=0&&id<(int)(sizeof biome_trim)&&biome_trim[id]?stmlib::SemitonesToRatio(biome_trim[id]*.99658f):1.f;   /* half dB: x 12/6.0206/2 semitones */
}
#include "lofi_kit.inc"
static void clear_audio() {
 fill_state=0;
 memset(journey_release,0,sizeof journey_release);memset(journey_dry_last,0,sizeof journey_dry_last);
 creative_reset();learn_reset();learn_cancel();kit_reset();reset_fx();tape_valid=0;writehead=grainclock=0;
 memset(grains,0,sizeof grains);memset(repeats,0,sizeof repeats);
 memset(dcx,0,sizeof dcx);memset(dcy,0,sizeof dcy);feedback_lp=duckenv=grain_noise_smooth=grain_noise_lp=0;memset(seq_gate,0,sizeof seq_gate);record_mode=0;record_note=record_step=-1;
}
static void factory_scene(md_patch &patch,int id) {
 id=bound(id,0,MD_BIOMES-1);
 const auto &biome=biome_bank[id];
 memset(&patch,0,sizeof patch);
 patch.magic=0x4D445231;
 patch.seed=biome.seed;
 // 2.2: the row starts with its six tone IDs; a track with a factory tone takes that tone's ten
 // fields from tone_bank (generate_presets.py stores only the rest). Same patch as before.
 const uint8_t *bytes=biome_values+biome.at;
 uint32_t bits=0;
 unsigned available=0;
 auto take=[&](unsigned field)->int32_t{
  const unsigned width=factory_widths[field];
  while(available<width){
   bits|=(uint32_t)*bytes++<<available;
   available+=8;
  }
  const int32_t value=factory_minimums[field]+(int32_t)(bits&((1u<<width)-1u));
  bits>>=width;
  available-=width;
  return value;
 };
 int32_t tones[6];
 for(unsigned t=0;t<6;t++)tones[t]=take(FACTORY_TONE_FIELD+t);
 for(unsigned field=0;field<160;field++){
  int32_t value;
  const unsigned rel=field-FACTORY_TRACK_FIELD,t=rel/FACTORY_TRACK_FIELDS,k=rel%FACTORY_TRACK_FIELDS;
  if(field>=FACTORY_TONE_FIELD&&field<FACTORY_TONE_FIELD+6)value=tones[field-FACTORY_TONE_FIELD];
  else if(field>=FACTORY_TRACK_FIELD&&t<6&&tones[t]>=0&&((FACTORY_TONE_MASK>>k)&1u)){
   const TonePreset &a=tone_bank[tones[t]];
   static const uint8_t word[22]={0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,1,2,2,0,2},at[22]={0,7,17,0,0,0,0,0,0,0,0,0,0,0,10,0,0,20,0,10,5,20};
   const uint32_t w=word[k]==0?a.identity:word[k]==1?a.contour:a.colour;
   value=(int32_t)((w>>at[k])&(k==0?31u:k==20?3u:1023u));
  }else value=take(field);
  memcpy((uint8_t*)&patch+12+field*4,&value,sizeof value);
 }
 extension_defaults(patch);
 if(id!=MD_BLANK_BIOME){
  unsigned family=biome_bank[id].family;
  int atlas_arch=id>=MD_V13_BIOMES&&id<157?(id-MD_V13_BIOMES)%8:-1;
  bool ambient=family==0||family==1||(id>=65&&id<MD_V13_BIOMES)||atlas_arch==2||atlas_arch==5||atlas_arch==6;
  patch.fx_level=ambient?380:family==2||family==5||family==7?260:140;
  patch.reverb_time=ambient?640:450;patch.delay_fb=ambient?220:270;
  patch.delay_ms=bound(60000/patch.tempo/2,40,350);
  for(int i=0;i<6;i++){
   md_extension &e=patch.ext[i];int model=patch.track[i].model;
   e.reverb_send=model==MD_BASS||model==MD_KICK?60:ambient?330+(i%3)*55:180;
   e.delay_send=(model==MD_FM||model==MD_VELVET_KEYS||model==MD_RINGS)?(ambient?140:110):0;
   if(ambient&&md_has_envelope(model)){e.tides=90+(i%3)*35;e.tides_rate=650+i*45;}
  }
 }
 if(id>=77&&id<MD_V13_BIOMES){
  patch.chord_pattern=1+(id-77)%4;patch.chord_bars=4;patch.chord_tracks=63;
  // A sparse, editable phrase in the existing step sequencer: one foreground
  // gesture, while pads and bass keep their independent slower cycles.
  static const int8_t phrases[4][16]={
   {0,-1,-1,-1,7,-1,-1,-1,0,-1,3,-1,-1,-1,-1,-1},
   {0,-1,-1,7,-1,-1,-1,-1,12,-1,-1,-1,7,-1,-1,-1},
   {0,-1,-1,-1,-1,3,-1,-1,7,-1,-1,-1,-1,-1,0,-1},
   {0,-1,-1,-1,5,-1,-1,-1,-1,-1,7,-1,0,-1,-1,-1}};
  for(int i=0;i<6;i++)if(patch.track[i].model==MD_VELVET_KEYS){
   md_sequence &q=patch.sequence[i];q.enabled=1;q.length=16;patch.track[i].speed=4;
   for(int k=0;k<16;k++){int off=phrases[(id-77)%4][k];q.steps[k]={(uint8_t)(off<0?0:bound(md_quantize(patch.track[i].note+off,patch.root,patch.scale),24,96)),(uint8_t)(k==0?78:62+(k*3+id)%14),100,(uint8_t)(72+(k*7)%25)};}break;
  }
 }
 if(id>=93&&id<MD_V13_BIOMES){
  if(id>=101){patch.kit.enabled=1;static const int kit_banks[8]={0,1,2,3,4,0,3,4};patch.kit.bank=kit_banks[id-101];kit_pattern(patch.kit,(id-101)%4);patch.track[3].level=700;patch.track[3].send=280;patch.ext[3].reverb_send=150;patch.track[3].speed=1;}
  patch.chord_pattern=1+(id%3);patch.chord_bars=4;patch.chord_tracks=7;
 }
 if(id>=MD_V13_BIOMES&&id<157){
  const int arch=(id-MD_V13_BIOMES)%8,variation=(id-MD_V13_BIOMES)/8;
  if(arch==4){
   patch.kit.enabled=1;patch.kit.bank=variation%5;kit_pattern(patch.kit,variation%4);
   patch.track[3].level=700;patch.track[3].send=170;
   patch.ext[3].reverb_send=110;patch.grain_noise=90;
  }else if(arch==6){patch.grain_noise=55;}
  if(arch==0||arch==2||arch==5){
   patch.chord_pattern=5+variation%4;patch.chord_bars=4;
   patch.chord_tracks=arch==2?0x24:0x35;
  }else if(arch==3){
   patch.chord_pattern=1+variation%3;patch.chord_bars=4;patch.chord_tracks=0x12;
  }
  if(arch==1||arch==3||arch==7){
   const int lead=arch==3?1:arch==7?3:5;
   md_sequence &q=patch.sequence[lead];q.enabled=1;q.length=16;
   patch.track[lead].speed=arch==1?1:2;
   static const int8_t phrase[3][16]={
    {0,-1,-1,-1,3,-1,7,-1,0,-1,-1,10,7,-1,-1,-1},
    {0,-1,7,-1,-1,-1,3,-1,12,-1,-1,7,-1,-1,3,-1},
    {0,-1,3,-1,7,-1,-1,10,0,-1,7,-1,12,-1,3,-1}};
   for(int k=0;k<16;k++){
    int off=phrase[(variation+arch)%3][k];
    q.steps[k]={(uint8_t)(off<0?0:bound(md_quantize(patch.track[lead].note+off,patch.root,patch.scale),24,96)),
                (uint8_t)(k==0?76:53+(k*3+variation)%22),100,(uint8_t)(64+(k*5)%31)};
   }
  }
 }
 if(id>=157&&id<MD_V15_BIOMES){
  const int world=id-157;
  bool quiet=world==1||world==6||world==11;
  patch.fx_level=quiet?350:world==4||world==5||world==7||world==10?225:310;
  patch.reverb_time=quiet?760:world==0||world==9?630:510;
  // The independent echo has 16,384 samples (371 ms at 44.1 kHz). Keep every
  // factory world inside its validated 40..350 ms window; an oversized delay
  // would make the ring read position negative before the write head wraps.
  patch.delay_ms=bound(60000/patch.tempo/2,40,350);
  patch.delay_fb=quiet?250:world==0||world==8?310:210;
  patch.grain_noise=world==3?70:world==4?55:world==11?35:0;
  for(int i=0;i<6;i++){
   md_extension &e=patch.ext[i];int model=patch.track[i].model;
   e.reverb_send=model==MD_KICK||model==MD_BASS?60:quiet?420:world==5||world==7?140:270;
   e.delay_send=model==MD_FM||model==MD_VELVET_KEYS||model==MD_RINGS?
                (quiet?190:125):0;
   if(quiet&&md_has_envelope(model)){e.tides=95+(i%3)*32;e.tides_rate=700+i*50;}
  }
  if(world==4){
   patch.kit.enabled=1;patch.kit.bank=4;kit_pattern(patch.kit,2);
   patch.track[3].level=690;patch.track[3].send=170;
   patch.ext[3].reverb_send=100;
  }
  if(world==2||world==8||world==9){
   patch.chord_pattern=world==2?6:world==8?7:8;
   patch.chord_bars=4;patch.chord_tracks=world==8?0x23:0x15;
  }
 }
 if(id>=MD_V15_BIOMES&&id<177){
  const unsigned world=(unsigned)(id-MD_V15_BIOMES);
  static const uint16_t room_time[8]={620,780,510,320,710,480,540,810};
  static const uint16_t wet[8]={310,330,250,170,340,230,260,360};
  static const uint16_t echo_time[8]={285,340,258,165,330,285,310,345};
  static const uint8_t chord[8]={6,0,1,0,7,5,6,0};
  patch.reverb_time=room_time[world];patch.fx_level=wet[world];
  patch.delay_ms=echo_time[world];patch.delay_fb=world==2?335:world==3?130:225;
  patch.chord_pattern=chord[world];patch.chord_bars=world==2?4:8;
  patch.chord_tracks=world==2?0x14:world==5?0x06:0x15;
  patch.grain_noise=world==5?65:world==7?25:0;
  patch.motion.compact=1;
  for(unsigned i=0;i<6;i++){
   md_extension &e=patch.ext[i];const int model=patch.track[i].model;
   e.reverb_send=model==MD_KICK||model==MD_BASS?45:world==3?100:world==2?190:330;
   e.delay_send=model==MD_VELVET_KEYS||model==MD_SATIN?world==2?290:170:world==3?70:0;
   e.marbles=0; // The scene's identity comes from authored phrases, not jitter.
   if(md_has_envelope(model)&&model!=MD_BASS){
    e.tides=world==3?0:95;e.tides_rate=720+i*35;patch.motion.target[i]=0;patch.motion.wave[i]=1;
    patch.motion.target2[i]=2;patch.motion.wave2[i]=3;
    patch.motion.depth2[i]=world==3?0:70;patch.motion.rate2[i]=840+i*23;
   }
  }
  if(world==5){
   patch.kit.enabled=1;patch.kit.bank=2;kit_pattern(patch.kit,3);
   patch.kit.hiss=8;patch.kit.length=32;patch.track[3].send=110;
   patch.ext[3].reverb_send=95;patch.ext[3].delay_send=60;
  }
  // Distinct foreground phrases; each voice still owns an editable sequence.
  // Longer breath-only worlds deliberately have no sequence to compete with.
  if(world==0||world==2||world==5||world==6){
   const unsigned lead=world==0?0:world==2?4:world==5?1:0;
   static const int8_t phrases[4][16]={
    {0,-1,-1,-1,7,-1,-1,-1,-1,-1,2,-1,-1,-1,12,-1},
    {0,-1,-1,-1,-1,-1,7,-1,-1,-1,-1,-1,12,-1,-1,-1},
    {0,-1,-1,7,-1,-1,-1,-1,3,-1,-1,-1,-1,10,-1,-1},
    {0,-1,7,-1,2,-1,-1,-1,12,-1,7,-1,-1,-1,2,-1}};
   const unsigned phrase=world==0?0:world==2?1:world==5?2:3;
   md_sequence &q=patch.sequence[lead];q.enabled=1;q.length=16;
   for(unsigned k=0;k<16;k++){
    int off=phrases[phrase][k];
    q.steps[k]={(uint8_t)(off<0?0:md_quantize(patch.track[lead].note+off,patch.root,patch.scale)),
                (uint8_t)(k==0?78:58+(k*5+world*3)%17),100,(uint8_t)(world==2?45:72+(k%3)*8)};
   }
   patch.motion.direction[lead]=world==6?2:0;
  }
 }
 if(id>=177&&id<189){
  unsigned world=(unsigned)(id-177);const bool dry=world==2||world==7||world==10;
  patch.reverb_time=dry?390:world==1?810:680;patch.fx_level=dry?140:290;
  patch.delay_ms=world==5?315:world==2?185:285;patch.delay_fb=dry?170:260;
  patch.motion.compact=1;patch.chord_pattern=world==1||world==4||world==8?7:world==2?1:0;
  patch.chord_bars=8;patch.chord_tracks=0x15;
  for(unsigned i=0;i<6;i++){
   md_extension &e=patch.ext[i];int model=patch.track[i].model;
   e.reverb_send=model==MD_BASS||model==MD_KICK?35:dry?100:300;
   e.delay_send=model==MD_CEDAR?dry?85:220:model==MD_VELVET_KEYS?150:0;
   if(md_has_envelope(model)&&model!=MD_BASS){e.tides=dry?40:120;e.tides_rate=710+i*37;
    patch.motion.target[i]=0;patch.motion.wave[i]=1;patch.motion.target2[i]=2;patch.motion.wave2[i]=3;
    patch.motion.depth2[i]=dry?25:85;patch.motion.rate2[i]=800+i*29;}
  }
  if(world==5||world==10){patch.kit.enabled=1;patch.kit.bank=world==5?3:1;kit_pattern(patch.kit,world==5?2:0);patch.kit.hiss=9;}
  if(world==0||world==2||world==3||world==5||world==7||world==9||world==10){
   static const int8_t phrase[3][16]={{0,-1,-1,7,-1,-1,2,-1,-1,12,-1,-1,7,-1,4,-1},{0,-1,-1,-1,-1,5,-1,-1,7,-1,-1,-1,2,-1,-1,-1},{0,-1,7,-1,-1,3,-1,-1,10,-1,-1,7,-1,-1,2,-1}};
   unsigned lead=world==5||world==10?1:0;md_sequence &q=patch.sequence[lead];q.enabled=1;q.length=16;
   for(unsigned k=0;k<16;k++){int off=phrase[world%3][k];q.steps[k]={(uint8_t)(off<0?0:md_quantize(patch.track[lead].note+off,patch.root,patch.scale)),(uint8_t)(k==0?78:56+(k*7+world)%20),100,(uint8_t)(world==2?48:82)};}
   patch.motion.direction[lead]=world==9?2:0;
  }
 }

 if(id>=189&&id<197){
  unsigned world=id-189;patch.fx_level=world==1?330:world==4?260:180;patch.reverb_time=world==1?790:540;
  patch.delay_ms=world==0?273:world==6?315:225;patch.delay_fb=220;patch.motion.compact=1;
  patch.chord_tracks=0;patch.chord_pattern=0;
  for(unsigned i=0;i<6;i++){
   bool skin=patch.track[i].model==MD_SKIN;auto &e=patch.ext[i];
   e.reverb_send=skin?(world==1?230:75):220;e.delay_send=skin?(world==6?280:world==0?120:0):100;
   if(!skin&&md_has_envelope(patch.track[i].model)){e.tides=65;e.tides_rate=780+i*19;patch.motion.target[i]=0;patch.motion.wave[i]=1;}
  }
 }

 if(id>=197&&id<213){
  unsigned w=(unsigned)(id-197);bool dry=w==4||w==12,slow=patch.tempo<60;
  patch.fx_level=dry?90:slow?310:210;patch.reverb_time=dry?280:slow?810:580;
  patch.delay_ms=w==9?340:w==15?242:290;patch.delay_fb=dry?110:w==9?360:210;
  patch.grain_noise=w==7||w==14?45:w==3||w==11?70:0;patch.motion.compact=1;
  patch.chord_pattern=w==0?7:w==8?4:w==13?8:0;patch.chord_bars=8;patch.chord_tracks=w==13?0x17:0x07;
  for(unsigned i=0;i<6;i++){
   int model=patch.track[i].model;md_extension &e=patch.ext[i];
   bool low=model==MD_BASS||model==MD_KICK,hand=model==MD_SKIN||model==MD_HAT||model==MD_SNARE;
   e.reverb_send=low?25:dry?65:hand?80:slow?330:200;
   e.delay_send=low?0:dry?45:w==9?290:model==MD_CEDAR||model==MD_VELVET_KEYS?170:65;
   if(md_has_envelope(model)&&!low){e.tides=slow?75:45;e.tides_rate=780+i*23;patch.motion.target[i]=0;patch.motion.wave[i]=1;}
  }
  if(w==3||w==11){patch.kit.enabled=1;patch.kit.bank=w==3?3:4;kit_pattern(patch.kit,w==3?1:3);patch.kit.hiss=w==3?8:5;patch.track[3].level=w==3?280:190;patch.track[3].send=w==3?180:420;}
  if(!slow){
   static const int8_t phrase[4][16]={{0,-1,-1,7,-1,-1,3,-1,5,-1,-1,10,-1,7,-1,-1},{0,-1,-1,-1,5,-1,7,-1,-1,12,-1,-1,7,-1,2,-1},{0,-1,0,-1,7,-1,-1,3,-1,5,-1,-1,10,-1,7,-1},{0,-1,-1,2,-1,7,-1,-1,5,-1,-1,3,-1,0,-1,-1}};
   unsigned lead=w==3||w==11?1:w==9||w==15?4:0;md_sequence &q=patch.sequence[lead];q.enabled=1;q.length=16;
   for(unsigned k=0;k<16;k++){int n=phrase[w%4][k];q.steps[k]={(uint8_t)(n<0?0:md_quantize(patch.track[lead].note+n,patch.root,patch.scale)),(uint8_t)(k%4?54+(k*7+w)%24:80),100,(uint8_t)(dry?55:84)};}
   patch.motion.direction[lead]=w==1||w==12?1:0;
  }
 }
 if(id>=MD_WARM_FIRST_BIOME&&id<MD_RULE_FIRST_BIOME){
  const WarmScene &w=warm_scenes[id-MD_WARM_FIRST_BIOME];
  patch.fx_level=w.mix;patch.reverb_time=w.room;patch.delay_ms=w.delay;patch.delay_fb=w.feedback;
  patch.motion.compact=1;patch.chord_pattern=w.chord;patch.chord_bars=8;patch.chord_tracks=0x16;
  const bool dry=w.mix<=150;
  for(unsigned i=0;i<6;i++){
   auto &e=patch.ext[i];const int m=patch.track[i].model;
   const bool bass=m==MD_BASS,hand=m==MD_SKIN;
   e.reverb_send=bass?25:dry?70:hand?100:w.room>700?310:220;
   e.delay_send=bass||hand?0:dry?45:i==w.lead?230:90;
   if(!bass&&md_has_envelope(m)){
    e.tides=dry?40:85;e.tides_rate=780+i*23;patch.motion.wave[i]=1;
   }
  }
  if(w.phrase){
   // Compact editable phrases. Wood, tines and voltage stabs have room to
   // answer one another; long-breath Biomes keep their independent cycles.
   static const int8_t phrase[4][16]={
    {0,-1,7,-1,-1,2,-1,-1,12,-1,-1,7,-1,-1,4,-1},
    {0,-1,-1,2,-1,7,-1,-1,-1,5,-1,-1,2,-1,-1,-1},
    {0,-1,-1,-1,7,-1,12,-1,-1,-1,5,-1,2,-1,-1,-1},
    {0,-1,-1,0,-1,7,-1,-1,3,-1,-1,10,-1,7,-1,-1}};
   auto &q=patch.sequence[w.lead];q.enabled=1;q.length=16;
   for(unsigned k=0;k<16;k++){
    int n=phrase[w.phrase-1][k];
    q.steps[k]={(uint8_t)(n<0?0:md_quantize(patch.track[w.lead].note+n,patch.root,patch.scale)),
                (uint8_t)(k==0?78:57+(k*7+w.phrase)%21),100,(uint8_t)(dry?54:78)};
   }
  }
 }
 if(id>=MD_RULE_FIRST_BIOME){
  const RuleScene &w=rule_scenes[id-MD_RULE_FIRST_BIOME];
  const unsigned family=(id-MD_RULE_FIRST_BIOME)/3;
  patch.fx_level=w.mix;patch.reverb_time=w.room;patch.delay_ms=w.delay;
  patch.delay_fb=w.feedback;patch.grain_noise=w.noise;
  for(unsigned i=0;i<MD_TRACKS;i++){
   md_sequence &q=patch.sequence[i];q.enabled=1;q.length=w.lengths[i];
   md_extension &e=patch.ext[i];
   const int model=patch.track[i].model;
   const bool low=model==MD_KICK||model==MD_BASS;
   const bool percussion=model==MD_SKIN||model==MD_HAT||model==MD_SNARE;
   e.reverb_send=low?15:family==2?55:family==0?290:family==3?265:percussion?75:185;
   e.delay_send=low||percussion?0:family==1?(i==2?470:120):family==3?185:family==0?70:25;
   if((family==0||family==3)&&!low&&md_has_envelope(model)){
    e.tides=family==0?62:94;e.tides_rate=760+i*29;
    patch.motion.target[i]=family==0?0:2;patch.motion.wave[i]=1;
   }
   if(id>=MD_SOFT_FIRST_BIOME){
    const md_track &t=patch.track[i];
    const bool foundation=low||(!percussion&&t.note<44);
    const bool pad=!foundation&&t.attack>=300;
    e.reverb_send=foundation?14:percussion||w.mix<160?62:pad?365:190;
    e.delay_send=foundation||percussion?0:id==MD_SOFT_FIRST_BIOME+1&&i==2?525:pad?120:80;
    if(pad){e.tides=85;e.tides_rate=850+i*22;patch.motion.wave[i]=1;}
   }
  }
  for(unsigned i=w.first;i<w.first+w.count;i++){
   const uint32_t event=rule_events[i];
   md_step &s=patch.sequence[event&7u].steps[(event>>3)&31u];
   s={(uint8_t)((event>>8)&127u),(uint8_t)((event>>15)&127u),100,(uint8_t)((event>>22)&127u)};
   md_step_set_rule(&s,event>>29);
  }
 }
 for(const auto &arrangement:factory_journeys)if(arrangement.biome==id){
  patch.journey.mode=2; // Loop. The other factory Biomes inherit Off.
  patch.journey.length=arrangement.length;
  for(unsigned i=0;i<arrangement.length;i++)patch.journey.section[i]=arrangement.section[i];
  break;
 }
}
static void out_all_off(void);
/* 2.2 development (user feedback): KEEP SETTING. A Biome chosen by hand brings its character
 * (voices, rhythms, space) and keeps the current setting: root, scale, transpose, tempo, chords. */
static bool keep_setting;
static __attribute__((noinline)) void biome_into(md_patch &dst,int id){
 const int32_t was[6]={patch.root,patch.scale,patch.transpose,patch.tempo,patch.chord_pattern,patch.chord_bars};
 factory_scene(dst,id);
 if(keep_setting){dst.root=was[0];dst.scale=was[1];dst.transpose=was[2];dst.tempo=was[3];dst.chord_pattern=was[4];dst.chord_bars=was[5];}
}
/* 2.2 development (user feedback): BIOME CHANGE. While playing, a Biome chosen by hand can arrive
 * on the next bar (NEXT BAR) or hand over track by track (HANDOVER: pads and bass, then leads,
 * then rhythm, one bar apart). Changing voices dissolve over the last sixteenth like a Journey
 * section; echo, room, grains and tape keep sounding. The staging patch is `previous`, rebuilt
 * from the Biome ID at every stage, so edits in between cannot corrupt it. */
static const uint8_t admission_weight[]={8,13,8,18,10,27,17,11,8,30,10,12,10,12,10,10,17,14,10,10,10,10,10,10};
static_assert(sizeof admission_weight==MD_MODELS,"one admission weight per engine");
static uint8_t soft_mode,soft_stage,soft_count,soft_mask[3];
static int soft_biome=-1;
static void soft_begin(int id);
static void soft_apply();
static void scene(int id) {
 out_all_off();
 journey_active=journey_prepared=0;runtime_eco=false;soft_biome=-1;biome_into(patch,id);


 clear_audio();for(int i=0;i<6;i++){reset_voice(i);}for(int j=0;j<4;j++)smooth[j]=unit(patch.macro[j]);
 previous=patch;step=0;untilstep=0;seqrng=patch.seed;pending_biome=-1;md_mark_saved();
}
static void biome_change(int id){if(playing&&soft_mode&&!journey_active){pending_biome=-1;soft_begin(id);}else scene(id);}
static void tone_preset(int track,int id) {
 id=bound(id,0,MD_TONE_PRESETS-1);const TonePreset &a=tone_bank[id];md_track &t=patch.track[track];
 t.model=a.identity&31u;t.filter=(a.identity>>5)&3u;
 t.tone=(a.identity>>7)&1023u;t.harm=a.identity>>17;
 t.decay=a.contour&1023u;t.attack=(a.contour>>10)&1023u;t.shape=a.contour>>20;
 t.cutoff=a.colour&1023u;t.resonance=(a.colour>>10)&1023u;t.breathe=a.colour>>20;
 patch.tone_preset[track]=id;patch.edited_mask=(patch.edited_mask&~(2u<<track))|1u;reset_voice(track);
}
static int32_t *track_field(md_track &t,int c) {
switch(c){case MD_MODEL:return &t.model;case MD_TONE:return &t.tone;case MD_HARM:return &t.harm;case MD_DECAY:return &t.decay;case MD_LEVEL:return &t.level;case MD_SEND:return &t.send;case MD_HITS:return &t.hits;case MD_LENGTH:return &t.length;case MD_ROTATE:return &t.rotate;case MD_PROB:return &t.prob;case MD_NOTE:return &t.note;case MD_PAN:return &t.pan;case MD_MUTE:return &t.mute;default:return &t.speed;}
}
/* 2.2 development (user feedback 1): the sequencer's own notes out over USB MIDI, one channel per
 * track (0 off). The audio owner writes USB-MIDI packets into a single-writer ring; the main loop
 * sends them (md_midi_out_read). Keyboard and incoming MIDI notes are not echoed (no loops).
 * Monophonic per track: a new note ends the last one; stop, mute, a new Biome and a new channel
 * end any note still sounding. */
static uint8_t out_channel[6],out_sent[6];static int16_t out_note[6]={-1,-1,-1,-1,-1,-1};static float out_left[6];
static uint32_t out_ring[64];static volatile unsigned out_head,out_tail;
static void out_push(uint32_t pkt){const unsigned h=out_head;if(h-out_tail>=64u)return;out_ring[h&63u]=pkt;fence();out_head=h+1;}
static void out_off(unsigned i){if(out_note[i]<0)return;out_push(0x08u|((0x80u|(out_sent[i]-1u))<<8)|((unsigned)out_note[i]<<16));out_note[i]=-1;}
// This control-only helper is shared by transport/scene changes, not expanded
// at every call site. Keep the six Note-Offs outside the steady audio path.
static __attribute__((noinline)) void out_all_off(void){for(unsigned i=0;i<6;i++)out_off(i);}
static void out_on(unsigned i,float note,float velocity,float frames){
 if(!out_channel[i]||patch.track[i].mute||(patch.kit.enabled&&i>=3))return;
 out_off(i);
 // Keep one Note-Off slot per track even if the USB consumer stops draining.
 // A skipped Note-On must never be remembered as an externally sounding note.
 if(out_head-out_tail>=64u-MD_TRACKS)return;
 const int n=bound((int)(note+.5f),0,127),v=bound((int)(velocity*127.f+.5f),1,127);
 out_sent[i]=out_channel[i];
 out_push(0x09u|((0x90u|(out_sent[i]-1u))<<8)|((unsigned)n<<16)|((unsigned)v<<24));
 out_note[i]=(int16_t)n;out_left[i]=frames>64.f?frames:64.f;
}
static void fire(unsigned i,float note,float velocity) {
 if(patch.kit.enabled){if(i>=4)return;if(i==3){kit_fire(kit_note((int)note),velocity);stats.events[3]++;stats.last_note[3]=(int)note;return;}}
 note_touch[i]=0;
 link_hit(i,velocity);seq_gate[i]=0;Voice &v=voices[i];v.trigger=1;v.midi_gate=0;v.target_note=note;if(!patch.ext[i].glide||!v.active)v.note=note;v.velocity=velocity;v.active=1;
 stats.events[i]++;stats.last_note[i]=(int)note;
 if((int)i==patch.duck_source&&!patch.track[i].mute)duckenv=1;
}
// Progressions use scale degrees, not arbitrary chromatic transposition.
static const uint8_t journey_degrees[9][8]={
 {0,0,0,0,0,0,0,0},{0,3,0,4},{0,5,3,4},{0,6,3,0},{0,2,5,3},
 {0,0,5,5,3,1,3,0}, // Dusk: leave room before gently returning home.
 {0,4,5,2,3,0,1,4}, // Lanterns: a longer answering phrase.
 {0,1,3,4,5,4,3,1}, // Tidal: rise and return through neighbouring roots.
 {0,0,3,0,5,3,4,0}  // Still Water: a strong recurring tonic.
};
// Control-rate curves reuse lookup tables; no new audio-rate oscillator or buffer.
static float tidal_wave(float phase,unsigned shape){
 if(shape==0)return 1-4*fabsf(phase-.5f);
 if(shape<3){float w=plaits::Sine(phase+.75f);if(shape==2){float u=(w+1)*.5f;w=2*u*u-1;}return w;}
 float u=phase<.25f?phase*4:(1-phase)*(4.f/3.f);return 2*u*u*(3-2*u)-1;
}
static unsigned sequence_position(unsigned i,unsigned local){
 const md_sequence &q=patch.sequence[i];unsigned n=q.length,at=local%n;
 switch(patch.motion.direction[i]){
 case 1:at=n-1-at;break;
 case 2:if(n>1){unsigned k=local%(2*n-2);at=k<n?k:2*n-2-k;}break;
 case 3:at=(at+local/n)%n;break;
 case 4:{unsigned r=patch.seed^((i+1)*0x9e3779b9u)^((at+1)*0x85ebca6bu);at=random_word(r)%n;break;}
 }
 return (at+patch.motion.shift[i])%n;
}
static int chord_offset(){
 static const int intervals[13][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10},{0,2,4,5,7,9,11},{0,2,3,5,7,9,10},{0,2,3,5,7,8,10},{0,2,4,5,7,9,11},{0,2,4,6,8,10,12},{0,1,3,5,7,8,10},{0,2,3,6,7,9,10},{0,2,3,5,7,8,11},{0,2,3,6,7,8,11},{0,2,4,5,7,8,11},{0,1,4,5,7,8,11}};
 // Pentatonic melodies retain functional fourth/fifth roots from their parent
 // major/minor scale. Quantization still constrains each resulting voice.
 int d=journey_degrees[patch.chord_pattern][((step-(journey_active?journey_chord_origin:0))/(16*patch.chord_bars))%(patch.chord_pattern>4?8:4)];
 int offset=intervals[patch.scale][d];return patch.motion.compact&&offset>6?offset-12:offset;
}
static void capture_note(unsigned i,int note,float velocity){
 if(!record_mode||i!=(unsigned)record_track||(record_mode==2&&!playing))return;
 md_sequence &q=patch.sequence[i];q.enabled=1;
 unsigned at=record_mode==1?q.edit:sequence_position(i,(step?step-1:0)/patch.track[i].speed);
 const unsigned rule=md_step_rule(&q.steps[at]);
 q.steps[at]={(uint8_t)bound(note,24,96),(uint8_t)bound((int)(velocity*100+.5f),1,100),100,85};
 md_step_set_rule(&q.steps[at],rule);
 record_note=note;record_step=at;record_start=stats.frames;patch.edited_mask|=1;
 if(record_mode==1)q.edit=(at+1)%q.length;
}
static void capture_off(unsigned i,int note){
 if(record_mode==2&&i==(unsigned)record_track&&record_step>=0&&record_note==note){
  float duration=MD_RATE*60.0f/(tempo_f()*4.0f)*patch.track[i].speed;
  // Convert before multiplication: the integer product overflowed after a
  // sixteen-minute hold, occasionally turning a long note into a tiny gate.
  float percent=(float)(stats.frames-record_start)*100.f/duration;
  md_step &s=patch.sequence[i].steps[record_step];
  s.gate=(s.gate&128u)|(uint8_t)fmaxf(1.f,fminf(percent,100.f));
  record_step=record_note=-1;
 }
}
static void generate_sequence(unsigned i,int kind){
 md_sequence &q=patch.sequence[i];previous=patch;q.generator=bound(kind,0,7);q.enabled=1;
 unsigned r=patch.seed^(i+1)*0x9e3779b9u;static const int melody[]={0,7,12,3,5,7,10,12};
 for(int k=0;k<MD_STEPS;k++){unsigned roll=random_word(r);bool on=k%4==0||(q.generator==1?k%2==0:q.generator==2?k%3!=1:q.generator==3?roll%100<45:false);
  int offset=q.generator==0?(k%8?7:0):melody[q.generator==1?(k/2)%8:(roll>>12)%8];
  int note=md_quantize(patch.track[i].note+offset,patch.root,patch.scale);
  if(q.generator>=4){
   static const int petals[8]={0,7,2,9,5,7,2,0},tide[8]={0,2,5,7,9,7,5,2},answer[8]={0,7,12,14,12,7,5,2};
   int off=0;
   if(q.generator==4){on=k%8==0||k%8==3||k%8==6;off=petals[(k/3)%8];}
   else if(q.generator==5){on=k%3!=2;off=tide[(k/2)%8];}
   else if(q.generator==6){on=md_hit(k,5,q.length,0);off=petals[(roll>>12)%8];}
   else {on=k%8<3||k%8==5;off=answer[k%8];}
   note=md_quantize(patch.track[i].note+off,patch.root,patch.scale);
   q.steps[k]={(uint8_t)(on?note:0),(uint8_t)(k%8?58+roll%17:78),(uint8_t)(q.generator==6?88:100),(uint8_t)(q.generator==7?78:96)};
  }else q.steps[k]={(uint8_t)(on?note:0),(uint8_t)(k%4?65+roll%16:85),(uint8_t)(q.generator==3?80:100),(uint8_t)(q.generator==2?50:90)};
 }
}
static void sequence_tool(unsigned command,unsigned track,int value){
 md_sequence &q=patch.sequence[track];
 const unsigned kind=(command==MD_SEQ_TRANSPOSE?value>=64:value!=0)?2:1;
 unsigned result=0;
 if((patch.kit.enabled&&track>=3)||
    (command==MD_SEQ_TRANSPOSE?(value<0||(value>48&&value<64)||value>112):(value<0||value>1)))
  result=MD_SEQ_UNAVAILABLE;
 else if(command==MD_SEQ_COPY){
  if(kind==1)sequence_clipboard.steps[0]=q.steps[q.edit];
  else sequence_clipboard=q;
  sequence_clip_kind=kind;result=MD_SEQ_COPIED;
 }else if(command==MD_SEQ_PASTE){
  if(sequence_clip_kind!=kind)result=MD_SEQ_WRONG_CLIP;
  else{
   previous=patch;
   if(kind==1)q.steps[q.edit]=sequence_clipboard.steps[0];
   else{unsigned cursor=q.edit;q=sequence_clipboard;q.edit=bound(cursor,0,q.length-1);}
   result=MD_SEQ_PASTED;
  }
 }else if(command==MD_SEQ_TRANSPOSE){
  const unsigned first=kind==1?q.edit:0,end=kind==1?first+1:q.length;
  const int delta=value-(kind==2?64:0)-24;
  bool notes=false;
  for(unsigned i=first;i<end;i++)if(q.steps[i].note){
   const int note=q.steps[i].note+delta;
   if(note<24||note>96){result=MD_SEQ_NOTE_LIMIT;break;}
   notes=true;
  }
  if(!result){
   if(!notes)result=MD_SEQ_NO_NOTES;
   else if(delta){
    previous=patch;
    for(unsigned i=first;i<end;i++)if(q.steps[i].note)q.steps[i].note+=delta;
    result=MD_SEQ_TRANSPOSED;
   }
  }
 }else if(q.length>1){
  previous=patch;
  for(unsigned i=0;i<q.length/2;i++){
   unsigned other=q.length-1-i;
   md_step step=q.steps[i];q.steps[i]=q.steps[other];q.steps[other]=step;
  }
  result=MD_SEQ_MIRRORED;
 }
 if(result>=MD_SEQ_PASTED&&result<=MD_SEQ_MIRRORED)patch.edited_mask|=1u;
 if(result)sequence_notice=((sequence_notice+16)&~15u)|result;
}
static void sequence_expression(int track,int value){
 if(track<0||track>=MD_TRACKS||(patch.kit.enabled&&track>=3))return;
 const int mode=value>=MD_EXPRESSION_GATE_OFFSET?MD_EXPRESSION_GATE:MD_EXPRESSION_ACCENT;
 const int amount=value-(mode==MD_EXPRESSION_GATE?MD_EXPRESSION_GATE_OFFSET:0);
 if(amount<0||amount>100)return;
 md_sequence &phrase=patch.sequence[track];
 unsigned result=MD_SEQ_UNAVAILABLE;
 if(phrase.enabled){
  result=MD_SEQ_NO_NOTES;
  bool changed=false;
  for(unsigned i=0;i<phrase.length;i++){
   md_step &step=phrase.steps[i];
   if(!step.note)continue;
   const int current=mode==MD_EXPRESSION_GATE?md_step_gate(&step):step.velocity;
   const int next=md_phrase_value(&step,i,mode,amount);
   if(next==current){if(!changed)result=MD_SEQ_NO_CHANGE;continue;}
   if(!changed){previous=patch;changed=true;}
   if(mode==MD_EXPRESSION_GATE)step.gate=(step.gate&128u)|next;
   else step.velocity=next;
  }
  if(changed){
   patch.edited_mask|=1u;
   result=mode==MD_EXPRESSION_GATE?MD_SEQ_ARTICULATED:MD_SEQ_ACCENTED;
  }
 }
 sequence_notice=((sequence_notice+16)&~15u)|result;
}
static void sequence_rewind(){
 fill_state=0;
 memset(sequence_head,0,sizeof sequence_head);
 sequence_hit=0;
}
static void sequence_visit(unsigned track,unsigned step){
 sequence_head[track]=step+1;
 sequence_hit&=~(1u<<track);
}
static void apply(Command e);
#include "creative.inc"
static int macro_controller_value(unsigned k);
#include "midi_learn.inc"
static void itinera_enter_first();static void itinera_nudge(unsigned k,int v);static void itinera_keep();
#include "itinera.inc"
/* Learned controllers pick up the listener's offset, not the scene's own
 * moving macro. Direct K1-K4 and CC controllers share the same neutral 500. */
static int macro_controller_value(unsigned k){return itinera.active?500+itinera.user[k]:patch.macro[k];}
static void apply(Command e) {
 if(!md_command_allowed(e.c))return;
 if(itinera.active&&e.c==MD_PLAY&&e.v&&e.v!=2)return;
 if(e.c==MD_ITINERA_START){itinera_begin(e.t,e.v);return;}
 if(e.c>=MD_SEQ_COPY&&e.c<=MD_SEQ_MIRROR){sequence_tool(e.c,bound(e.t,0,MD_TRACKS-1),e.v);return;}
 if(e.c==MD_SEQ_EXPRESSION){sequence_expression(e.t,e.v);return;}
 if(e.c==MD_FILL){
  if(playing)fill_state=(fill_state&1u)|((e.v<0?!(fill_state&2u):e.v!=0)?2u:0u);
  return;
 }
 if(e.c==MD_SEQ_RULE){
  if(e.t<0||e.t>=MD_TRACKS||(patch.kit.enabled&&e.t>=3)||e.v<MD_RULE_ALWAYS||e.v>MD_RULE_FILL)return;
  md_sequence &q=patch.sequence[e.t];
  md_step &s=q.steps[q.edit];
  if(md_step_rule(&s)!=(unsigned)e.v){previous=patch;md_step_set_rule(&s,e.v);patch.edited_mask|=1u;}
  return;
 }
 if(e.c>=MD_LEARN_TARGET&&e.c<=MD_LEARN_ENABLE){learn_edit(e);return;}
 learn_parameter_changed(e);
 int i=bound(e.t,0,5),v=e.v;md_track &t=patch.track[i];
 if(patch.kit.enabled&&i==3&&(e.c==MD_MODEL||e.c==MD_TONE_PRESET))return;
 // Playback, preview notes and emergency silence do not change a preset.
 if(e.c!=MD_MIDI_ROUTE&&e.c!=MD_MIDI_MACRO&&e.c!=MD_SCENE_CONFIRM&&e.c!=MD_KIT_TRIGGER&&e.c!=MD_KIT_PAD&&e.c!=MD_KIT_STEP&&e.c!=MD_SEQ_STEP&&e.c!=MD_BASS_STEP&&e.c!=MD_SCENE&&e.c!=MD_RETURN&&e.c!=MD_PLAY&&e.c!=MD_TRIGGER&&e.c!=MD_PANIC&&e.c!=MD_MUTATE&&e.c!=MD_RECORD&&e.c!=MD_NOTE_OFF)patch.edited_mask|=1u;
 if(e.c==MD_MODEL||e.c==MD_TONE||e.c==MD_HARM||e.c==MD_DECAY||e.c==MD_ATTACK||(e.c>=MD_SHAPE&&e.c<=MD_BREATHE))patch.edited_mask|=2u<<i;
 if(e.c<=MD_SPEED) {
  int32_t *p=track_field(t,e.c);
  if(e.c==MD_MODEL) { *p=bound(v,0,MD_MODELS-1);patch.tone_preset[i]=-1;reset_voice(i); }
  else if(e.c==MD_LENGTH) { t.length=bound(v,1,32);t.hits=bound(t.hits,0,t.length);t.rotate%=t.length; }
  else if(e.c==MD_HITS) t.hits=bound(v,0,t.length);
  else if(e.c==MD_ROTATE) t.rotate=bound(v,0,t.length-1);
  else if(e.c==MD_NOTE) t.note=bound(v,24,96);
  else if(e.c==MD_MUTE) {t.mute=!!v;if(t.mute)out_off(i);}
  else if(e.c==MD_SPEED) t.speed=bound(v,1,8);
  else *p=bound(v,0,1000);
 } else switch(e.c) {
 case MD_LINK_SOURCE:patch.links[bound(e.t,0,3)].source=bound(v,0,5);break;
 case MD_LINK_SIGNAL:patch.links[bound(e.t,0,3)].signal=bound(v,0,3);break;
 case MD_LINK_DEST:patch.links[bound(e.t,0,3)].destination=bound(v,0,5);break;
 case MD_LINK_TARGET:patch.links[bound(e.t,0,3)].target=bound(v,0,5);break;
 case MD_LINK_DEPTH:patch.links[bound(e.t,0,3)].depth=bound(v,-1000,1000);break;
 case MD_LINK_RELEASE:patch.links[bound(e.t,0,3)].release=bound(v,0,1000);break;
 case MD_DRIFT_MODE:if(!v||patch.drift.captured==3){patch.drift.mode=bound(v,0,2);drift_phase=unit(patch.drift.position);drift_live=drift_phase;drift_direction=1;}break;
 case MD_DRIFT_POSITION:patch.drift.position=bound(v,0,1000);break;
 case MD_DRIFT_RATE:patch.drift.rate=bound(v,0,1000);break;
 case MD_DRIFT_DEPTH:patch.drift.depth=bound(v,0,1000);break;
 case MD_DRIFT_CAPTURE:if(!patch.drift.mode){previous=patch;drift_capture(bound(v,0,1));}break;
 case MD_JOURNEY_MODE:patch.journey.mode=bound(v,0,2);break;
 case MD_JOURNEY_LENGTH:patch.journey.length=bound(v,1,8);break;
 case MD_JOURNEY_BIOME:patch.journey.section[bound(e.t,0,7)].biome=v<0?65535:bound(v,0,MD_V21_BIOMES-1);break; /* 2.2: a stored Journey keeps 2.1 IDs */
 case MD_JOURNEY_BARS:patch.journey.section[bound(e.t,0,7)].bars=bound(v,1,64);break;
 case MD_JOURNEY_REPEATS:patch.journey.section[bound(e.t,0,7)].repeats=bound(v,1,16);break;
 case MD_JOURNEY_TRANSPOSE:patch.journey.section[bound(e.t,0,7)].transpose=bound(v,-24,24);break;
 case MD_JOURNEY_KEEP_TRACK:{auto &a=patch.journey.section[bound(e.t,0,7)];unsigned bit=1u<<bound(v/2,0,5);a.keep=(a.keep&~bit)|((v&1)?bit:0);break;}
 case MD_JOURNEY_KEEP:patch.journey.section[bound(e.t,0,7)].keep=bound(v,0,63);break;
 case MD_MACRO:if(itinera.active){itinera_nudge(bound(e.t,0,3),v);break;}patch.macro[bound(e.t,0,3)]=bound(v,0,1000);break;
 case MD_TEMPO:patch.tempo=bound(v,30,220);break;
 // A fresh local start gets its full external-clock wait after a prior timeout.
 case MD_PLAY:if(v==2){itinera_keep();break;}out_all_off();while(!v&&soft_biome>=0)soft_apply();if(!v){fill_state=0;journey_stop();}else if(!playing){clock_age=0;journey_start();}playing=!!v;if(!playing){record_mode=0;record_note=record_step=-1;}memset(repeats,0,sizeof repeats);if(playing){step=0;untilstep=0;seqrng=patch.seed;sequence_rewind();}break;
 case MD_LOCK:patch.locked=!!v;break;
 case MD_MUTATE: previous=patch;patch.edited_mask|=1u;patch.seed=random_word(seqrng)|1;for(unsigned j=0;j<6;j++)patch.track[j].rotate=random_word(seqrng)%patch.track[j].length;break;
 case MD_RETURN:soft_biome=-1;journey_prepared=0;runtime_eco=false;record_mode=0;record_note=record_step=-1;patch=previous;seqrng=patch.seed;for(unsigned j=0;j<6;j++){repeats[j].remaining=0;if(voices[j].model!=patch.track[j].model)reset_voice(j);}break;
 case MD_SCENE:v=bound(v,0,MD_BIOMES-1);if(md_unsaved()&&soft_biome<0)pending_biome=v;else biome_change(v);break;
 case MD_SCENE_CONFIRM:if(v==1&&pending_biome>=0)biome_change(pending_biome);else pending_biome=-1;break;
 case MD_KIT_MODE:if(patch.kit.enabled!=!!v){patch.kit.enabled=!!v;kit_reset();for(int j=3;j<6;j++){out_off(j);reset_voice(j);}record_mode=0;}break;
 case MD_KIT_BANK:patch.kit.bank=bound(v,0,4);break; // Existing sample voices keep their source and decay naturally.
 case MD_KIT_PAD:patch.kit.pad=bound(v,0,7);break;
 case MD_KIT_STEP:patch.kit.step=bound(v,0,patch.kit.length-1);break;
 case MD_KIT_GATE:{uint32_t bit=1u<<patch.kit.step;if(v)patch.kit.gates[patch.kit.pad]|=bit;else patch.kit.gates[patch.kit.pad]&=~bit;break;}
 case MD_KIT_TUNE:patch.kit.pads[patch.kit.pad].tune=bound(v,-12,12);break;
 case MD_KIT_LEVEL:patch.kit.pads[patch.kit.pad].level=bound(v,0,1000);break;
 case MD_KIT_CUTOFF:patch.kit.pads[patch.kit.pad].cutoff=bound(v,0,1000);break;
 case MD_KIT_SEND:patch.kit.pads[patch.kit.pad].send=bound(v,0,1000);break;
 case MD_KIT_DECAY:patch.kit.pads[patch.kit.pad].decay=bound(v,0,1000);break;
 case MD_KIT_HISS:patch.kit.hiss=bound(v,0,100);break;
 case MD_KIT_GENERATE:previous=patch;kit_pattern(patch.kit,bound(v,0,4));break;
 case MD_KIT_LENGTH:patch.kit.length=bound(v,1,32);patch.kit.step=bound(patch.kit.step,0,patch.kit.length-1);break;
 case MD_KIT_TRIGGER:if(patch.kit.enabled){unsigned pad=bound(v,0,7);kit_fire(pad,.8f);stats.events[3]++;if(record_mode&&record_track==3){unsigned at=record_mode==1?patch.kit.step:((step?step-1:0)/patch.track[3].speed)%patch.kit.length;patch.kit.gates[pad]|=1u<<at;if(record_mode==1)patch.kit.step=(at+1)%patch.kit.length;}}break;
 case MD_TONE_PRESET:tone_preset(i,v);break;
 case MD_GRAIN_NOISE:patch.grain_noise=bound(v,0,1000);break;
 case MD_LFO2_TARGET:patch.motion.target2[i]=bound(v,0,MD_LFO_TARGET_COUNT-1);break;
 case MD_LFO2_SHAPE:patch.motion.wave2[i]=bound(v,0,3);break;
 case MD_LFO2_DEPTH:patch.motion.depth2[i]=bound(v,0,1000);break;
 case MD_LFO2_RATE:patch.motion.rate2[i]=bound(v,0,1000);break;
 case MD_MIDI_ROUTE:md_set_midi_route(i,bound(v,0,16));break;
 case MD_MIDI_MACRO:md_set_midi_macro_enabled(v);break;
 case MD_SEQ_DIRECTION:patch.motion.direction[i]=bound(v,0,4);break;
 case MD_SEQ_FEEL:patch.motion.feel[i]=bound(v,0,100);break;
 case MD_SEQ_SHIFT:patch.motion.shift[i]=bound(v,0,31);break;
 case MD_LFO_TARGET:patch.motion.target[i]=bound(v,0,MD_LFO_TARGET_COUNT-1);break;
 case MD_LFO_SHAPE:patch.motion.wave[i]=bound(v,0,3);break;
 case MD_CHORD_COMPACT:patch.motion.compact=!!v;break;
 case MD_SEQ_ENABLE:patch.sequence[i].enabled=!!v;break;
 case MD_SEQ_STEP:patch.sequence[i].edit=bound(v,0,patch.sequence[i].length-1);break;
 case MD_SEQ_NOTE:patch.sequence[i].steps[patch.sequence[i].edit].note=v<24?0:bound(v,24,96);break;
 case MD_SEQ_VELOCITY:patch.sequence[i].steps[patch.sequence[i].edit].velocity=bound(v,1,100);break;
 case MD_SEQ_CHANCE:{md_step &s=patch.sequence[i].steps[patch.sequence[i].edit];s.chance=(s.chance&128u)|bound(v,0,100);break;}
 case MD_SEQ_GATE:{md_step &s=patch.sequence[i].steps[patch.sequence[i].edit];s.gate=(s.gate&128u)|bound(v,1,100);break;}
 case MD_SEQ_LENGTH:patch.sequence[i].length=bound(v,1,32);patch.sequence[i].edit%=patch.sequence[i].length;break;
 case MD_SEQ_GENERATE:generate_sequence(i,v);break;
 case MD_RECORD:if(!record_mode&&v)previous=patch;record_mode=bound(v,0,2);record_track=i;record_note=record_step=-1;break;
 case MD_CHORD_PATTERN:patch.chord_pattern=bound(v,0,8);break;
 case MD_CHORD_BARS:patch.chord_bars=bound(v,1,16);break;
 case MD_CHORD_FOLLOW:if(v)patch.chord_tracks|=1<<i;else patch.chord_tracks&=~(1<<i);break;
 case MD_CHORD_TRACKS:patch.chord_tracks=bound(v,0,63);break;
 case MD_FREEZE:patch.freeze=!!v;break;
 case MD_GRAIN_SIZE:patch.grain_size=bound(v,0,1000);break;
 case MD_GRAIN_POS:patch.grain_pos=bound(v,0,1000);break;
 case MD_FEEDBACK:patch.feedback=bound(v,0,850);break;
 case MD_QUALITY:{const bool was=quality_on();runtime_eco=false;patch.quality=bound(v,0,1);if(was!=quality_on())eco_settle();}break;
 case MD_TRIGGER:if(patch.kit.enabled&&i==3){kit_fire(kit_note(v),.8f);stats.events[3]++;break;}capture_note(i,bound(v,24,96),.8f);fire(i,(float)(t.model>=MD_FM?md_quantize(v+patch.transpose,patch.root,patch.scale):bound(v,24,96)),.8f);break;
 case MD_NOTE_OFF:if(patch.kit.enabled&&i>=3)break;capture_off(i,bound(v,24,96));break;
 case MD_ATTACK:t.attack=bound(v,0,1000);break;
 case MD_RATCHET:t.ratchet=bound(v,1,4);break;
 case MD_SHAPE:t.shape=bound(v,0,1000);break;
 case MD_CUTOFF:t.cutoff=bound(v,0,1000);break;
 case MD_RESONANCE:t.resonance=bound(v,0,1000);break;
 case MD_FILTER:if(t.filter!=bound(v,0,3)){v=bound(v,0,3);t.filter=v;voices[i].filter1.Reset();voices[i].filter2.Reset();voices[i].fade=0;}break;
 case MD_BREATHE:t.breathe=bound(v,0,1000);break;
 case MD_ACCENT:t.accent=bound(v,0,1000);break;
 case MD_SWING:patch.swing=bound(v,0,600);break;
 case MD_ROOT:patch.root=bound(v,0,11);break;
 case MD_SCALE:patch.scale=bound(v,0,12);break;
 case MD_TRANSPOSE:patch.transpose=bound(v,-24,24);break;
 case MD_GRAIN_PITCH:patch.grain_pitch=bound(v,-24,24);break;
 case MD_REVERSE:patch.reverse=!!v;break;
 case MD_DUCK_SOURCE:patch.duck_source=bound(v,-1,5);break;
 case MD_DUCK:patch.duck_amount=bound(v,0,1000);break;
 case MD_DUCK_RELEASE:patch.duck_release=bound(v,0,1000);break;
 case MD_DELAY_SEND:patch.ext[i].delay_send=bound(v,0,1000);break;
 case MD_REVERB_SEND:patch.ext[i].reverb_send=bound(v,0,1000);break;
 case MD_DELAY_TIME:patch.delay_ms=bound(v,40,350);break;
 case MD_DELAY_FB:patch.delay_fb=bound(v,0,800);break;
 case MD_REVERB_TIME:patch.reverb_time=bound(v,0,850);break;
 case MD_FX_LEVEL:patch.fx_level=bound(v,0,1000);break;
 case MD_CLOCK_SOURCE:out_all_off();clock_seen=0;clock_tempo=0;clock_source=bound(v,0,3);fill_state=0;playing=0;record_mode=0;record_note=record_step=-1;clock_pulse=0;clock_age=clock_last=0;clock_interval=0;break;
 case MD_MIDI_QUANTIZE:midi_quantize=!!v;break;
 case MD_BASS_MODE:patch.ext[i].bass_mode=bound(v,0,2);break;
 case MD_BASS_PATTERN:{static const int motifs[4][16]={{0,-1,0,-1,7,-1,0,-1,0,-1,3,-1,7,-1,10,-1},{0,0,-1,7,0,-1,3,-1,0,0,7,-1,10,-1,7,-1},{0,-1,-1,-1,0,-1,7,-1,3,-1,-1,-1,7,-1,5,-1},{0,-1,12,7,0,3,-1,7,0,-1,10,7,5,-1,3,-1}};patch.ext[i].pattern=bound(v,0,3);memcpy(patch.ext[i].notes,motifs[patch.ext[i].pattern],sizeof motifs[0]);break;}
 case MD_GLIDE:patch.ext[i].glide=bound(v,0,1000);break;
 case MD_MARBLES:patch.ext[i].marbles=bound(v,0,1000);break;
 case MD_TIDES:patch.ext[i].tides=bound(v,0,1000);break;
 case MD_TIDES_RATE:patch.ext[i].tides_rate=bound(v,0,1000);break;
 case MD_WARPS:patch.ext[i].warps=bound(v,0,1000);break;
 case MD_BASS_STEP:patch.ext[i].edit_step=bound(v,0,15);break;
 case MD_BASS_VALUE:patch.ext[i].notes[patch.ext[i].edit_step]=bound(v,-1,24);break;
 case MD_PANIC:out_all_off();while(soft_biome>=0)soft_apply();journey_stop();playing=0;clear_audio();for(unsigned j=0;j<6;j++)reset_voice(j);break;
 }
}
#include "journey.inc"
static bool step_rule_allows(unsigned track,const md_sequence &q,const md_step &s,unsigned local){
 const unsigned rule=md_step_rule(&s);
 if(rule==MD_RULE_ALWAYS)return true;
 if(rule==MD_RULE_FILL)return (fill_state&1u)!=0;
 const unsigned period=patch.motion.direction[track]==2&&q.length>1?2*q.length-2:q.length;
 const unsigned cycle=local/period;
 return rule==MD_RULE_FIRST?cycle==0:(cycle&1u)!=0;
}
/* BIOME CHANGE stages (see biome_into): masks of the tracks that change at each bar, in order. */
static __attribute__((noinline)) void soft_begin(int id){
 biome_into(previous,id);
 soft_biome=id;soft_stage=soft_count=0;journey_prepared=0;journey_fade_mask=0;
 if(soft_mode>=2&&!previous.kit.enabled&&!patch.kit.enabled){
  unsigned by_kind[3]={0,0,0};
  for(unsigned i=0;i<6;i++)by_kind[itinera_kind(previous,i)]|=1u<<i;
  for(int s=2;s>=0;s--)if(by_kind[s])soft_mask[soft_count++]=by_kind[s];
  /* A stage that mixes old and new voices must stay under the admission guard (quantum): a heavier
   * mix would drop the rack to ECO for its bars. Such a stage waits and changes with the next one. */
  for(unsigned s=0;s+1<soft_count;){
   unsigned now=0,cost=12+(fx_active||previous.fx_level?10:0);
   for(unsigned k=0;k<=s;k++)now|=soft_mask[k];
   for(unsigned i=0;i<6;i++){const md_patch &p=now&(1u<<i)?previous:patch;if(!p.track[i].mute)cost+=admission_weight[p.track[i].model]+(p.ext[i].warps?2:0);}
   if(cost<=85){s++;continue;}
   soft_mask[s]|=soft_mask[s+1];for(unsigned k=s+1;k+1<soft_count;k++)soft_mask[k]=soft_mask[k+1];soft_count--;
  }
 }
 if(!soft_count){soft_mask[0]=63;soft_count=1;}
}
// The last sixteenth before a stage: its changing voices dissolve (rendered like a Journey section end).
static void soft_prepare(){
 journey_fade_mask=soft_mask[soft_stage];journey_prepared=1;
 journey_fade_step=1.f/(MD_RATE*60.f/(tempo_f()*4.f)*(1+(step%2?-1:1)*unit(patch.swing)*.5f));
}
static __attribute__((noinline)) void soft_apply(){
 unsigned now=0;for(unsigned s=0;s<=soft_stage;s++)now|=soft_mask[s];
 const unsigned changing=soft_mask[soft_stage];
 biome_into(previous,soft_biome);
 for(unsigned i=0;i<6;i++)if(!(now&(1u<<i)))journey_copy_track(previous,patch,i);
 learn_reset();patch=previous;runtime_eco=false;journey_prepared=0;journey_fade_mask=0;seqrng=patch.seed;
 for(unsigned i=0;i<6;i++)if((changing&(1u<<i))||voices[i].model!=patch.track[i].model){out_off(i);reset_voice(i);repeats[i].remaining=0;seq_gate[i]=0;}
 if(changing==63u)kit_reset();
 if(++soft_stage>=soft_count){soft_biome=-1;previous=patch;md_mark_saved();}
}
/* 2.4: a dream is never silent for long. Its patterns are sparse and every start has a new seed, so a dream could open
 * with half a minute of nothing, or sit without a note for 40 s in its middle (measured: up to 30 s and 59 s). When no
 * track has sounded for a while, one soft note plays on a pad or bass track: its own base note, in the scene's scale. The
 * first note comes within 1.5 s of the start, later ones after 8 s of nothing. No random numbers are drawn, so the
 * dream's own sequence is not moved. Not during the closing fade, and never inside a scene's rest. */
static void itinera_heartbeat(float step_frames){
 uint32_t events=0;for(unsigned j=0;j<6;j++)events+=stats.events[j];
 if(events!=itinera.events_seen){itinera.events_seen=events;itinera.idle_steps=0;itinera.heard=1;return;}
 if(itinera_remaining()<itinera_fade_frames)return;
 const unsigned limit=(unsigned)((itinera.heard?8.f:1.5f)*MD_RATE/step_frames);
 if(++itinera.idle_steps<limit)return;
 unsigned pick=6;
 for(unsigned pass=0;pass<2&&pick==6;pass++)for(unsigned i=0;i<(patch.kit.enabled?3u:6u);i++){
  const md_track &t=patch.track[i];
  if(t.mute||t.level==0||itinera_resting(i)||(pass==0&&itinera_role(patch,i)!=2))continue;
  pick=i;break;
 }
 if(pick==6)return;
 const md_track &t=patch.track[pick];
 int note=t.note;if(t.model>=MD_FM)note=md_quantize(note+patch.transpose,patch.root,patch.scale);
 const float velocity=.7f,length=step_frames*8.f;
 sequence_hit|=1u<<pick;
 fire(pick,(float)note,velocity);
 out_on(pick,(float)note,velocity,length);
 if(md_has_envelope(t.model)){voices[pick].midi_gate=1;seq_gate[pick]=length;}
 itinera.idle_steps=0;
}
static void tick() {
 if(journey_active&&!journey_tick())return;
 if(soft_biome>=0&&!(step&15u)&&journey_prepared)soft_apply();
 if(!(step&15u))fill_state>>=1;
 float duration=MD_RATE*60.0f/(tempo_f()*4.0f)*(1+(step%2?-1:1)*unit(patch.swing)*.5f);
 if((step%32)==0) {if(patch.locked)seqrng=patch.seed;else patch.seed=random_word(seqrng)|1;}
 if((step%32)==0&&patch.kit.enabled&&patch.kit.generator==4)grids_generate(patch.kit,127,127,127,40,seqrng);   /* GRIDS re-rolls each pattern (locked seed: the same one) */
 static const int degree[7]={0,2,3,5,7,10,12};
 for(unsigned i=0;i<(patch.kit.enabled?4u:6u);i++) {
  const md_track &t=patch.track[i];unsigned voice_step=step-(journey_active?journey_origin[i]:0);
  if(voice_step%(unsigned)t.speed)continue;
  unsigned local=voice_step/t.speed;
  if(patch.kit.enabled&&i==3){
   unsigned at=local%patch.kit.length;
   sequence_visit(i,at);
   if(!t.mute)for(unsigned pad=0;pad<8;pad++)if((patch.kit.gates[pad]>>at)&1u){
    if(random_word(seqrng)%1000<(unsigned)creative_probability(i)&&!itinera_resting(3)){
     kit_fire(pad,at%4?.67f:.8f);stats.events[3]++;sequence_hit|=1u<<3;
    }
   }
   continue;
  }
  int hits=bound(t.hits+(drift_macro(0)-500)*t.length/1500,0,t.length);
  unsigned roll=random_word(seqrng);
  const md_sequence &sq=patch.sequence[i];
  if(sq.enabled){
   unsigned at=sequence_position(i,local);
   sequence_visit(i,at);
   const md_step &st=sq.steps[at];
   if(st.note&&st.velocity&&!t.mute&&step_rule_allows(i,sq,st,local)&&roll%100<md_step_chance(&st)&&(roll/100)%1000<(unsigned)creative_probability(i)&&!itinera_resting(i)){
    int note=st.note;if(t.model>=MD_FM){if(patch.chord_tracks&(1u<<i))note+=chord_offset();note=md_quantize(note+patch.transpose,patch.root,patch.scale);}
    unsigned human=patch.seed^((i+1)*0x27d4eb2du)^((local%sq.length+1)*0x85ebca6bu);
    float feel=1+((int)(random_word(human)%25)-12)*patch.motion.feel[i]*.0001f;
    sequence_hit|=1u<<i;
    fire(i,note,fminf(1.f,st.velocity*.01f*feel));
    // Swing alternates every tick. A slow track spans whole pairs, with only
    // one extra long/short tick for odd speeds; do not multiply the swing.
    const float span=MD_RATE*60.0f/(tempo_f()*4.0f)*(t.speed+(t.speed%2?(step%2?-1:1)*unit(patch.swing)*.5f:0));
    out_on(i,(float)note,fminf(1.f,st.velocity*.01f*feel),span*md_step_gate(&st)*.01f);
    if(md_has_envelope(t.model)){voices[i].midi_gate=1;seq_gate[i]=span*md_step_gate(&st)*.01f;}
   }continue;
  }
  const unsigned rhythmic_length=patch.ext[i].bass_mode==2&&t.length>16?16:t.length;
  sequence_visit(i,local%rhythmic_length);
  if((patch.ext[i].bass_mode==2||md_hit(local,hits,t.length,t.rotate))&&roll%1000<(unsigned)creative_probability(i)&&!t.mute&&!itinera_resting(i)) {
   int note=t.note;
   const md_extension &e=patch.ext[i];
   if(e.bass_mode){int off=e.notes[local%(e.bass_mode==2?(t.length>16?16:t.length):16)];if(off<0)continue;note+=off;
    // Marbles-inspired remembered variation: deterministic for each seed/position,
    // independent of audio noise and stable across repeated phrases when locked.
    unsigned mr=patch.seed^(i*0x9e3779b9u)^((local%16+1)*0x85ebca6bu);unsigned choice=random_word(mr);
    if((local%4)!=0&&choice%1000<(unsigned)e.marbles){static const int interval[]={0,3,5,7};note=t.note+interval[(choice>>12)&3];}
   }
   if(t.model>=MD_FM) {
    if(!e.bass_mode&&(roll>>10)%1000<(unsigned)patch.macro[1])note+=degree[(roll>>20)%7];
    if(patch.chord_tracks&(1u<<i))note+=chord_offset();
    note=md_quantize(note+patch.transpose,patch.root,patch.scale);
   }
   float velocity=.8f+unit(t.accent)*((local%t.length)==0?.2f:-.25f);
   sequence_hit|=1u<<i;
   fire(i,(float)note,velocity);
   out_on(i,(float)note,velocity,duration*t.speed*.5f/t.ratchet);
   repeats[i]={(duration/t.ratchet),duration/t.ratchet,(float)note,velocity,(unsigned)t.ratchet-1};
  }
 }
 if(itinera.active)itinera_heartbeat(duration);
 if(journey_active)journey_prepare();
 else if(soft_biome>=0&&(step&15u)==15u)soft_prepare();
 stats.step=step++;untilstep+=duration;
}
#if defined(MD_PORT_PERFORMANCE)
struct PerformanceFx { float room, echo, texture, dusk; };
static PerformanceFx performance_target, performance;

static float performance_slew(float value,float target,unsigned frames){
 const float next=value+(target-value)*(frames/(frames+(target>value?1400.f:4400.f)));
 return target==0.f&&next<.0001f?0.f:next;
}
#endif
// Keep the sample-by-sample tape, echo, room and output arithmetic intact.
// Only instruction placement changes on the FM-1: this hot path avoids the
// shared flash cache. No extra delay line, reduced rate or reserve is used.
#if defined(MD_DSP_IN_RAM)
#define MD_HOT_RAM __attribute__((section(".app_ram"),noinline,flatten))
#else
#define MD_HOT_RAM __attribute__((noinline,flatten))
#endif
static MD_HOT_RAM float render_effects(float *lr,unsigned n,const float *send,
 const float *delay_send,const float *room_send,const float *duck,
 float grainrate,float fb,float grain_size,float grain_pos,bool source_active,float echo_feedback,float room_decay,
 float effect_return,float echo_target,unsigned maxgrains,bool room_moving,
 const float *room_offset,bool level_on,float &level_gain,float level_step){
 float level_power=0.f;
 // Commands are applied between quanta, never inside this sample loop. Read
 // the shared noise target and latch FX activation once per tile; neither is
 // changed by tape/room processing. Keep all sample arithmetic and state
 // updates in their original order, including tails after the return is zero.
 const float noise_target=source_active?unit(patch.grain_noise):0.f;
 if(n){
#if defined(MD_PORT_PERFORMANCE)
  if(effect_return>0.f)fx_active=true;
#else
  if(patch.fx_level)fx_active=true;
#endif
 }
 const bool process_fx=fx_active;
 // These states have no observer inside the sample loop. Retain their exact
 // sample order in local scalars and commit at the tile boundary, avoiding
 // repeated shared-state stores through the output/send aliases.
 const bool tape_frozen=patch.freeze;
 float tile_peak=stats.peak;unsigned tape_writes=0;
 float tile_fx_smooth=fx_smooth;
 float tile_delay_smooth=delay_smooth;
 unsigned tile_echo_head=echo_head;
 float tile_echo_lp=echo_lp;
 float tile_fx_highpass_in=fx_highpass_in;
 float tile_fx_highpass_out=fx_highpass_out;
 float tile_feedback_lp=feedback_lp;
 float tile_grain_noise_smooth=grain_noise_smooth;
 float tile_grain_noise_lp=grain_noise_lp;

 for(unsigned k=0;k<n;k++) {
  float wet[2]={};
  if(!grainclock){
   unsigned active=0;int slot=-1;
   for(unsigned j=0;j<4;j++){if(grains[j].life&&grains[j].age<grains[j].life)active++;else if(slot<0)slot=(int)j;}
   grainclock=32; // Defer rather than truncate a grain after a size/quality change.
   if(slot>=0&&active<maxgrains){
    unsigned life=2205+(unsigned)(grain_size*11025);
    float travel=((tape_frozen?0.0f:1.0f)-grainrate)*life;
    float lo=260+fmaxf(0.0f,-travel),hi=(float)tape_valid-260-fmaxf(0.0f,travel);
    if(hi>lo){
     Grain &g=grains[slot];g.life=life;g.age=0;g.phase=0;g.inc=1.0f/life;
     float delay=lo+grain_pos*(hi-lo);
     delay=fminf(hi,fmaxf(lo,delay+noise()*250));
     g.pos=(float)writehead-delay;while(g.pos<0)g.pos+=GN;
     g.pan=.5f+noise()*.4f;g.rate=grainrate;g.gain=2.0f/maxgrains;
     grainclock=(life+maxgrains-1)/maxgrains;
    }
   }
  }
  --grainclock;
  // Finish existing FULL grains when entering ECO; never silence a live slot.
  for(unsigned j=0;j<(ABL(0)?0u:4u);j++){Grain &g=grains[j];if(!g.life||g.age>=g.life)continue;
   float distance=(float)writehead-g.pos;if(distance<0)distance+=GN;
   /* fminf/fmaxf are library calls on the device (three per grain per sample here); the values are finite and never -0, so the compares give the same numbers */
   const float room=(float)tape_valid-distance;
   float edge=distance<room?distance:room;edge=edge>0.0f?edge:0.0f;edge*=(1.0f/256);edge=edge<1.0f?edge:1.0f;
   float x=tape_read_ring(g.pos),w=plaits::Sine(g.phase*.5f);w=w*w*g.gain*edge;
   wet[0]+=x*w*(1-g.pan);wet[1]+=x*w*g.pan;
   g.pos+=g.rate;if(g.pos>=GN)g.pos-=GN;if(g.pos<0)g.pos+=GN;
   g.age++;g.phase=g.age*g.inc;
  }
  float w=(wet[0]+wet[1])*.5f;tile_feedback_lp+=(w-tile_feedback_lp)*.65f;
  // Full-rate tape preserves the dry send's bandwidth; 131072 PCM16 samples.
  // Same 256 KiB memory footprint, now 2.97 s at 44.1 kHz rather than 5.94 s.
  tile_grain_noise_smooth+=(noise_target-tile_grain_noise_smooth)*.0005f;
  float air=0;if(tile_grain_noise_smooth>.00001f){tile_grain_noise_lp+=(noise()-tile_grain_noise_lp)*.06f;air=tile_grain_noise_lp*tile_grain_noise_smooth*.08f;}
  if(!tape_frozen){float input=clip(send[k]+tile_feedback_lp*fb+air);
   tape[writehead]=int16_t(input*32767);writehead=(writehead+1)&GM;if(tape_valid<GN)tape_valid++;++tape_writes;
  }
  float effect[2]={};
  if(process_fx){
  // Full-rate independent echo, interpolated time movement, bounded feedback.
  float delayed=0.f;
  if(!ABL(1)){
  tile_delay_smooth+=(echo_target-tile_delay_smooth)*.00005f;
  float read=tile_echo_head+16384.0f-tile_delay_smooth;unsigned ri=(unsigned)read;float frac=read-ri;
  delayed=(echo[ri&16383]*(1-frac)+echo[(ri+1)&16383]*frac)/32768.0f;
  tile_echo_lp+=(delayed-tile_echo_lp)*.25f;
  echo[tile_echo_head]=int16_t(clip(delay_send[k]+tile_echo_lp*echo_feedback)*32767);tile_echo_head=(tile_echo_head+1)&16383;
  }
  float room_input=room_send[k];float hp=room_input-tile_fx_highpass_in+.985f*tile_fx_highpass_out;tile_fx_highpass_in=room_input;tile_fx_highpass_out=hp;
  // Schroeder diffusion before the existing four-line room. Unit-magnitude
  // allpasses distribute transients; tails and tape allocation stay unchanged.
  const unsigned *const lens=room_lens;float rv[4]={0.f,0.f,0.f,0.f},sum=0;
  if(!ABL(2)){
  hp=diffuse_room(hp);
  if(room_moving){
   // K-4: each line read 0..6 samples early, moving slowly and independently
   // (interpolated): the four fixed echoes no longer ring as one metallic
   // colour. Only in a dream; Biomes and live play keep the fixed room.
   for(unsigned j=0;j<4;j++){
    const float pos=room_head[j]+room_offset[j];const unsigned at=(unsigned)pos;const float f=pos-at;
    const unsigned i0=at>=lens[j]?at-lens[j]:at,i1=i0+1>=lens[j]?0:i0+1;
    rv[j]=(room[room_base[j]+i0]+(room[room_base[j]+i1]-room[room_base[j]+i0])*f)/32768.0f;room_lp[j]+=(rv[j]-room_lp[j])*.3f;sum+=room_lp[j];
   }
  }else for(unsigned j=0;j<4;j++){rv[j]=room[room_base[j]+room_head[j]]/32768.0f;room_lp[j]+=(rv[j]-room_lp[j])*.3f;sum+=room_lp[j];}
  for(unsigned j=0;j<4;j++)if(fabsf(room_lp[j])<1e-18f)room_lp[j]=0;
  for(unsigned j=0;j<4;j++){float value=hp*.3f+(sum*.5f-room_lp[j])*room_decay;room[room_base[j]+room_head[j]]=int16_t(clip(value)*32767);if(++room_head[j]>=lens[j])room_head[j]=0;}
  }
  tile_fx_smooth+=(effect_return-tile_fx_smooth)*.002f;
  effect[0]=tile_fx_smooth*(delayed*.5f+(rv[0]+rv[2])*.35f);effect[1]=tile_fx_smooth*(delayed*.5f+(rv[1]+rv[3])*.35f);
  }
  for(unsigned c=0;c<2;c++) {
   float x=effect[c]+lr[2*k+c]+wet[c]*smooth[3]*1.7f*duck[k];
   if(level_on){level_power+=x*x;x*=level_gain;}
   float y=x-dcx[c]+.9997f*dcy[c];dcx[c]=x;dcy[c]=y;
   if(fabsf(dcx[c])<1e-18f)dcx[c]=0;if(fabsf(dcy[c])<1e-18f)dcy[c]=0;
   x=clip(y*1.9f);lr[2*k+c]=x;
   if(fabsf(x)>tile_peak)tile_peak=fabsf(x);
  }
  if(level_on)level_gain+=level_step;
 }
 fx_smooth=tile_fx_smooth;
 delay_smooth=tile_delay_smooth;
 echo_head=tile_echo_head;
 echo_lp=tile_echo_lp;
 fx_highpass_in=tile_fx_highpass_in;
 fx_highpass_out=tile_fx_highpass_out;
 feedback_lp=tile_feedback_lp;
 grain_noise_smooth=tile_grain_noise_smooth;
 grain_noise_lp=tile_grain_noise_lp;
 // A-07: flush denormals (-180 dB, inaudible) so the recursive tails never park in slow denormal land
 if(fabsf(echo_lp)<1e-18f)echo_lp=0;
 if(fabsf(feedback_lp)<1e-18f)feedback_lp=0;
 if(fabsf(grain_noise_lp)<1e-18f)grain_noise_lp=0;
 if(fabsf(grain_noise_smooth)<1e-18f)grain_noise_smooth=0;
 if(fabsf(fx_highpass_in)<1e-18f)fx_highpass_in=0;
 if(fabsf(fx_highpass_out)<1e-18f)fx_highpass_out=0;
 stats.peak=tile_peak;stats.grain_writes+=tape_writes;
 return level_power;
}
// Identical envelope, filter, dry-stem and send arithmetic for one voice.
// Keep it in the same guarded RAM code window as the shared effects.
static MD_HOT_RAM void mix_voice(Voice &v,const md_track &t,unsigned i,unsigned n,
 float *out,const float *aux,float *lr,float *send,float *delay_send,
 float *room_send,const float *duck,float f,float target,float pan,float dec,
 float attackstep,float sendlevel,float warp_target,float ds,float rs,float expr,
 bool melodic,unsigned release_at,float scene_gain,float scene_step
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
 ,const md_usb_stems &stems,unsigned offset
#endif
 ,bool duck_identity=false
){
  float track_peak=stats.track_peak[i];
  // Keep the exact envelope -> filter -> gain order, but let the SVF retain
  // coefficients and state in registers across a tile instead of the mixer.
  const uintptr_t out_address=reinterpret_cast<uintptr_t>(out),aux_address=reinterpret_cast<uintptr_t>(aux);
  const size_t body_bytes=size_t(n)*sizeof(float);
  const bool body_separate=out_address<aux_address?aux_address-out_address>=body_bytes:out_address-aux_address>=body_bytes;
  const bool body_filter=!melodic&&t.filter&&body_separate&&
      (t.model==MD_ELEMENTS||t.model==MD_RINGS);
  if(!body_filter){   /* 2.4: the envelope state and the model in locals: the float stores to out[] could alias the Voice's floats, so every sample reloaded them. Same arithmetic, same order. */
   const int model=t.model;const float velocity=v.velocity;
   float env=v.env,gate=v.midi_gate;int attacking=v.attacking;
   for(unsigned k=0;k<n;k++){
    float x=model==MD_ELEMENTS?(out[k]+aux[k]*.3f)*3.0f*velocity:model==MD_RINGS?(out[k]+aux[k])*12.0f:out[k];
    if(melodic){if(k==release_at)gate=-1;if(gate<0){env*=.9975f;attacking=0;}else if(attacking){env+=attackstep;if(env>=1){env=1;attacking=0;}}else if(gate==0)env*=dec;x*=env;}
    out[k]=x;
   }
   v.env=env;v.midi_gate=gate;v.attacking=attacking;
  }
  // Envelope commits above determine whether quiet tracking is applicable.
  // Keep the body/filter fusion, but carry no peak state through the filters.
  const bool measure_quiet=!melodic||(!v.attacking&&v.midi_gate<=0);
  if(body_filter){
   if(t.model==MD_ELEMENTS)v.filter1.ProcessLowPassBody<true,false>(out,aux,out,n,v.velocity);
   else v.filter1.ProcessLowPassBody<false,false>(out,aux,out,n,v.velocity);
   if(t.filter==2)v.filter2.Process<stmlib::FILTER_MODE_LOW_PASS>(out,out,n);
  }else if(t.filter){
   v.filter1.Process<stmlib::FILTER_MODE_LOW_PASS>(out,out,n);
   if(t.filter==2)v.filter2.Process<stmlib::FILTER_MODE_LOW_PASS>(out,out,n);
  }
  // For positive finite gain, rounded nonnegative multiplication is monotone:
  // max(abs(out))*gain reaches the threshold iff any non-NaN sample does.
  // Once one does, later samples cannot make the voice quiet again this tile.
  // Invalid, zero or negative gain retains the original complete peak scan.
  if(measure_quiet){
   const float quiet_gain=fmaxf(unit(t.level)*.48f,v.amp);
   bool is_quiet;
   if(quiet_gain>0.f&&stmlib::IsFiniteFloat32(quiet_gain)){
    is_quiet=true;
    for(unsigned k=0;k<n;k++)if(fabsf(out[k])*quiet_gain>=7.6e-6f){is_quiet=false;break;}
   }else{
    float quiet_peak=0.f;
    for(unsigned k=0;k<n;k++){const float a=fabsf(out[k]);quiet_peak=a>quiet_peak?a:quiet_peak;}
    is_quiet=quiet_peak*quiet_gain<7.6e-6f;
   }
   voice_quiet[i]=is_quiet?voice_quiet[i]+n:0;
  }else voice_quiet[i]=0;
  // Local scalars prevent output-buffer aliasing from forcing repeated Voice
  // loads/stores. Same arithmetic and order; commit once after the tile.
  float mix_amp=v.amp;float mix_fade=v.fade;float mix_lpg=v.lpg;float mix_warp_smooth=v.warp_smooth;float mix_warp_phase=v.warp_phase;float mix_expression_smooth=v.expression_smooth;float mix_pan_smooth=v.pan_smooth;float mix_delay_send_smooth=v.delay_send_smooth;float mix_reverb_send_smooth=v.reverb_send_smooth;float mix_grain_send_smooth=v.grain_send_smooth; // O-2
  const bool duck_this_voice=(int)i!=patch.duck_source;
  const bool journey_fading=journey_prepared&&(journey_fade_mask&(1u<<i));const bool lpg_filter=t.filter==3;
  // A rounded fixed point is an exact invariant, not a tolerance. These
  // six independent slews have constant targets for this tile. Keep the
  // original loop whenever even one would still change (including NaNs).
  const float fade_next=mix_fade+(1-mix_fade)*.004f;
  const float warp_next=mix_warp_smooth+(warp_target-mix_warp_smooth)*.002f;
  const float expression_next=mix_expression_smooth+(expr-mix_expression_smooth)*.003f;
  const float grain_next=mix_grain_send_smooth+(sendlevel-mix_grain_send_smooth)*.002f;
  const float delay_next=mix_delay_send_smooth+(ds-mix_delay_send_smooth)*.002f;
  const float room_next=mix_reverb_send_smooth+(rs-mix_reverb_send_smooth)*.002f;
  const bool settled=fade_next==mix_fade&&warp_next==mix_warp_smooth&&
      expression_next==mix_expression_smooth&&grain_next==mix_grain_send_smooth&&
      delay_next==mix_delay_send_smooth&&room_next==mix_reverb_send_smooth;
  if(n&&settled){
   // Commit the first rounded result too: equality includes -0 == +0.
   mix_fade=fade_next;mix_warp_smooth=warp_next;mix_expression_smooth=expression_next;
   mix_grain_send_smooth=grain_next;mix_delay_send_smooth=delay_next;mix_reverb_send_smooth=room_next;
   // Specialize only exact neutral factors and tile-invariant disabled paths.
   // Every original state recurrence, sample order and RNG draw is retained.
   // Bounded finite gains also prove that removing the two *1 identities
   // cannot expose a NaN payload/canonicalization difference after the input guard.
   if(duck_identity&&mix_expression_smooth==1.f&&fabsf(mix_amp)<=1.f&&
      fabsf(target)<=1.f&&fabsf(mix_fade)<=1.f&&!lpg_filter&&!journey_fading&&
      mix_warp_smooth<=.000001f&&scene_step==0.f&&scene_gain==1.f){
    // Specialize a contiguous output span once. A straddling tile and no
    // stream retain the original row selection. Do not keep a tile-local RNG.
    const auto sample_neutral=[&](unsigned k,int16_t *stem_row,bool direct_stem){
     mix_amp+=(target-mix_amp)*.004f;
     float x=out[k];mix_lpg*=dec;
     uint32_t x_bits;__builtin_memcpy(&x_bits,&x,sizeof x_bits);
   if((x_bits&0x7f800000u)==0x7f800000u){stats.nonfinite++;x=0;}
     mix_pan_smooth+=(pan-mix_pan_smooth)*.003f;
     x=x*mix_amp*mix_fade;
     const float sent=x;
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
     if(direct_stem)stem_row[i]=stem_quantize(x,stem_rng);
     else write_stem(stems,offset+k,i,x);
#endif
     {const float ax=fabsf(x);track_peak=ax>track_peak?ax:track_peak;}
     lr[2*k]+=x*(1-mix_pan_smooth);lr[2*k+1]+=x*mix_pan_smooth;
     send[k]+=sent*mix_grain_send_smooth;delay_send[k]+=sent*mix_delay_send_smooth;room_send[k]+=sent*mix_reverb_send_smooth;
    };
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
    int16_t *stem_row=nullptr;
    if(stems.first&&n&&n<=64&&offset<=128){
     if(offset<stems.first_frames){
      if(n<=stems.first_frames-offset)stem_row=stems.first+offset*8;
     }else if(stems.wrapped)stem_row=stems.wrapped+(offset-stems.first_frames)*8;
    }
    if(stem_row){
     // Row base, not track pointer: the final increment is at most one-past.
     for(unsigned k=0;k<n;k++,stem_row+=8)sample_neutral(k,stem_row,true);
    }else
#endif
    {
     for(unsigned k=0;k<n;k++)sample_neutral(k,nullptr,false);
    }
   }else{
  for(unsigned k=0;k<n;k++) {
   mix_amp+=(target-mix_amp)*.004f;
   float x=out[k];if(lpg_filter)x*=mix_lpg;
   mix_lpg*=dec;
   uint32_t x_bits;__builtin_memcpy(&x_bits,&x,sizeof x_bits);
   if((x_bits&0x7f800000u)==0x7f800000u) {stats.nonfinite++;x=0;}
   // Individual oscillator outputs remain linear; tape and final mix retain peak protection.
   // Warps-inspired ring blending, not a claim to port the complete module.
   if(mix_warp_smooth>.000001f){mix_warp_phase+=f;if(mix_warp_phase>=1)mix_warp_phase-=1;x*=1-mix_warp_smooth*.5f+mix_warp_smooth*.5f*plaits::Sine(mix_warp_phase);}
   mix_pan_smooth+=(pan-mix_pan_smooth)*.003f;
   x=x*mix_amp*mix_fade*mix_expression_smooth;if(journey_fading)x*=journey_gain(i,k,n);if(duck_this_voice)x*=duck[k];
   // K-1: an Itinera scene's dry voice leaves; its sends stay and carry the tail.
   const float sent=x;
   if(scene_step!=0.f||scene_gain!=1.f){scene_gain+=scene_step;x*=scene_gain;}
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
   write_stem(stems,offset+k,i,x);
#endif
   {const float ax=fabsf(x);track_peak=ax>track_peak?ax:track_peak;}   /* = fmaxf (a NaN keeps the peak) */
   lr[2*k]+=x*(1-mix_pan_smooth);lr[2*k+1]+=x*mix_pan_smooth;send[k]+=sent*mix_grain_send_smooth;
   delay_send[k]+=sent*mix_delay_send_smooth;room_send[k]+=sent*mix_reverb_send_smooth;
  }
   }
  }else{
  for(unsigned k=0;k<n;k++) {
   mix_amp+=(target-mix_amp)*.004f;mix_fade+=(1-mix_fade)*.004f;
   float x=out[k];if(lpg_filter)x*=mix_lpg;
   mix_lpg*=dec;
   uint32_t x_bits;__builtin_memcpy(&x_bits,&x,sizeof x_bits);
   if((x_bits&0x7f800000u)==0x7f800000u) {stats.nonfinite++;x=0;}
   // Individual oscillator outputs remain linear; tape and final mix retain peak protection.
   // Warps-inspired ring blending, not a claim to port the complete module.
   mix_warp_smooth+=(warp_target-mix_warp_smooth)*.002f;
   if(mix_warp_smooth>.000001f){mix_warp_phase+=f;if(mix_warp_phase>=1)mix_warp_phase-=1;x*=1-mix_warp_smooth*.5f+mix_warp_smooth*.5f*plaits::Sine(mix_warp_phase);}
   mix_expression_smooth+=(expr-mix_expression_smooth)*.003f;mix_pan_smooth+=(pan-mix_pan_smooth)*.003f;
   x=x*mix_amp*mix_fade*mix_expression_smooth;if(journey_fading)x*=journey_gain(i,k,n);if(duck_this_voice)x*=duck[k];
   // K-1: an Itinera scene's dry voice leaves; its sends stay and carry the tail.
   const float sent=x;
   if(scene_step!=0.f||scene_gain!=1.f){scene_gain+=scene_step;x*=scene_gain;}
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
   write_stem(stems,offset+k,i,x);
#endif
   {const float ax=fabsf(x);track_peak=ax>track_peak?ax:track_peak;}   /* = fmaxf (a NaN keeps the peak) */
   lr[2*k]+=x*(1-mix_pan_smooth);lr[2*k+1]+=x*mix_pan_smooth;mix_grain_send_smooth+=(sendlevel-mix_grain_send_smooth)*.002f;send[k]+=sent*mix_grain_send_smooth;
   mix_delay_send_smooth+=(ds-mix_delay_send_smooth)*.002f;mix_reverb_send_smooth+=(rs-mix_reverb_send_smooth)*.002f;
   delay_send[k]+=sent*mix_delay_send_smooth;room_send[k]+=sent*mix_reverb_send_smooth;
  }
  }
  v.amp=mix_amp;v.fade=mix_fade;v.lpg=mix_lpg;v.warp_smooth=mix_warp_smooth;v.warp_phase=mix_warp_phase;v.expression_smooth=mix_expression_smooth;v.pan_smooth=mix_pan_smooth;v.delay_send_smooth=mix_delay_send_smooth;v.reverb_send_smooth=mix_reverb_send_smooth;v.grain_send_smooth=mix_grain_send_smooth;
  stats.track_peak[i]=track_peak;
  if(melodic&&!v.attacking&&v.env<.000001f)v.active=0;
  else if(voice_quiet[i]>=MD_RATE/2){v.active=0;voice_quiet[i]=0;}
}
#ifdef MD_MIX_PIPELINE
// One outstanding job, owned by the caller until join. Copy the dynamic track:
// creative_track's loop-local value must not be borrowed by an asynchronous job.
#if defined(MD_SOURCE_READY_HOST_TEST) && !defined(PLAT_HOST)
#error "Readiness override is only for host parity tests"
#endif
#ifdef MD_SOURCE_MIX_PIPELINE
// A hint chooses source placement only; it never substitutes for mix_job_join.
static inline bool mix_source_worker_ready(){
#if defined(PLAT_HOST) && defined(MD_SOURCE_READY_HOST_TEST)
 return true; // Deliberately stale-positive hint; still must join before reuse.
#elif defined(MD_MIX_DEVICE)
 return md_mix_worker_ready_device()!=0;
#else
 return false; // Conservative native backend; no cross-thread polling contract.
#endif
}
// Opt-in experiment: CPU0 resolves controls in their original order. The job
// owns one slot until join; only these RNG-free sources can enter this path.
struct WavetableSource {
 unsigned kind,band,shape; // 0 none; 1 SATIN; 2 SPECTRAL. Indices select immutable tables.
 float f2,morph,fade;
 bool blend_low;
};
static_assert(sizeof(WavetableSource)==28,"source snapshot has no PCM storage");
static struct {uint32_t accepted,completed,serial;} source_mix_counts; // CPU0 only
static MD_HOT_RAM void render_wavetable_tile(Voice &,float,float,float,
 const float *,const float *,const float *,const float *,bool,float,float *,unsigned);
#endif
struct MixJob {
 Voice *voice;md_track track;unsigned index,n;
 float *out;const float *aux;float *lr,*send,*delay_send,*room_send;const float *duck;
 float f,target,pan,dec,attackstep,sendlevel,warp_target,ds,rs,expr;
 bool melodic;unsigned release_at;float scene_gain,scene_step;
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
 md_usb_stems stems;unsigned offset;
#endif
 bool duck_identity;
#ifdef MD_SOURCE_MIX_PIPELINE
 WavetableSource source; // immutable from publication through join/confirmed hold
#endif
#ifdef MD_MIX_DEVICE
 uint32_t check;
#endif
};
#ifdef MD_MIX_DEVICE
static uint32_t mix_word(uint32_t h,uint32_t x){return (h^x)*16777619u;}
static uint32_t mix_check(const MixJob &j){
 uint32_t h=2166136261u;
 const md_track &t=j.track;const int32_t fields[]={t.model,t.tone,t.harm,t.decay,t.level,t.send,t.hits,t.length,t.rotate,t.prob,t.note,t.pan,t.mute,t.speed,t.attack,t.ratchet,t.accent,t.shape,t.cutoff,t.resonance,t.filter,t.breathe};
 static_assert(sizeof(md_track)==sizeof fields,"track checksum fields");for(auto x:fields)h=mix_word(h,(uint32_t)x);
 const uintptr_t p[]={(uintptr_t)j.voice,(uintptr_t)j.out,(uintptr_t)j.aux,(uintptr_t)j.lr,(uintptr_t)j.send,(uintptr_t)j.delay_send,(uintptr_t)j.room_send,(uintptr_t)j.duck};
 for(auto x:p){h=mix_word(h,(uint32_t)x);
#if UINTPTR_MAX > UINT32_MAX
 h=mix_word(h,(uint32_t)(x>>32));
#endif
 }
 const float f[]={j.f,j.target,j.pan,j.dec,j.attackstep,j.sendlevel,j.warp_target,j.ds,j.rs,j.expr,j.scene_gain,j.scene_step};
 for(auto x:f){uint32_t b;memcpy(&b,&x,4);h=mix_word(h,b);}
 h=mix_word(h,j.index);h=mix_word(h,j.n);h=mix_word(h,j.melodic);h=mix_word(h,j.release_at);h=mix_word(h,j.duck_identity);
#ifdef MD_SOURCE_MIX_PIPELINE
 h=mix_word(h,j.source.kind);h=mix_word(h,j.source.band);h=mix_word(h,j.source.shape);h=mix_word(h,j.source.blend_low);
 const float sf[]={j.source.f2,j.source.morph,j.source.fade};for(auto x:sf){uint32_t b;memcpy(&b,&x,4);h=mix_word(h,b);}
#endif
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
 h=mix_word(h,(uint32_t)(uintptr_t)j.stems.first);h=mix_word(h,(uint32_t)(uintptr_t)j.stems.wrapped);h=mix_word(h,j.stems.first_frames);h=mix_word(h,j.offset);
#endif
 return h;
}
static bool mix_span(const void *p,unsigned bytes,unsigned align){
 const uintptr_t a=(uintptr_t)p;
#ifdef MD_MIX_DEVICE_TEST
 return p&&!(a&(align-1u));
#else
 return a>=0x01c00000u&&a<=0x01c7c000u&&bytes<=0x01c7c000u-a&&!(a&(align-1u));
#endif
}
static bool mix_valid(const MixJob &j){
 if(j.index>=6||j.n<1||j.n>64||j.voice!=voices+j.index||j.release_at>j.n||j.check!=mix_check(j))return false;
#ifdef MD_SOURCE_MIX_PIPELINE
 const auto &s=j.source;
 if(s.kind){
  if(s.kind>2||j.track.model!=(s.kind==1?MD_SATIN:MD_SPECTRAL)||s.band>3||s.shape>2)return false;
  if(!stmlib::IsFiniteFloat32(j.f)||j.f<0.f||!stmlib::IsFiniteFloat32(s.f2)||s.f2<0.f||!stmlib::IsFiniteFloat32(s.morph)||!stmlib::IsFiniteFloat32(s.fade)||s.morph<0.f||s.morph>1.f||s.fade<0.f||s.fade>1.f)return false;
  if(s.blend_low!=(s.band&&s.fade<1.f))return false;
 }
#endif
 const void *p[]={j.out,j.aux,j.send,j.delay_send,j.room_send,j.duck};for(auto x:p)if(!mix_span(x,j.n*4,4))return false;
 if(!mix_span(j.lr,j.n*8,4))return false;
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
 if(j.offset>4096u-j.n)return false;
 if(j.stems.first){
  if(j.stems.first_frames>4096)return false;
  const unsigned end=j.offset+j.n;
  if(j.offset<j.stems.first_frames){const unsigned stop=end<j.stems.first_frames?end:j.stems.first_frames;if(!mix_span((const void*)((uintptr_t)j.stems.first+j.offset*16),(stop-j.offset)*16-4,2))return false;}
  if(end>j.stems.first_frames){const unsigned from=j.offset>j.stems.first_frames?j.offset-j.stems.first_frames:0,stop=end-j.stems.first_frames;if(!j.stems.wrapped||!mix_span((const void*)((uintptr_t)j.stems.wrapped+from*16),(stop-from)*16-4,2))return false;}
 }
#endif
 return true;
}
#endif
static void mix_job_run(void *opaque){
 MixJob &j=*static_cast<MixJob*>(opaque);
#ifdef MD_SOURCE_MIX_PIPELINE
 if(j.source.kind){
  const auto &s=j.source;
  const float (*table)[4][257]=s.kind==2?spectral_table:satin_table;
  const float *ha=table[s.band][s.shape],*hb=table[s.band][s.shape+1];
  const float *la=s.blend_low?table[s.band-1][s.shape]:ha,*lb=s.blend_low?table[s.band-1][s.shape+1]:hb;
  md_far(&render_wavetable_tile)(*j.voice,j.f,s.f2,s.morph,ha,hb,la,lb,s.blend_low,s.fade,j.out,j.n);
 }
#endif
 md_far(&mix_voice)(*j.voice,j.track,j.index,j.n,j.out,j.aux,j.lr,j.send,
  j.delay_send,j.room_send,j.duck,j.f,j.target,j.pan,j.dec,j.attackstep,
  j.sendlevel,j.warp_target,j.ds,j.rs,j.expr,j.melodic,j.release_at,
  j.scene_gain,j.scene_step
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
  ,j.stems,j.offset
#endif
  ,j.duck_identity);
}
#ifdef MD_SOURCE_MIX_PIPELINE
static bool mix_job_join(const MixJob &j){
 if(!md_mix_join())return false;
 if(j.source.kind)source_mix_counts.completed++;
 return true;
}
static void mix_job_accepted(const MixJob &j){if(j.source.kind)source_mix_counts.accepted++;}
static void mix_job_serial(MixJob &j){if(j.source.kind)source_mix_counts.serial++;mix_job_run(&j);}
#else
#define mix_job_join(j) md_mix_join()
#define mix_job_accepted(j) ((void)0)
#define mix_job_serial(j) mix_job_run(&(j))
#endif
#endif
// Same two-oscillator wavetable sample loop; only instruction placement changes.
// Its tables remain in flash and no additional synthesis buffer is allocated.
static MD_HOT_RAM void render_wavetable_tile(Voice &v,float f,float f2,float morph,
 const float *ha,const float *hb,const float *la,const float *lb,
 bool blend_low,float fade,float *out,unsigned n){
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=f;if(v.sub_phase>=1)v.sub_phase-=1;
     v.mellow_phase+=f2;if(v.mellow_phase>=1)v.mellow_phase-=1;
     float sum=0;float phases[2]={v.sub_phase,v.mellow_phase};
     for(int osc=0;osc<2;osc++){
      float index=phases[osc]*256;int at=(int)index;float frac=index-at;
      float x=ha[at]+(ha[at+1]-ha[at])*frac,y=hb[at]+(hb[at+1]-hb[at])*frac;
      float wave=x+(y-x)*morph;
      if(blend_low){x=la[at]+(la[at+1]-la[at])*frac;y=lb[at]+(lb[at+1]-lb[at])*frac;float low=x+(y-x)*morph;wave=low+(wave-low)*fade;}
      sum+=wave;
     }
     float fundamental=plaits::Sine(v.sub_phase);
     out[k]=.72f*fundamental+(sum*.46f-.72f*fundamental)*v.satin_tone;
    }
}

/* 2.4: the finish stages (WIDTH, CHORUS, AIR, the loudness trim) as one function that runs from RAM like render_effects and mix_voice:
 * they touch every sample of every block and ran from flash inside quantum(). trim_target_gain() stays a normal call. */
static MD_HOT_RAM void finish_stages(float *lr,unsigned n,float trim_target){
 // 2.4: WIDTH widens the sides (L-R) and leaves the middle (L+R) alone, so the mono sum is unchanged; eased between levels.
 {
  static const float side[4]={1.f,1.5f,2.f,3.f};
  const float target=side[finish_width];
  if(!ABL(4)&&(width_now!=1.f||target!=1.f)){
   float next=width_now+(target-width_now)*(n/(.25f*MD_RATE));
   if(fabsf(target-next)<1e-3f)next=target;
   const float step=(next-width_now)/n;float w=width_now;
   for(unsigned k=0;k<n;k++){
    w+=step;
    const float m=(lr[2*k]+lr[2*k+1])*.5f,sd=(lr[2*k]-lr[2*k+1])*.5f*w;
    lr[2*k]=m+sd;lr[2*k+1]=m-sd;
   }
   width_now=next;
  }
 }
 // 2.4: CHORUS. Everything above about 250 Hz goes through two slow, slightly different modulated delays (about 13 ms +- 3 ms, 0.35 and
 // 0.47 Hz), one per side, and a part of it is added back: the sound gets wider and softer, the bass is not touched.
 {
  const float target=finish_chorus?1.f:0.f;
  if(!ABL(5)&&(chorus_amount!=0.f||target!=0.f)){
   float next=chorus_amount+(target-chorus_amount)*(n/(.25f*MD_RATE));
   if(fabsf(target-next)<1e-3f)next=target;
   const float step=(next-chorus_amount)/n;float a=chorus_amount;
   chorus_phase_l+=n*(.35f/MD_RATE);chorus_phase_l-=(int)chorus_phase_l;
   chorus_phase_r+=n*(.47f/MD_RATE);chorus_phase_r-=(int)chorus_phase_r;
   const float end_l=573.f+132.f*parabola_sine(chorus_phase_l),end_r=573.f+132.f*parabola_sine(chorus_phase_r+.25f);
   const float dl=(end_l-chorus_delay_l)/n,dr=(end_r-chorus_delay_r)/n;float xl=chorus_delay_l,xr=chorus_delay_r;
   for(unsigned k=0;k<n;k++){
    a+=step;xl+=dl;xr+=dr;
    const float d[2]={xl,xr};
    for(unsigned c=0;c<2;c++){
     const float in=lr[2*k+c];
     chorus_lp[c]+=(in-chorus_lp[c])*.035f;if(fabsf(chorus_lp[c])<1e-9f)chorus_lp[c]=0.f;
     const float hp=in-chorus_lp[c];
     // Preserve the float interpolation; clamp inline with fminf/fmaxf's NaN result.
     const float written=hp*16384.f;
     chorus_buf[c][chorus_pos]=(int16_t)(written>32767.f?32767.f:(written>=-32768.f?written:-32768.f));
     const float at=(float)chorus_pos-d[c];
     // at is bounded by the 1024-sample ring and the 441..705-sample delay.
     int i0=(int)at;if(at<(float)i0)--i0;
     const int i1=i0+1;const float frac=at-(float)i0;
     const float w0=chorus_buf[c][i0&1023],w1=chorus_buf[c][i1&1023];
     lr[2*k+c]=in*(1.f-.1f*a)+(w0+(w1-w0)*frac)*(1.f/16384.f)*.5f*a;
    }
    chorus_pos=(chorus_pos+1)&1023;
   }
   chorus_amount=next;chorus_delay_l=end_l;chorus_delay_r=end_r;
   chorus_clear=next==0.f;
  }
 }
 // 2.4: AIR adds a quiet breath above 7 kHz that most Biomes do not have (263 of 279 have nothing there): noise, high-passed,
 // that follows the level of the sound, so it comes and goes with the music and is silent in silence.
 {
  static const float amount[4]={0.f,.06f,.14f,.28f};
  const float target=amount[finish_air];
  if(!ABL(6)&&(air_amount!=0.f||target!=0.f)){
   float next=air_amount+(target-air_amount)*(n/(.25f*MD_RATE));
   if(fabsf(target-next)<1e-4f)next=target;
   const float step=(next-air_amount)/n;float a=air_amount;
   for(unsigned k=0;k<n;k++){
    a+=step;
    const float level=fabsf(lr[2*k])+fabsf(lr[2*k+1]);
    air_env+=(level*.5f-air_env)*.0015f;if(air_env<1e-9f)air_env=0.f;
    for(unsigned c=0;c<2;c++){
     air_seed^=air_seed<<13;air_seed^=air_seed>>17;air_seed^=air_seed<<5;
     const float white=(float)(int32_t)air_seed*(1.f/2147483648.f);
     air_lp1[c]+=(white-air_lp1[c])*.631f;           // one pole at about 7 kHz
     air_lp2[c]+=(air_lp1[c]-air_lp2[c])*.631f;      // two poles
     lr[2*k+c]+=(white-air_lp1[c]-air_lp2[c]*.5f)*air_env*a;
    }
   }
   air_amount=next;
   air_clear=next==0.f;
  }
 }
 // 2.4: the Biome's loudness trim, eased over about a quarter of a second. A raised signal is kept inside full scale.
 {
  const float target=trim_target;
  if(trim_gain<0)trim_gain=target;
  if(!ABL(7)&&(trim_gain!=1.f||target!=1.f)){
   float next=trim_gain+(target-trim_gain)*(n/(.25f*MD_RATE));
   if(fabsf(target-next)<1e-4f)next=target;
   const float step=(next-trim_gain)/n;float g=trim_gain;
   if(step==0.f&&g<=1.f){
    if(g!=1.f)for(unsigned k=0;k<2*n;k++)lr[k]*=g;
   }else if(step==0.f){
    /* a steady raise: one multiply and two compares per sample (the library min/max calls cost far more on the device) */
    for(unsigned k=0;k<2*n;k++){float l=lr[k]*g;l=l>1.f?1.f:(l>=-1.f?l:-1.f);lr[k]=l;}
   }else{
    for(unsigned k=0;k<n;k++){
     g+=step;float l=lr[2*k]*g,r=lr[2*k+1]*g;
     if(g>1.f){l=fminf(1.f,fmaxf(-1.f,l));r=fminf(1.f,fmaxf(-1.f,r));}
     lr[2*k]=l;lr[2*k+1]=r;
    }
   }
   trim_gain=next;
  }
 }
}
// The app calls this only when its boot chime is absent. Finite audio, a
// bounded master and sleep gain are the same valid-input contract as app_render.
// Integer PCM cannot retain the sign of a removed +0 chime. Multiplication by
// sleep gain 1 is exact; a rounded gain fixed point remains identical each frame.
extern "C" MD_HOT_RAM float md_audio_output24(int32_t *pcm,const float *audio,
 unsigned n,unsigned master,float gain,float sleep_gain){
 if(!n)return gain;
 const float target=(float)master/4096.0f;
 const float next=gain+(target-gain)*.004f;
 if(next==gain){
  gain=next; // also preserves the first update from -0 to +0
  if(sleep_gain==1.f){
#pragma clang loop unroll_count(4)
  for(unsigned k=0;k<n;k++){pcm[2*k]=(int32_t)(audio[2*k]*gain*8380000.f);pcm[2*k+1]=(int32_t)(audio[2*k+1]*gain*8380000.f);}}
  else{
#pragma clang loop unroll_count(4)
  for(unsigned k=0;k<n;k++){pcm[2*k]=(int32_t)(audio[2*k]*gain*sleep_gain*8380000.f);pcm[2*k+1]=(int32_t)(audio[2*k+1]*gain*sleep_gain*8380000.f);}}
 }else{
  if(sleep_gain==1.f){
#pragma clang loop unroll_count(4)
  for(unsigned k=0;k<n;k++){gain+=(target-gain)*.004f;pcm[2*k]=(int32_t)(audio[2*k]*gain*8380000.f);pcm[2*k+1]=(int32_t)(audio[2*k+1]*gain*8380000.f);}}
  else{
#pragma clang loop unroll_count(4)
  for(unsigned k=0;k<n;k++){gain+=(target-gain)*.004f;pcm[2*k]=(int32_t)(audio[2*k]*gain*sleep_gain*8380000.f);pcm[2*k+1]=(int32_t)(audio[2*k+1]*gain*sleep_gain*8380000.f);}}
 }
 return gain;
}
#undef MD_HOT_RAM
#ifdef MD_PROFILE
static void prof_quanta_inc(unsigned n){prof.quanta++;prof.frames+=n;}
static void prof_active(unsigned i){prof.active_mask|=1u<<i;}
static void prof_end(void){
 const uint32_t cb0=prof.cb0;
 const uint32_t now=plat_ticks24();prof.reads++;
 if(!prof.overhead){uint32_t best=1000;for(int k=0;k<32;k++){const uint32_t a=plat_ticks24(),b=plat_ticks24();if(b-a<best)best=b-a;}prof.overhead=best?best:1;}
 ProfRec r;memset(&r,0,sizeof r);
 r.total=now-cb0;r.tick=prof.acc_tick;r.pre=prof.acc_pre;r.vox=prof.acc_vox;r.misc=prof.acc_misc;r.fx=prof.acc_fx;r.fin=prof.acc_fin;
 r.ctl=r.total>prof.acc_q?r.total-prof.acc_q:0;   /* everything outside quantum(): commands, MIDI, tick(), the loop */
 for(unsigned i=0;i<6;i++)r.voice[i]=prof.acc_voice[i];
 r.c_scene=(prof.scene&0xffffu)|(unsigned)quality_on()<<16|(unsigned)((finish_chorus?1u:0u)|(finish_air?2u:0u)|(finish_vintage?4u:0u))<<17|(unsigned)itinera.active<<20|(unsigned)journey_prepared<<21|(unsigned)(journey_fade_mask!=0)<<22|(unsigned)runtime_eco<<23;
 r.c_misc=(prof.frames&0x1ffu)|(prof.active_mask&63u)<<9|(prof.quanta&0xffu)<<15;   /* frames of the whole app_render (256 on the FM-1): 9 bits */
 for(unsigned i=0;i<6;i++)r.models|=(uint32_t)(patch.track[i].model&31u)<<(5*i);
 r.c_step=step;r.c_part=itinera.part&0xffffu;r.c_tempo=(uint32_t)(tempo_f()*10.f);r.reads=prof.reads;
 prof.blocks++;if(r.total>125366u)prof.over90++;
 {uint64_t *q=prof.sum;q[0]+=r.total;q[1]+=r.ctl;q[2]+=r.pre;q[3]+=r.vox;q[4]+=r.misc;q[5]+=r.fx;q[6]+=r.fin;q[7]+=r.tick;for(unsigned i=0;i<6;i++)prof.vsum[i]+=r.voice[i];}   /* 90 % of 5804 us, in ticks */
 int at=-1;
 for(unsigned k=0;k<prof.top_used;k++)if((prof.top[k].c_scene&0xffffu)==(r.c_scene&0xffffu)){at=(int)k;break;}   /* one entry a scene */
 if(at>=0){if(r.total>prof.top[at].total)prof.top[at]=r;else at=-2;}
 else if(prof.top_used<4){at=(int)prof.top_used++;prof.top[at]=r;}
 else{unsigned lo=0;for(unsigned k=1;k<4;k++)if(prof.top[k].total<prof.top[lo].total)lo=k;if(r.total>prof.top[lo].total){at=(int)lo;prof.top[lo]=r;}else at=-2;}
 if(at>=0)for(int a=at;a>0&&prof.top[a].total>prof.top[a-1].total;a--){const ProfRec t=prof.top[a];prof.top[a]=prof.top[a-1];prof.top[a-1]=t;}
 prof.acc_q=prof.acc_tick=prof.acc_pre=prof.acc_vox=prof.acc_misc=prof.acc_fx=prof.acc_fin=prof.reads=prof.quanta=prof.frames=prof.active_mask=0;
 for(unsigned i=0;i<6;i++)prof.acc_voice[i]=0;
}
#endif
static void quantum(float *lr,unsigned n
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
 ,const md_usb_stems &stems,unsigned offset
#endif
) {
 PT_START(q0);
#ifdef MD_PROFILE
 prof_quanta_inc(n);
#endif
 creative_tick(n);
 // Conservative admission guard from 0.4.1 FM-1 costs with full-rate tape, LP24 and maximum repeats.
 // Count unmuted slots before playback so the tape cannot prefill four FULL grains
 // immediately before a dense six-voice attack. Existing audible tails also count.
 if(quality_on()){const uint8_t *weight=admission_weight;
  unsigned cost=12;
  // Provisional additional weights, pending physical-device measurement.
  if(fx_active||patch.fx_level)cost+=10;if(patch.kit.enabled)cost+=12;
  for(unsigned j=0;j<(patch.kit.enabled?3u:6u);j++)if(!patch.track[j].mute&&patch.ext[j].warps)cost+=2;
  for(unsigned i=0;i<(patch.kit.enabled?3u:6u);i++)if(voices[i].active||!patch.track[i].mute)cost+=weight[patch.track[i].model];
  if(cost>85){runtime_eco=true;stats.quality_reductions++;eco_settle();}
 }

 // A shared mode budget also applies in ECO. All six tracks remain audible.
 unsigned element_count=0;
 for(unsigned j=0;j<(patch.kit.enabled?3u:6u);j++)if(patch.track[j].model==MD_ELEMENTS&&(voices[j].active||!patch.track[j].mute))element_count++;
 unsigned element_modes=element_count>1?16/element_count:(quality_on()?16:8);
 element_modes=element_modes<2?2:element_modes;stats.element_modes=element_modes;
 const float block_ratio=n>32?n/32.0f:1.0f;
 const float slew25=n>32?.025f*block_ratio*(1-.0125f*(block_ratio-1)):.025f;
 const float slew40=n>32?.04f*block_ratio*(1-.02f*(block_ratio-1)):.04f;
 const float slew35=n>32?.035f*block_ratio*(1-.0175f*(block_ratio-1)):.035f;
 for(unsigned i=0;i<6;i++)stats.track_peak[i]*=n>32?.984064f:.992f;
#if defined(MD_PORT_PERFORMANCE)
 performance.room=performance_slew(performance.room,performance_target.room,n);
 performance.echo=performance_slew(performance.echo,performance_target.echo,n);
 performance.texture=performance_slew(performance.texture,performance_target.texture,n);
 performance.dusk=performance_slew(performance.dusk,performance_target.dusk,n);
 for(unsigned j=0;j<4;j++){
  const float offset=j==2?-.36f*performance.dusk:j==3?.35f*performance.texture:0.f;
  smooth[j]+=(fminf(1.f,fmaxf(0.f,unit(drift_macro(j))+offset))-smooth[j])*slew25;
 }
#else
 for(unsigned j=0;j<4;j++)smooth[j]+=(unit(drift_macro(j))-smooth[j])*slew25;
#endif
 bool source_active=playing;float delay_send[Q]={},room_send[Q]={};
#ifndef MD_MIX_PIPELINE
 float send[Q]={},out[Q],aux[Q],tmp[Q],exc[Q],duck[Q];memset(lr,0,n*2*sizeof(float));
#else
 float send[Q]={},pipeline_out[2][Q],pipeline_aux[2][Q],tmp[Q],exc[Q],duck[Q];
 memset(lr,0,n*2*sizeof(float));
 unsigned pipeline_slot=0;MixJob mix_job;bool mix_pending=false;
#ifdef MD_MIX_DEVICE
 unsigned mix_last=0;
 for(unsigned j=0;j<(patch.kit.enabled?3u:6u);j++)if(voices[j].active||voices[j].trigger)mix_last=j;
#endif
#endif
 // O-4: links and drift change only in creative_tick: ask once a quantum.
 const bool moving_quantum=creative_active();
 float duckdec=1.0f-1.0f/(MD_RATE*(.04f+unit(patch.duck_release)*1.46f));
 bool duck_identity=true;
 for(unsigned k=0;k<n;k++){duck[k]=1-duckenv*unit(patch.duck_amount)*.9f;duckenv*=duckdec;if(duck[k]!=1.f)duck_identity=false;}
 float grainrate=stmlib::SemitonesToRatio((float)patch.grain_pitch)*(patch.reverse?-1:1);
 float fb=unit(patch.feedback);
 // Both existing LFOs can address the shared grain bus. Sum in track order,
 // A then B; cap the final modulation once, without changing the saved bases.
 float grain_size_mod=0.f,grain_pos_mod=0.f;
 PT_START(vox0);
#ifdef MD_PROFILE
 prof.acc_pre+=vox0-q0;
#endif
 for(unsigned i=0;i<(patch.kit.enabled?3u:6u);i++) {
#ifdef MD_MIX_PIPELINE
  float (&out)[Q]=pipeline_out[pipeline_slot],(&aux)[Q]=pipeline_aux[pipeline_slot];
#endif
#ifdef MD_MIX_POISON
  // Test-only: a source must initialize every sample its mixer consumes.
  for(unsigned k=0;k<Q;k++){const uint32_t bits=0x7fc02000u+i;memcpy(out+k,&bits,4);memcpy(aux+k,&bits,4);}
#endif
#ifdef MD_SOURCE_MIX_PIPELINE
  WavetableSource wavetable_source={};
#endif
  md_track dynamic_track;md_extension dynamic_ext;const bool moving=moving_quantum;
  if(moving){dynamic_track=patch.track[i];dynamic_ext=patch.ext[i];creative_track(i,dynamic_track,dynamic_ext);}
  md_track &t=moving?dynamic_track:patch.track[i];Voice &v=voices[i];bool trig=v.trigger;v.trigger=0;
  if(trig) {v.active=1;v.attacking=md_has_envelope(t.model)||t.attack>0; if(!v.attacking)v.env=1;v.excite=1;v.lpg=1;}
  /* Only FM/VA have an observable final envelope. Never cut resonator/drum tails. */
  if(!v.active){stats.skipped_quanta++;continue;}stats.voice_quanta++;source_active=true;PT_START(vt);
#ifdef MD_PROFILE
  prof_active(i);
#endif

  const md_extension &e=moving?dynamic_ext:patch.ext[i];
  v.note+=(v.target_note-v.note)*(e.glide?n/(float)(n+MD_RATE*.35f*unit(e.glide)):1.0f);
  v.tide_phase+=n/(MD_RATE*(.5f+unit(e.tides_rate)*19.5f));if(v.tide_phase>=1)v.tide_phase-=1;
  const unsigned route=patch.motion.target[i],route2=patch.motion.target2[i];
  float wave=tidal_wave(v.tide_phase,e.tides?patch.motion.wave[i]:0);
  float tidal=wave*unit(e.tides),tide_gain=1-unit(e.tides)*.5f+tidal*.5f;
  float depth2=unit(patch.motion.depth2[i]),tidal2=0,tide_gain2=1;
  if(depth2){v.tide2_phase+=n/(MD_RATE*(.5f+unit(patch.motion.rate2[i])*19.5f));if(v.tide2_phase>=1)v.tide2_phase-=1;
   tidal2=tidal_wave(v.tide2_phase,patch.motion.wave2[i])*depth2;tide_gain2=1-depth2*.5f+tidal2*.5f;
  }
  if(route==MD_LFO_TARGET_GRAIN_SIZE)grain_size_mod+=tidal;
  if(route2==MD_LFO_TARGET_GRAIN_SIZE)grain_size_mod+=tidal2;
  if(route==MD_LFO_TARGET_GRAIN_POSITION)grain_pos_mod+=tidal;
  if(route2==MD_LFO_TARGET_GRAIN_POSITION)grain_pos_mod+=tidal2;
  v.breath_phase+=n/(MD_RATE*(7.0f+i*2.3f));if(v.breath_phase>=1)v.breath_phase-=1;
  const float breathing=unit(t.breathe)*plaits::Sine(v.breath_phase);
  float tone=fminf(.98f,fmaxf(t.model==MD_HARMONIC?0.0f:.02f,unit(t.tone)+(smooth[2]-.5f)*.65f));
  if(route==1||route2==1)tone=fminf(.98f,fmaxf(0.f,tone+((route==1?tidal:0)+(route2==1?tidal2:0))*.2f));
  float harm=unit(t.harm),shape=unit(t.shape);
  if(note_touch[i]!=0){
   if(t.model==MD_BOWED)tone=fminf(.98f,fmaxf(.02f,tone+note_touch[i]*.28f));
   if(t.model==MD_AERO)shape=fminf(1.0f,fmaxf(0.0f,shape+note_touch[i]*.45f));
   if(t.model==MD_FELT)shape=fminf(1.0f,fmaxf(0.0f,shape+note_touch[i]*.50f));
  }
  if(t.model==MD_CHOIR||t.model==MD_BASS||t.model==MD_MELLOW||t.model==MD_VELVET_KEYS||t.model==MD_BREATH||t.model==MD_CEDAR||t.model==MD_REED||t.model==MD_SKIN||t.model==MD_BOWED||t.model==MD_AERO||t.model==MD_FELT||t.model==MD_SILK||t.model==MD_COPPER||t.model==MD_VOWEL){
   // These original voices have no internal Mutable parameter interpolator.
   // A 25 ms control slew removes abrupt spectral jumps from live knob/CC edits.
   float c=n/(MD_RATE*.025f+n);
   v.colour_tone+=(tone-v.colour_tone)*c;v.colour_harm+=(harm-v.colour_harm)*c;v.colour_shape+=(shape-v.colour_shape)*c;
   tone=v.colour_tone;harm=v.colour_harm;shape=v.colour_shape;
  }
  bend_smooth[i]+=(bend[i]-bend_smooth[i])*slew40;
  float played_note=v.note+bend_smooth[i];
  if(finish_vintage){
   if(vint_wait[i]==0){const float peak=finish_vintage==1?.06f:.14f;vint_target[i]=((float)(int)(random_word(vint_rng)&1023u)-512.f)*(peak/512.f);vint_wait[i]=(uint16_t)(2000u+random_word(vint_rng)%4000u);}
   else vint_wait[i]--;
   vint_off[i]+=(vint_target[i]-vint_off[i])*.0004f;
   played_note+=vint_off[i];
  }else if(vint_off[i]!=0.f){vint_off[i]*=.99f;if(fabsf(vint_off[i])<1e-4f)vint_off[i]=0.f;played_note+=vint_off[i];}
  float f=440.0f*stmlib::SemitonesToRatio(played_note-69.0f)/MD_RATE;
  plaits::EngineParameters p={trig?1:0,played_note,tone,unit(t.shape),unit(t.harm)*.999f,v.velocity};bool done=false;
  memset(out,0,sizeof out);
  // Only the resonators write/read AUX. Keep its zero baseline there while
  // skipping a redundant tile clear for the other thirteen sound engines.
  // Rings::Resonator::Process overwrites both outputs on every sample.
  // Elements still uses its auxiliary buffer as an accumulator.
  if(t.model==MD_ELEMENTS)memset(aux,0,sizeof aux);
  bool eco=!quality_on()&&(t.model==MD_FM||t.model==MD_VA||t.model==MD_HARMONIC);
  unsigned engine_n=eco?(n+(v.eco_phase?0:1))/2:n;
  if(eco)p.note+=12.0f; // same concert pitch at 22.05 kHz; linear reconstruction below.
  switch(t.model) {
   case MD_KICK:VOICE_ENGINE(v,kick).Render(false,trig,v.velocity,f,tone*(.65f+.35f*tone),unit(t.decay),unit(t.harm)*.75f,.12f,out,n);break;
   case MD_SNARE:VOICE_ENGINE(v,snare).Render(false,trig,v.velocity,f,tone*(.7f+.3f*tone),unit(t.decay),unit(t.harm)*.85f,out,n);break;
   case MD_HAT:VOICE_ENGINE(v,hat).Render(false,trig,v.velocity,f,tone,unit(t.decay),unit(t.harm),tmp,exc,out,n);break;
   case MD_FM:
    // Keep the original feedback and ratio ranges. Articulate the index with
    // the amplitude contour and touch instead of a static bright sine bell.
    // Breathe adds very slow colour drift, not pitch randomness or detuning.
    p.timbre=(tone+.24f*tone*(1-tone))*fminf(1.0f,(.28f+.72f*v.env)*(.55f+.45f*v.velocity)*(1+.18f*breathing));
    if(engine_n)md_far(&md_ram_fm_render)(&VOICE_ENGINE(v,fm),&p,out,nullptr,engine_n,&done);break;
   case MD_VA:
    // A wider low-register pulse region instead of thin pulses at low Tone.
    // The upper half still exposes the original sync range.
    if(p.timbre<.5f)p.timbre=.28f+.44f*p.timbre;
    p.morph=fminf(1.0f,fmaxf(0.0f,p.morph+.065f*breathing));
    if(engine_n)md_far(&md_ram_va_render)(&VOICE_ENGINE(v,va),&p,out,nullptr,engine_n,&done);break;
   case MD_HARMONIC:
    p.timbre=fminf(1.0f,fmaxf(0.0f,tone*(.55f+.45f*tone)+.018f*breathing));
    p.harmonics*=.85f;
    if(engine_n)md_far(&md_ram_additive_render)(&VOICE_ENGINE(v,additive),&p,out,nullptr,engine_n,&done);break;
   case MD_ELEMENTS:{
    elements::Exciter &strike=element_strike[i];elements::Resonator &body=ELEMENT_BODY(i);
    if(trig)v.strike_gate=MD_RATE*(.03f+.5f*unit(t.shape));
    unsigned strike_flags=(trig?elements::EXCITER_FLAG_RISING_EDGE:0)|(v.strike_gate>0?elements::EXCITER_FLAG_GATE:0);v.strike_gate=fmaxf(0,v.strike_gate-n);
    strike.set_model(t.shape<334?elements::EXCITER_MODEL_MALLET:t.shape<667?elements::EXCITER_MODEL_PLECTRUM:elements::EXCITER_MODEL_PARTICLES);
    // In the Plectrum region, keep its main pulse 1.7-5.2 ms after the primer.
    // The original 14-38 ms mapping was continually restarted by 220 BPM
    // four-ratchet rolls (17 ms spacing), leaving only the tiny primer audible.
    strike.set_parameter(t.shape>=334&&t.shape<667?.05f+.15f*(t.shape-334)/332.0f:.1f+.8f*unit(t.shape));
    strike.set_timbre(.08f+.82f*tone*tone);md_far(&md_ram_exciter_process)(&strike,(uint8_t)strike_flags,exc,n);
    body.set_frequency(f);body.set_geometry(fminf(.999f,unit(t.harm)));body.set_brightness(tone*(.6f+.4f*tone));body.set_damping(unit(t.decay)*.8f);body.set_position(.2f+.16f*tone+.07f*breathing);body.set_resolution(element_modes);
    memset(tmp,0,sizeof tmp);md_far(&md_ram_resonator_process)(&body,tmp,exc,out,aux,n);break;}
   case MD_CEDAR:cedar_render(i,v,t,f,tone,harm,shape,trig,out,n);break;
   case MD_BOWED:cedar_render(i,v,t,f,tone,harm,shape,trig,out,n,true);break;
   case MD_AERO:{
    // Original warm blown-tube colour: tapered partials and two-pole breath.
    // Reuses the existing per-voice noise/phase state; no delay or new RAM.
    v.mellow_drift+=n/(MD_RATE*(8.f+i));if(v.mellow_drift>=1)v.mellow_drift-=1;
    float df=f*stmlib::SemitonesToRatio(.06f*shape*plaits::Sine(v.mellow_drift));
    float h2=(.03f+.19f*tone)*partial_gain(df*2),h3=.08f*harm*tone*partial_gain(df*3);
    float pole=.012f+.055f*tone,air=.08f+.16f*shape;
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=df;if(v.sub_phase>=1)v.sub_phase-=1;
     float noise=(random_word(cedar_state[i].rng)>>8)*(1.f/8388608.f)-1.f;
     v.noise_lp+=(noise-v.noise_lp)*pole;v.noise_slow+=(v.noise_lp-v.noise_slow)*(.35f*pole);
     out[k]=.62f*plaits::Sine(v.sub_phase)+h2*plaits::Sine(v.sub_phase*2)+h3*plaits::Sine(v.sub_phase*3)+air*(v.noise_lp-v.noise_slow);
    }break;}
   case MD_SKIN:skin_render(v,t,f,tone,harm,shape,trig,out,n);break;
   case MD_REED:{
    // Original breathing drawbars. Harmonics moves between pipe and reed;
    // every upper partial tapers before Nyquist. No samples or extra buffer.
    v.mellow_drift+=n/(MD_RATE*(7.f+i));if(v.mellow_drift>=1)v.mellow_drift-=1;
    float detune=stmlib::SemitonesToRatio(.12f*shape*plaits::Sine(v.mellow_drift));
    float a2=.24f*tone*(1-.65f*harm)*partial_gain(f*2),a3=.24f*tone*harm*partial_gain(f*3);
    float a4=.11f*tone*(1-harm)*partial_gain(f*4),a5=.10f*tone*harm*partial_gain(f*5);
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=f;if(v.sub_phase>=1)v.sub_phase-=1;
     v.mellow_phase+=f*detune;if(v.mellow_phase>=1)v.mellow_phase-=1;
     out[k]=.36f*(plaits::Sine(v.sub_phase)+plaits::Sine(v.mellow_phase))+a2*plaits::Sine(v.sub_phase*2)+a3*plaits::Sine(v.sub_phase*3)+a4*plaits::Sine(v.sub_phase*4)+a5*plaits::Sine(v.sub_phase*5);
    }break;}
   case MD_VELVET_KEYS:{
    // A mellow two-operator phase-modulation voice, with no feedback or hard sync.
    // Reduce the index before high notes could generate appreciable aliases.
    static const float ratios[]={1,2,3,4};float ratio=ratios[bound(t.harm*4/1001,0,3)];
    // A felt tine speaks first, then settles into its rounded body.
    float index=(.06f+tone*(.28f+.95f*v.excite))*fminf(1.0f,.08f/f)*(.3f+.7f*v.env);
    float body=.14f*shape*partial_gain(f*2);
    v.excite*=1.0f/(1+n/(MD_RATE*.24f));
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=f;if(v.sub_phase>=1)v.sub_phase-=1;
     v.mellow_phase+=f*ratio;if(v.mellow_phase>=1.f||v.mellow_phase<0.f)v.mellow_phase-=floorf(v.mellow_phase);   /* 2.4: the same value, the call only when the phase left [0,1) */
     // The shared sine lookup expects a nonnegative phase before wrapping.
     float wave=plaits::Sine(1.0f+v.sub_phase+index*.15915494f*plaits::Sine(v.mellow_phase));
     out[k]=.66f*wave+body*plaits::Sine(v.sub_phase*2);
    }break;}
   case MD_FELT:{
    // A tuned bar: a soft body, a first overtone and a separately damped,
    // inharmonic strike. Keep the strike continuous across fast retriggers.
    const float strike_frequency=f*(2.25f+harm*.82f);
    const float overtone=(.04f+.17f*tone)*partial_gain(f*2);
    const float strike_gain=(.08f+.27f*shape)*(.45f+.55f*v.velocity)*partial_gain(strike_frequency);
    v.excite*=1.0f/(1+n/(MD_RATE*(.035f+.19f*shape)));
    const float strike=strike_gain*v.excite;
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=f;if(v.sub_phase>=1)v.sub_phase-=1;
     v.mellow_phase+=strike_frequency;if(v.mellow_phase>=1)v.mellow_phase-=1;
     v.noise_lp+=(strike-v.noise_lp)*.004f;
     out[k]=.58f*plaits::Sine(v.sub_phase)+overtone*plaits::Sine(v.sub_phase*2)+v.noise_lp*plaits::Sine(v.mellow_phase);
    }
    break;}
   case MD_SILK:{
    // A pitch-stable centre with two drifting satellites. The sides never
    // sum into a hard waveform, and the odd partial fades before Nyquist.
    v.mellow_drift+=n/(MD_RATE*(5.0f+i*.91f));if(v.mellow_drift>=1)v.mellow_drift-=1;
    const float drift=plaits::Sine(v.mellow_drift)*shape;
    const float spread=.004f+.014f*harm;
    const float left=f*(1-spread+.003f*drift),right=f*(1+spread+.004f*drift);
    const float third=.14f*tone*partial_gain(3*f);
    const float ensemble=.15f+.06f*shape;
    float side=v.choir_phase[0];
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=f;if(v.sub_phase>=1)v.sub_phase-=1;
     v.mellow_phase+=left;if(v.mellow_phase>=1)v.mellow_phase-=1;
     side+=right;if(side>=1)side-=1;
     out[k]=.49f*plaits::Sine(v.sub_phase)-third*plaits::Sine(3*v.sub_phase)
      +ensemble*(plaits::Sine(v.mellow_phase)+plaits::Sine(side));
    }
    v.choir_phase[0]=side;
    break;}
   case MD_COPPER:{
    // Ring modulation gives only the two explicit sum and difference
    // sidebands. Keep a tuned core and taper the pair before Nyquist.
    const float partner=f*(1.5f+1.5f*harm);
    const float sides=partial_gain(f+partner);
    const float tail=.38f*tone*sides;
    const float strike_gain=.44f*shape*sides;
    v.excite*=1.f/(1+n/(MD_RATE*(.055f+.27f*shape)));
    const float strike=strike_gain*v.excite;
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=f;if(v.sub_phase>=1)v.sub_phase-=1;
     v.mellow_phase+=partner;if(v.mellow_phase>=1)v.mellow_phase-=1;
     const float core=plaits::Sine(v.sub_phase);
     v.noise_lp+=(strike-v.noise_lp)*.0032f;
     out[k]=.58f*core+(tail+v.noise_lp)*core*plaits::Sine(v.mellow_phase);
    }
    break;}
   case MD_VOWEL:{
    // Blend three harmonic profiles around a steady fundamental. A small
    // envelope-driven shift gives the onset a different vowel from the tail.
    static const uint8_t formants[3][4]={{18,7,2,1},{6,15,9,2},{1,4,8,13}};
    const float vowel=2.f*fminf(1.f,fmaxf(0.f,tone+shape*(.16f*breathing+.17f*(v.env-.5f))));
    const unsigned pair=vowel>=1.f;
    const float blend=vowel-pair;
    const float focus=.004f+.012f*harm;
    float gain[4];
    for(unsigned j=0;j<4;j++){
     const float lo=formants[pair][j],hi=formants[pair+1][j];
     gain[j]=(lo+(hi-lo)*blend)*focus*partial_gain((j+2)*f);
    }
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=f;if(v.sub_phase>=1.f)v.sub_phase-=1.f;
     const float phase=v.sub_phase;
     out[k]=.47f*plaits::Sine(phase)+gain[0]*plaits::Sine(2*phase)
      +gain[1]*plaits::Sine(3*phase)+gain[2]*plaits::Sine(4*phase)+gain[3]*plaits::Sine(5*phase);
    }
    break;}
   case MD_BREATH:{
    // Soft breath / ocean noise mixed with a pitched reed; no sample RAM.
    float c=.008f+.11f*tone*tone;
    float reed_colour=.20f*shape*partial_gain(f*3);
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=f;if(v.sub_phase>=1)v.sub_phase-=1;
     float w=noise();v.noise_lp+=(w-v.noise_lp)*c;v.noise_slow+=(v.noise_lp-v.noise_slow)*.0008f;
     float air=(v.noise_lp-v.noise_slow)*2.3f;
     float reed=.56f*plaits::Sine(v.sub_phase)+reed_colour*plaits::Sine(v.sub_phase*3);
     out[k]=reed+(air-reed)*harm;
    }break;}
   case MD_SPECTRAL:
   case MD_SATIN:{
    // Original rounded wavetable oscillator. Reduce harmonics continuously
    // before Nyquist; interpolate phase and waveform, with no oversampling RAM.
    float colour=fminf(2.999f,fmaxf(0.0f,unit(t.harm)*2.999f+breathing*(t.model==MD_SPECTRAL?.55f:.28f)));
    v.satin_morph+=(colour-v.satin_morph)*slew35;
    v.satin_tone+=(tone-v.satin_tone)*slew35;
    int shape=(int)v.satin_morph;float morph=v.satin_morph-shape;
    v.mellow_drift+=n/(MD_RATE*(9.0f+i));if(v.mellow_drift>=1)v.mellow_drift-=1;
    float f2=f*stmlib::SemitonesToRatio((t.model==MD_SPECTRAL?.22f:.16f)*unit(t.shape)*plaits::Sine(v.mellow_drift));
    int band=3;float highest=fmaxf(f,f2);
    while(band>0&&highest*(1<<band)>.45f)band--;
    float fade=band?fminf(1.0f,fmaxf(0.0f,(.45f-highest*(1<<band))/.1125f)):1;
    // O-6: the tile's tables are fixed: chosen once, not per sample and voice.
    const bool blend_low=band&&fade<1;
#ifdef MD_SOURCE_MIX_PIPELINE
    // Avoid serializing source i behind a still-busy mix(i-1). CPU0 can
    // render it in the other ping-pong slot while that mix remains owned.
    // The first eligible voice can still give its source to CPU1. A ready
    // hint is advisory: the unchanged bounded join below remains mandatory.
    if(!mix_pending||mix_source_worker_ready())
     wavetable_source={t.model==MD_SPECTRAL?2u:1u,(unsigned)band,(unsigned)shape,f2,morph,fade,blend_low};
    else
#endif
    {
     const float (*table)[4][257]=t.model==MD_SPECTRAL?spectral_table:satin_table;
     const float *ha=table[band][shape],*hb=table[band][shape+1];
     const float *la=blend_low?table[band-1][shape]:ha,*lb=blend_low?table[band-1][shape+1]:hb;
     md_far(&render_wavetable_tile)(v,f,f2,morph,ha,hb,la,lb,blend_low,fade,out,n);
#ifdef MD_SOURCE_MIX_PIPELINE
     source_mix_counts.serial++; // This source already ran on CPU0, exactly once.
#endif
    }
    break;}
   case MD_MELLOW:{
    // Finite odd-harmonic triangle blend: no discontinuous saw/square aliasing.
    // Small, slow independent drift; Tone adds rounded harmonics, Shape detune.
    v.mellow_drift+=n/(MD_RATE*(8.0f+i));if(v.mellow_drift>=1)v.mellow_drift-=1;
    float detune=stmlib::SemitonesToRatio(.10f*shape*plaits::Sine(v.mellow_drift));
    float h5=.04f*partial_gain(f*5),h3=partial_gain(f*3)/9,h2=.20f*harm*partial_gain(f*2);
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=f;if(v.sub_phase>=1)v.sub_phase-=1;
     v.mellow_phase+=f*detune;if(v.mellow_phase>=1)v.mellow_phase-=1;
     float a=plaits::Sine(v.sub_phase),b=plaits::Sine(v.mellow_phase);
     float tri=plaits::Sine(v.sub_phase*5)*h5-plaits::Sine(v.sub_phase*3)*h3;
     out[k]=.34f*(a+b)+tone*.8f*tri+h2*plaits::Sine(v.sub_phase*2);
    }break;}
   case MD_BASS:{
    // Shape opens a rounded octave body with the amplitude contour. Harmonics
    // mixes the sub; the fundamental remains the anchor, without a hard shaper.
    float body=(.10f+.28f*tone)*(.35f+.65f*v.env)*(.5f+.5f*shape)*partial_gain(f*2);
    for(unsigned k=0;k<n;k++){v.sub_phase+=f*.5f;if(v.sub_phase>=1)v.sub_phase-=1;
     out[k]=(.68f-.10f*tone)*plaits::Sine(v.sub_phase*2)+.30f*harm*plaits::Sine(v.sub_phase)+body*plaits::Sine(v.sub_phase*4);}
    break;}
   case MD_CHOIR:{
    static const int chord[4][3]={{0,3,7},{0,4,7},{0,5,7},{0,7,12}};
    int ci=bound(t.harm*4/1001,0,3);
    for(int j=0;j<3;j++){
     int note=md_quantize((int)played_note+chord[ci][j],patch.root,patch.scale);
     float hz=440.0f*stmlib::SemitonesToRatio(note-69.0f+(j-1)*shape*(.15f+.11f*breathing))/MD_RATE;
     float hz0=v.choir_frequency[j]?v.choir_frequency[j]:hz,dh=(hz-hz0)/n;
     float second=.24f*tone*partial_gain(hz*2),amp=j?.25f:.40f,phase=v.choir_phase[j];
     for(unsigned k=0;k<n;k++){
      hz0+=dh;phase+=hz0;if(phase>=1)phase-=1;
      out[k]+=amp*((1-second)*plaits::Sine(phase)+second*plaits::Sine(phase*2));
     }
     v.choir_phase[j]=phase;v.choir_frequency[j]=hz;
    }break;}
   case MD_RINGS:
    VOICE_ENGINE(v,rings).set_frequency(f);VOICE_ENGINE(v,rings).set_structure(unit(t.harm)*.999f);VOICE_ENGINE(v,rings).set_brightness(tone*(.6f+.4f*tone));
    VOICE_ENGINE(v,rings).set_damping(unit(t.decay)*.9f);VOICE_ENGINE(v,rings).set_position(.22f+.18f*tone+.07f*breathing);VOICE_ENGINE(v,rings).set_resolution(quality_on()?24:8);
    // A repeatable damped mallet carries the strike; a little decaying air
    // retains texture. A noise-only strike made equal-velocity notes vary
    // wildly at individual resonant modes. Leave the sounding body intact.
    const float mallet=.08f+.55f*tone;
    for(unsigned k=0;k<n;k++){v.noise_lp+=((trig&&k==0?2.6f:0)-v.noise_lp)*mallet;exc[k]=(v.noise_lp+noise()*v.excite*.085f)*v.velocity;v.excite*=.99f;}
    md_far(&md_ram_rings_process)(&VOICE_ENGINE(v,rings),exc,out,aux,n);break;
  }
  if(eco){memcpy(tmp,out,engine_n*sizeof(float));unsigned j=0;
   for(unsigned k=0;k<n;k++){if(!v.eco_phase){v.eco_previous=v.eco_next;v.eco_next=tmp[j++];out[k]=(v.eco_previous+v.eco_next)*.5f;}else out[k]=v.eco_next;v.eco_phase^=1;}
  }
  float cutoff=unit(t.cutoff)+((route==0?tidal:0)+(route2==0?tidal2:0))*.2f+breathing*.13f+(smooth[2]-.5f)*.18f;
  if(t.filter==3)cutoff-=.55f*(1-v.lpg);
  cutoff=fminf(1.0f,fmaxf(0.0f,cutoff));
  v.cut_smooth+=(cutoff-v.cut_smooth)*slew40;v.res_smooth+=(unit(t.resonance)-v.res_smooth)*slew40;
  if(t.filter){float fc=cutoff_hz(v.cut_smooth)/MD_RATE;
   v.filter1.set_f_q<stmlib::FREQUENCY_ACCURATE>(fc,filter_q(v.res_smooth));
   if(t.filter==2)v.filter2.set_f_q<stmlib::FREQUENCY_ACCURATE>(fc,.707f);}
  const bool melodic=md_has_envelope(t.model);
  // Fold velocity into the existing sample-rate gain slew. A changed accent
  // must not introduce an instantaneous gain discontinuity on a sounding tail.
  float target=t.mute?0:unit(t.level)*.24f*(melodic?v.velocity:1)*(1-.045f*fabsf(breathing));
  if(route==6)target*=1-unit(e.tides)*.35f+tidal*.35f;
  if(route2==6)target*=1-depth2*.35f+tidal2*.35f;
  float pan=fminf(1.0f,fmaxf(0.0f,unit(t.pan)+.035f*breathing+((route==2?tidal:0)+(route2==2?tidal2:0))*.35f)),dec=1.0f-1.0f/(MD_RATE*(.06f+unit(t.decay)*unit(t.decay)*16.0f));
  float attackstep=1.0f/(1+MD_RATE*(.002f+8.0f*unit(t.attack)*unit(t.attack))),sendlevel=unit(t.send);
  if(route==3)sendlevel*=tide_gain;
  if(route2==3)sendlevel*=tide_gain2;
  const float warp_target=unit(e.warps)*(route==5?tide_gain:1.f)*(route2==5?tide_gain2:1.f),ds=unit(e.delay_send)*(route==4?tide_gain:1.f)*(route2==4?tide_gain2:1.f),rs=unit(e.reverb_send)*(route==4?tide_gain:1.f)*(route2==4?tide_gain2:1.f),expr=expression[i];
  // Release inside this voice's envelope loop. Splitting the entire rack at
  // each track's gate deadline made six short staggered gates overload the
  // FM-1. No oscillator/filter reconfiguration is needed to end one gate.
  const unsigned release_at=seq_gate[i]>0&&seq_gate[i]<n?(unsigned)ceilf(seq_gate[i]):n;
  // K-1: the staggered end of an Itinera scene, eased across this quantum.
  float scene_gain=1.f,scene_step=0.f;
  if(itinera.active){
   const float step_frames=MD_RATE*60.0f/(tempo_f()*4.0f);
   const float remaining=(float)(journey_duration()>journey_ticks?journey_duration()-journey_ticks:0)+untilstep/step_frames;
   const float g=itinera_fade_gain(i,(float)journey_ticks-untilstep/step_frames,remaining);
   scene_gain=itinera.fade[i];scene_step=(g-scene_gain)/n;itinera.fade[i]=g;
  }
#ifdef MD_MIX_PIPELINE
  // Source generation stays on CPU0, including every original RNG draw.
  // Opt-in SATIN/SPECTRAL defer only their RNG-free oscillator tile to this
  // same ordered job; no CPU0 source may observe that slot before its join.
  // The previous mix owns the other out/aux slot and all shared mix buses.
  // Join before reusing this descriptor, then publish the next mix in track
  // order. Refusal means never accepted: the caller can execute exactly once.
  if(mix_pending){if(!mix_job_join(mix_job))return;mix_pending=false;}
  mix_job={&v,t,i,n,out,aux,lr,send,delay_send,room_send,duck,
   f,target,pan,dec,attackstep,sendlevel,warp_target,ds,rs,expr,melodic,
   release_at,scene_gain,scene_step
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
   ,stems,offset
#endif
   ,duck_identity
#ifdef MD_SOURCE_MIX_PIPELINE
   ,wavetable_source
#endif
  };
  #ifdef MD_MIX_DEVICE
  if(i!=mix_last&&n>=md_mix_min_frames&&md_mix_worker_available_device()){
   mix_job.check=mix_check(mix_job);
   if(md_mix_worker_submit_device(&mix_job)){mix_pending=true;mix_job_accepted(mix_job);}else mix_job_serial(mix_job);
  }else mix_job_serial(mix_job);
#else
  if(md_mix_worker_submit(&mix_job_run,&mix_job)){mix_pending=true;mix_job_accepted(mix_job);}
  else mix_job_serial(mix_job);
#endif
  pipeline_slot^=1u;
#else
  md_far(&mix_voice)(v,t,i,n,out,aux,lr,send,delay_send,room_send,duck,
   f,target,pan,dec,attackstep,sendlevel,warp_target,ds,rs,expr,melodic,
   release_at,scene_gain,scene_step
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
   ,stems,offset
#endif
   ,duck_identity
  );
#endif
  PT_ADD(acc_voice[i],vt);
 }
#ifdef MD_MIX_PIPELINE
 // Kit, effects, final quantization, MIDI/commands and the next quantum may
 // observe these states only after the last voice job has completed.
 if(mix_pending&&!mix_job_join(mix_job))return;
#endif
 PT_ADD(acc_vox,vox0);
 PT_START(misc0);
 kit_scene_gain=1.f;kit_scene_step=0.f;
 if(itinera.active&&patch.kit.enabled){
  const float step_frames=MD_RATE*60.0f/(tempo_f()*4.0f);
  const float remaining=(float)(journey_duration()>journey_ticks?journey_duration()-journey_ticks:0)+untilstep/step_frames;
  const float g=itinera_fade_gain(3,(float)journey_ticks-untilstep/step_frames,remaining);
  kit_scene_gain=itinera.fade[3];kit_scene_step=(g-kit_scene_gain)/n;itinera.fade[3]=g;
 }
 if(patch.kit.enabled)kit_mix(lr,send,delay_send,room_send,n
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
 ,stems,offset
#endif
 );
#if defined(MD_PORT_PERFORMANCE)
 if(performance.room>0.f||performance.echo>0.f||performance.texture>0.f){
  for(unsigned k=0;k<n;k++){
   const float dry=lr[2*k]+lr[2*k+1];
   room_send[k]+=dry*performance.room*.23f;
   delay_send[k]+=dry*performance.echo*.18f;
   send[k]+=dry*performance.texture*.13f;
  }
 }
#endif
 // Stop restores home controls immediately, but releases the last dry sample
 // continuously. This tiny anti-click carry is not another synthesis voice.
 if(journey_release[0]!=0||journey_release[1]!=0)for(unsigned k=0;k<n;k++)for(unsigned c=0;c<2;c++){
  lr[2*k+c]+=journey_release[c];journey_release[c]*=.9975f;if(fabsf(journey_release[c])<1e-9f)journey_release[c]=0;
 }
 if(itinera.active){itinera_arrive_audio(lr,send,delay_send,room_send,n);itinera_release_audio(lr,send,delay_send,room_send,n);}
 if(journey_active){journey_dry_last[0]=lr[2*n-2];journey_dry_last[1]=lr[2*n-1];}
#if defined(MD_PORT_PERFORMANCE)
 const float echo_feedback=unit(patch.delay_fb),room_decay=.45f+.5f*unit(patch.reverb_time),echo_target=fminf(16382.0f,patch.delay_ms*44.1f);
 const float effect_return=fmaxf(unit(patch.fx_level),.55f*fmaxf(performance.room,performance.echo));
#else
 const float echo_feedback=unit(patch.delay_fb),room_decay=.45f+.5f*unit(patch.reverb_time),effect_return=unit(patch.fx_level),echo_target=fminf(16382.0f,patch.delay_ms*44.1f);
#endif
 unsigned maxgrains=quality_on()?4:2;
 // K-4: the room's read offsets for this quantum (control rate, 0.11..0.29 Hz).
 const bool room_moving=itinera.active;float room_offset[4]={0,0,0,0};
 if(room_moving){
  static const float rate[4]={.11f,.17f,.23f,.29f};
  for(unsigned j=0;j<4;j++){
   room_mod_phase[j]+=rate[j]*n/MD_RATE;if(room_mod_phase[j]>=1)room_mod_phase[j]-=1;
   room_offset[j]=3.f+3.f*plaits::Sine(room_mod_phase[j]);
  }
 }
 // K-5: an Itinera dream's level gain for this quantum, eased from the last.
 const bool level_on=itinera.active;float level_gain=1.f,level_step=0.f,level_power=0.f;
 if(level_on){
  float target=itinera.gain;
  const unsigned fade_zone=itinera_fade_frames+30u*MD_RATE;
  if(itinera.level_short>1e-9f&&itinera.level_long>1e-9f&&itinera_remaining()>fade_zone){
   target=sqrtf(itinera.level_long/itinera.level_short);
   target=target<.63f?.63f:target>1.58f?1.58f:target;
  }
  const float next=itinera.gain+(target-itinera.gain)*(n/(30.f*MD_RATE));
  level_gain=itinera.gain;level_step=(next-level_gain)/n;
 }
 PT_START(fx0);
#ifdef MD_PROFILE
 prof.acc_misc+=fx0-misc0;
#endif
 float grain_size=unit(patch.grain_size),grain_pos=unit(patch.grain_pos);
 if(grain_size_mod!=0.f)grain_size=fminf(1.f,fmaxf(0.f,grain_size+fminf(1.f,fmaxf(-1.f,grain_size_mod))*.40f));
 if(grain_pos_mod!=0.f)grain_pos=fminf(1.f,fmaxf(0.f,grain_pos+fminf(1.f,fmaxf(-1.f,grain_pos_mod))*.30f));
 level_power=md_far(&render_effects)(lr,n,send,delay_send,room_send,duck,
  grainrate,fb,grain_size,grain_pos,source_active,echo_feedback,room_decay,effect_return,echo_target,
  maxgrains,room_moving,room_offset,level_on,level_gain,level_step);
 PT_ADD(acc_fx,fx0);
 if(level_on){
  // K-5: the mean power before the gain, short (8 s) and long (120 s); a
  // quiet passage (below about -90 dBFS) is not measured, never boosted.
  const float power=level_power/(2*n);
  itinera.gain=level_gain;
  if(power>1e-9f){
   if(itinera.level_long<=0)itinera.level_long=itinera.level_short=power;
   itinera.level_short+=(power-itinera.level_short)*(n/(8.f*MD_RATE));
   itinera.level_long+=(power-itinera.level_long)*(n/(120.f*MD_RATE));
  }
 }
 if(fx_active&&effect_return<=0.f&&fx_smooth<1e-7f)rest_fx(); // O-3
 PT_START(fin0);md_far(&finish_stages)(lr,n,trim_target_gain());PT_ADD(acc_fin,fin0);
 if(chorus_clear){chorus_clear=false;chorus_lp[0]=chorus_lp[1]=0.f;memset(chorus_buf,0,sizeof chorus_buf);}   /* resets stay out of the RAM function: it may call RAM code only */
 if(air_clear){air_clear=false;air_lp1[0]=air_lp1[1]=air_lp2[0]=air_lp2[1]=0.f;air_env=0.f;}
 PT_ADD(acc_q,q0);
 stats.frames+=n;
}
}
extern "C" {
#ifdef MD_SOURCE_MIX_PIPELINE
// Audio owner or caller holding its lock; worker never writes these counters.
void md_source_mix_stats(uint32_t out[3]){out[0]=source_mix_counts.accepted;out[1]=source_mix_counts.completed;out[2]=source_mix_counts.serial;}
#endif
int md_mapping_valid(const md_midi_mapping *m){return m&&m->enabled<=1&&m->port<=3&&m->channel<=16&&m->cc<=127&&m->target<12&&m->track<6&&!m->reserved[0]&&!m->reserved[1]&&(!m->enabled||learn_safe(m->cc));}
const md_midi_mapping *md_mapping(unsigned j){return j<16?&mappings[j]:0;}
int md_set_mapping(unsigned j,const md_midi_mapping *m){if(j>=16||!md_mapping_valid(m))return 0;mappings[j]=*m;mapping_generation++;learn_reset();return 1;}
unsigned md_mapping_generation(){return mapping_generation;}
int md_learn_armed(){return learn_arm>=0&&(int32_t)(stats.frames-learn_deadline)<0?learn_arm:-1;}
int md_learn_picked(unsigned j){return j<16?learn_picked[j]:0;}
const char *md_learn_target_name(int k){static const char *s[]={"DENSITY","MOTION","COLOUR","SPACE","LEVEL","CUTOFF","PAN","GRAIN SEND","DELAY SEND","ROOM SEND","DRIFT POS","DRIFT DEPTH"};return s[bound(k,0,11)];}
int md_next_learn_cc(int cc,int direction){for(int i=0;i<128;i++){cc=bound(cc+(direction<0?-1:1),1,119);if(learn_safe(cc))return cc;if(cc==1||cc==119)break;}return 1;}
int md_drift_ready(){return patch.drift.captured==3;}
int md_drift_position(){return bound((int)(drift_live*1000+.5f),0,1000);}
float md_link_value(unsigned j){return j<4?link_readout[j]:0;}
const char *md_link_target_name(int k){static const char *s[]={"CUTOFF","COLOUR","PAN","GRAIN SEND","FX SENDS","PROBABILITY"};return s[bound(k,0,5)];}
const char *md_link_signal_name(int k){static const char *s[]={"HIT","ENVELOPE","LFO A","LFO B"};return s[bound(k,0,3)];}
int md_hit(unsigned s,unsigned h,unsigned l,unsigned r) {if(!l||l>32)return 0;h=h>l?l:h;return (((s+r)%l)*h)%l<h;}
#ifdef MD_MIX_DEVICE
static void mix_device_job_ptr(void *arg){
 md_mix_worker_job_begin();
 if(!mix_span(arg,sizeof(MixJob),alignof(MixJob))){md_mix_worker_reject_device();md_mix_worker_job_end();return;}
 MixJob &j=*(MixJob*)arg;
 if(plat_core()!=1||!mix_valid(j)
#ifdef MD_CPU2_FAULT_TEST
 ||md_mix_worker_inject_device()==2
#endif
 ){md_mix_worker_reject_device();md_mix_worker_job_end();return;}
#ifdef MD_CPU2_FAULT_TEST
 if(md_mix_worker_inject_device()==1){j.lr[0]=.25f;plat_cpu2_sync();for(;;)__asm__ volatile("nop" ::: "memory");}
#endif
 mix_job_run(&j);md_mix_worker_job_end();
}
extern "C" void md_mix_device_job(uint32_t arg){mix_device_job_ptr((void*)(uintptr_t)arg);}
#ifdef MD_MIX_DEVICE_TEST
extern "C" void md_mix_device_job_host(void *arg){mix_device_job_ptr(arg);}
#endif
static uint32_t mix_test_hash(uint32_t h,const void *ptr,unsigned count){
 const unsigned char *p=(const unsigned char*)ptr;for(unsigned i=0;i<count;i++)h=(h^p[i])*16777619u;return h;
}
extern "C" int md_mix_device_selftest(void){
 // Boot only, before bank loading and audio interrupts. No user patch is involved.
 // Same object and buffer addresses on both cores; fresh deterministic states per run.
 const uint32_t initial_stem_rng=stem_rng;
 float out[64],aux[64],lr[128],send[64],echo_send[64],room_send[64],duck[64];
 int16_t stem[128*8+8];MixJob job;
 unsigned cases=0;bool good=true;
 for(unsigned model=0;model<24&&good;model++)for(unsigned filter=0;filter<4&&good;filter++)for(unsigned tile=0;tile<3&&good;tile++){
  const unsigned n=tile==0?1:tile==1?32:64,axis=(model+filter+tile)%4;
  const auto prepare=[&](){
   stem_rng=0x86421357u;
   patch.track[0].model=(int)model;patch.track[0].filter=(int)filter;patch.track[0].cutoff=650;patch.track[0].resonance=260;patch.track[0].level=600;
   patch.duck_source=axis==1?-1:1;
#ifdef MD_ENGINE_UNION
   destroy_voice_engine(0);
#endif
   memset((void*)&voices[0],0,sizeof(Voice));new (&voices[0]) Voice();Voice &v=voices[0]; // Mixer-only state: never initialize unrelated source engines/globals.
   v.active=1;v.env=axis==1?1.f:.7f;v.attacking=axis==0;v.midi_gate=axis==2?-1.f:0.f;v.velocity=.75f;v.lpg=.8f;
   v.amp=axis==1?.125f:.1f;v.fade=axis==1?1.f:.2f;v.expression_smooth=axis==1?1.f:.4f;v.pan_smooth=.5f;
   v.warp_smooth=axis==1?0.f:.2f;v.grain_send_smooth=axis==1?.25f:.1f;v.delay_send_smooth=axis==1?.25f:.1f;v.reverb_send_smooth=axis==1?.25f:.1f;
   v.filter1.Init();v.filter2.Init();v.filter1.set_f_q<stmlib::FREQUENCY_ACCURATE>(.09f,1.1f);v.filter2.set_f_q<stmlib::FREQUENCY_ACCURATE>(.09f,.707f);
   stats.track_peak[0]=0;stats.nonfinite=0;voice_quiet[0]=0;
   for(unsigned k=0;k<64;k++){out[k]=((int)(k*17u%41)-20)*.001f;aux[k]=((int)(k*7u%19)-9)*.002f;duck[k]=axis==1?1.f:.8f;}
   if(axis==2){const uint32_t bits[]={0x80000000u,0x00000001u,0x7f800000u,0x7fc00123u};for(unsigned k=0;k<4;k++)memcpy(out+k,bits+k,4);}
   memset(lr,0,sizeof lr);memset(send,0,sizeof send);memset(echo_send,0,sizeof echo_send);memset(room_send,0,sizeof room_send);memset(stem,0,sizeof stem);
   md_usb_stems stems={stem+2,stem+64*8+2,axis==3?32u:64u};
   job={&v,patch.track[0],0,n,out,axis==3?out:aux,lr,send,echo_send,room_send,duck,.01f,.125f,.5f,.995f,.02f,.25f,axis==1?0.f:.3f,.25f,.25f,axis==1?1.f:.7f,md_has_envelope(model)!=0,n>1?n/2:n,axis==3?.8f:1.f,axis==3?-.001f:0.f,stems,axis==3?31u:0u,axis==1,0};
   job.check=mix_check(job);
  };
  const auto hash=[&](){uint32_t h=2166136261u;h=mix_test_hash(h,&voices[0],sizeof(Voice));h=mix_test_hash(h,out,sizeof out);h=mix_test_hash(h,lr,sizeof lr);h=mix_test_hash(h,send,sizeof send);h=mix_test_hash(h,echo_send,sizeof echo_send);h=mix_test_hash(h,room_send,sizeof room_send);h=mix_test_hash(h,stem,sizeof stem);h=mix_test_hash(h,&stem_rng,4);h=mix_test_hash(h,&voice_quiet[0],4);h=mix_test_hash(h,&stats.nonfinite,4);h=mix_test_hash(h,&stats.track_peak[0],4);return h;};
  prepare();mix_job_run(&job);const uint32_t expected=hash();
  prepare();md_mix_worker_test_begin();
  if(!md_mix_worker_submit_device(&job)){good=false;break;}
  if(md_mix_worker_wait_device()<0||hash()!=expected){good=false;break;}
  cases++;
 }
 // Halt on failure while stack buffers still exist, before returning to the caller.
 if(!good)md_mix_worker_retire_device();
 md_init();stem_rng=initial_stem_rng;
 return good?(int)cases:-1;
}
extern "C" void md_mix_device_recover(void){
 out_all_off();journey_stop();playing=0;soft_biome=-1;clear_audio();for(unsigned i=0;i<6;i++)reset_voice(i);
}
#endif
void md_init() {
#ifdef MD_SOURCE_MIX_PIPELINE
 source_mix_counts={}; // init is outside any accepted job
#endif
 out_all_off(); /* 2.2: no external note left hanging across a restart of the engine */
#if defined(MD_PORT_PERFORMANCE)
 performance_target=performance={};
#endif
 itinera={};journey_active=journey_prepared=0;trim_gain=-1.f;finish_width=level_match_off=0;width_now=1.f;finish_air=finish_chorus=0;tempo_shift=0;finish_vintage=0;chorus_amount=0.f;chorus_lp[0]=chorus_lp[1]=0.f;air_amount=air_env=0.f;air_lp1[0]=air_lp1[1]=air_lp2[0]=air_lp2[1]=0.f;
 sequence_clip_kind=0;sequence_notice=0;
 velocity_touch=0;
#ifdef MD_ENGINE_UNION
 if(voices)for(unsigned i=0;i<6;i++)destroy_voice_engine(i);
#endif
 voices=reinterpret_cast<Voice*>(voices_raw);memset(voices_raw,0,sizeof voices_raw);memset(tape,0,sizeof tape);memset(grains,0,sizeof grains);
 memset(&stats,0,sizeof stats);memset(smooth,0,sizeof smooth);memset(dcx,0,sizeof dcx);memset(dcy,0,sizeof dcy);
 memset(rpn_select,127,sizeof rpn_select);memset(bend_semitones,2,sizeof bend_semitones);memset(bend_cents,0,sizeof bend_cents);memset(macro14,0,sizeof macro14);memset(macro_lsb_seen,0,sizeof macro_lsb_seen);
 learn_init();memset(midi_bank,0,sizeof midi_bank);for(unsigned i=0;i<6;i++)midi_route[i]=i+1;midi_macro_enabled=1;qread=qwrite=writehead=grainclock=tape_valid=0;feedback_lp=0;rng=0x88888;stmlib::Random::Seed(rng);scene(0);playing=0;clock_source=midi_quantize=clock_pulse=0;clock_age=clock_last=0;clock_interval=0;
}
#if defined(MD_PORT_PERFORMANCE)
void md_performance_set(float room,float echo,float texture,float dusk){
 const auto level=[](float value){return fminf(1.f,fmaxf(0.f,value));};
 performance_target={level(room),level(echo),level(texture),level(dusk)};
}
#endif
int md_clock_source(void){return clock_source;}
int md_midi_quantize(void){return midi_quantize;}
int md_midi_route(unsigned track){return track<6?midi_route[track]:0;}
void md_set_velocity_touch(unsigned percent){velocity_touch=percent>100?100:percent;}
int md_midi_held_port(unsigned track){return track<6?held_port[track]:-1;}
int md_midi_macro_enabled(void){return midi_macro_enabled;}
float md_midi_bend_range(unsigned i){return i<6?bend_range(i):0;}
float md_midi_bend(unsigned i){return i<6?bend[i]:0;}
void md_midi_performance_state(unsigned i,int32_t out[7]){
 if(i>=6){memset(out,0,7*sizeof *out);return;}
 out[0]=held_port[i];out[1]=(int32_t)(bend[i]*1000);out[2]=(int32_t)(expression[i]*1000);out[3]=sustain[i];out[4]=bend_port[i];out[5]=expression_port[i];out[6]=sustain_port[i];
}
void md_set_midi_route(unsigned track,unsigned channel){if(track>=6||channel>16||midi_route[track]==channel)return;midi_route[track]=channel;bend_semitones[track]=2;bend_cents[track]=0;bend_wheel[track]=8192;bend[track]=0;held[track]=held_port[track]=-1;sustain[track]=0;voices[track].midi_gate=-1;expression[track]=1;bend_port[track]=expression_port[track]=sustain_port[track]=-1;}
void md_set_midi_macro_enabled(int enabled){if(midi_macro_enabled!=!!enabled){memset(macro14,0,sizeof macro14);memset(macro_lsb_seen,0,sizeof macro_lsb_seen);}midi_macro_enabled=!!enabled;}
void md_midi_lost(int port){
 if(port<0||port>=3)return;
 learn_reset_port(port);memset(rpn_select[port],127,sizeof rpn_select[port]);memset(macro14[port],0,sizeof macro14[port]);macro_lsb_seen[port]=0;
 for(unsigned i=0;i<6;i++){
  if(bend_port[i]==port){bend_wheel[i]=8192;bend[i]=0;bend_port[i]=-1;}
  if(expression_port[i]==port){expression[i]=1;expression_port[i]=-1;}
  if(sustain_port[i]==port){sustain[i]=0;sustain_port[i]=-1;if(held[i]<0){held_port[i]=-1;voices[i].midi_gate=-1;}}
  if(held_port[i]==port){held[i]=held_port[i]=-1;voices[i].midi_gate=-1;}
 }
}
void md_midi(unsigned st,unsigned a,unsigned b,int port){
 if(port<0||port>2||(itinera.active&&(st&240)!=0xb0))return;
 a&=127;b&=127;
 if(st>=0xf8){
  if(!clock_source||port!=(clock_source==1?0:clock_source==2?2:1))return;
  if(st==0xfa||st==0xfc){out_all_off();clock_seen=0;}
  if(st==0xfa){if(journey_active)journey_stop();journey_start();step=0;sequence_rewind();seqrng=patch.seed;clock_pulse=0;playing=1;clock_age=0;clock_last=stats.frames;clock_interval=0;memset(repeats,0,sizeof repeats);}
  else if(st==0xfb){if(!playing)journey_start();playing=1;clock_age=0;}
  else if(st==0xfc){fill_state=0;journey_stop();playing=0;record_mode=0;record_note=record_step=-1;memset(repeats,0,sizeof repeats);}
  else if(st==0xf8&&playing){unsigned delta=stats.frames-clock_last;clock_last=stats.frames;
   // F-2: once a whole beat (24 pulses) is known, the tempo comes from its
   // span and changes only by more than 0.6 BPM; before that, as before.
   if(delta>MD_RATE*60/(30*24)*2)clock_seen=0; // (a pause: start again)
   if(clock_seen>=24){
    const uint32_t span=stats.frames-clock_hist[clock_seen%24];
    if(span>=MD_RATE*60/220&&span<=MD_RATE*60/30){
     const float bpm=MD_RATE*60.0f/span;clock_interval=span/24.0f;
     if(fabsf(bpm-(float)tempo_now())>.6f)clock_tempo=bound((int)(bpm+.5f),30,220);
    }
   }else if(delta>=MD_RATE*60/(220*24)&&delta<=MD_RATE*60/(30*24)){clock_interval=clock_interval?clock_interval*.85f+delta*.15f:delta;clock_tempo=bound((int)(MD_RATE*60/(24*clock_interval)+.5f),30,220);}
   clock_hist[clock_seen%24]=stats.frames;clock_seen++;
   clock_age=0;if(clock_pulse==0){untilstep=0;tick();}clock_pulse=(clock_pulse+1)%6;}
  return;
 }
 unsigned channel=(st&15)+1,type=st&240;
 if(type==0xb0&&learn_cc(port,channel,a,b))return;
 // MSB-only controllers retain the exact old 7-bit mapping. Fine data opts
 // only this transport/macro into a full 14-bit pair (CC16..19 + CC48..51).
 if(channel==16&&midi_macro_enabled&&type==0xb0&&((a>=16&&a<=19)||(a>=48&&a<=51))){
  unsigned k=a&3;uint16_t &v=macro14[port][k];
  if(a<32)v=(v&127)|(b<<7);else{v=(v&16256)|b;macro_lsb_seen[port]|=1u<<k;}
  int value=(macro_lsb_seen[port]&(1u<<k))?(int)((uint32_t)v*1000/16383):(int)b*1000/127;
  apply({MD_MACRO,(int)k,value});return;
 }
 // A dream accepts its four macro CCs only. Notes, transport, RPN and other
 // performance controllers keep their existing isolation from the story.
 if(itinera.active)return;
 if(type==0xb0){if(a==101)rpn_select[port][channel-1][0]=b;else if(a==100)rpn_select[port][channel-1][1]=b;else if(a==99||a==98||a==121)rpn_select[port][channel-1][0]=rpn_select[port][channel-1][1]=127;}

 for(unsigned i=0;i<(patch.kit.enabled?4u:6u);i++){
 if(midi_route[i]!=channel)continue;
 Voice &v=voices[i];
 if(patch.kit.enabled&&i==3){if(type==0x90&&b){kit_fire(kit_note(a),b/127.f);stats.events[3]++;if(record_mode&&record_track==3){unsigned at=record_mode==1?patch.kit.step:((step?step-1:0)/patch.track[3].speed)%patch.kit.length;patch.kit.gates[kit_note(a)]|=1u<<at;if(record_mode==1)patch.kit.step=(at+1)%patch.kit.length;}}else if(type==0xb0&&(a==120||a==123))kit_reset();if(type!=0xb0)continue;}
 if(type==0x90&&b){
  int note=midi_quantize?md_quantize(a,patch.root,patch.scale):bound(a,24,96);
  float velocity=b/127.0f;
  capture_note(i,bound(a,24,96),velocity);
  fire(i,note,velocity);
  if(velocity_touch&&(v.model==MD_BOWED||v.model==MD_AERO||v.model==MD_FELT)){
   note_touch[i]=(velocity-.5f)*(velocity_touch*.02f);
  }
  held[i]=a;held_port[i]=port;v.midi_gate=md_has_envelope(v.model)?1:0;
 }
 else if(type==0x80||(type==0x90&&!b)){if(held[i]==(int)a&&held_port[i]==port){capture_off(i,bound(a,24,96));held[i]=-1;if(!sustain[i]){held_port[i]=-1;v.midi_gate=-1;}}}
 else if(type==0xc0&&!journey_active){unsigned id=midi_bank[i]*128+a;if(id<MD_TONE_PRESETS)tone_preset(i,id);}
 else if(type==0xe0){bend_wheel[i]=a|(b<<7);bend_port[i]=port;update_bend(i);}
 else if(type==0xb0){int value=b*1000/127;
  if((a==6||a==38||a==96||a==97)&&rpn_select[port][channel-1][0]==0&&rpn_select[port][channel-1][1]==0){
   if(a==6)bend_semitones[i]=bound(b,0,24);else if(a==38)bend_cents[i]=bound(b,0,99);else bend_semitones[i]=bound((int)bend_semitones[i]+(a==96?1:-1),0,24);
   update_bend(i);
  }
  else if(a==0)midi_bank[i]=b;
  else if(a==32){} /* Only LSB zero supported; reserved, never a send. */
  else if(a==121){expression[i]=1;bend_wheel[i]=8192;bend[i]=0;bend_port[i]=expression_port[i]=sustain_port[i]=-1;rpn_select[port][channel-1][0]=rpn_select[port][channel-1][1]=127;sustain[i]=0;if(held[i]<0){held_port[i]=-1;v.midi_gate=-1;}}
  else if(a==64){sustain[i]=b>=64;sustain_port[i]=sustain[i]?port:-1;if(!sustain[i]&&held[i]<0){held_port[i]=-1;v.midi_gate=-1;}}
  else if(a==120)reset_voice(i);
  else if(a==123){held[i]=held_port[i]=-1;sustain[i]=0;sustain_port[i]=-1;v.midi_gate=-1;}
  else if(a==11){expression[i]=b/127.0f;expression_port[i]=port;}
  else {int c=a==7?MD_LEVEL:a==10?MD_PAN:a==74?MD_CUTOFF:a==71?MD_RESONANCE:a==91?MD_REVERB_SEND:a==12?MD_DELAY_SEND:a==13?MD_SEND:a==5?MD_GLIDE:-1;if(c>=0)apply({c,(int)i,value});}
 }
 }
}
// F-2: a callback's block begins; anything still waiting from the last one plays now.
void md_block_begin(void){
#ifdef MD_PROFILE
 prof.cb0=plat_ticks24();prof.reads=1;   /* the whole app_render starts here (app.c), 256 frames = the 5804 us block */
#endif
 for(unsigned j=0;j<midi_at_n;j++)md_midi(midi_at[j].st,midi_at[j].a,midi_at[j].b,midi_at[j].port);
 midi_at_n=0;block_pos=0;
}
void md_midi_at(unsigned st,unsigned a,unsigned b,int port,unsigned frame){
 if(frame<=block_pos||midi_at_n>=sizeof midi_at/sizeof midi_at[0]){md_midi(st,a,b,port);return;}
 unsigned i=midi_at_n++;
 while(i&&midi_at[i-1].frame>frame){midi_at[i]=midi_at[i-1];i--;}
 midi_at[i]={frame,(uint8_t)st,(uint8_t)a,(uint8_t)b,(int8_t)port};
}
unsigned md_command_space(void){return 64-(qwrite-qread);}
int md_command_valid(int c){return (c>=0&&c<=98)||(c>=120&&c<=123)||c==125||c==126||(c>=MD_JOURNEY_MODE&&c<MD_COMMAND_COUNT);}
int md_command_allowed(int c){if(itinera.active)return c==MD_PLAY||c==MD_PANIC||c==MD_MACRO;if(c==MD_ITINERA_START)return !playing&&pending_biome<0;if(c==MD_DRIFT_CAPTURE&&patch.drift.mode)return 0;if(c==MD_SEQ_COPY||c==MD_FILL)return 1;if(((c>=MD_SEQ_PASTE&&c<=MD_SEQ_MIRROR)||c==MD_SEQ_RULE||c==MD_SEQ_EXPRESSION)&&record_mode)return 0;return !journey_active||(c>=MD_LEARN_TARGET&&c<=MD_LEARN_ENABLE)||c==MD_DRIFT_POSITION||c==MD_DRIFT_DEPTH||c==MD_LEVEL||c==MD_PAN||c==MD_CUTOFF||c==MD_SEND||c==MD_DELAY_SEND||c==MD_REVERB_SEND||c==MD_PLAY||c==MD_PANIC||c==MD_TRIGGER||c==MD_NOTE_OFF||c==MD_KIT_TRIGGER||c==MD_MUTE||c==MD_MACRO||c==MD_QUALITY;}
int md_command(int c,int t,int v) {if(!md_command_valid(c)||!md_command_allowed(c))return 0;unsigned w=qwrite;if(w-qread>=64){stats.dropped_commands++;return 0;}queue[w&63]={c,t,v};fence();qwrite=w+1;return 1;}
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
void md_render_stems(float *lr,unsigned n,md_usb_stems stems) {
 unsigned offset=0;
#else
void md_render(float *lr,unsigned n) {
#endif
 for(unsigned i=0;i<6;i++)if(out_note[i]>=0&&(out_left[i]-=(float)n)<=0)out_off(i);
 while(n){
  // F-2: MIDI due at or before this point of the block, in order.
  while(midi_at_n&&midi_at[0].frame<=block_pos){const MidiAt e=midi_at[0];midi_at_n--;for(unsigned j=0;j<midi_at_n;j++)midi_at[j]=midi_at[j+1];md_midi(e.st,e.a,e.b,e.port);}
  while(qread!=qwrite){fence();Command e=queue[qread&63];
   // scene() rebuilds the whole rack from the Biome ID, so in a run of
   // consecutive Biome requests only the last one can be heard. Rebuild once
   // per run instead of once per encoder detent inside a single callback.
   if(e.c==MD_SCENE&&qread+1!=qwrite&&queue[(qread+1)&63].c==MD_SCENE){fence();qread++;continue;}
   apply(e);fence();qread++;}
  unsigned z=n<Q?n:Q;
  if(midi_at_n&&z>midi_at[0].frame-block_pos)z=midi_at[0].frame-block_pos; // F-2: stop at the next event
  if(itinera.active){
   const unsigned remaining=itinera_remaining();
   if(!remaining){out_all_off();journey_stop();fill_state=0;playing=0;untilstep=0;}
   else if(z>remaining)z=remaining;
  }
  if(playing){
   if(!clock_source&&untilstep<=0){PT_START(tk0);tick();PT_ADD(acc_tick,tk0);}
   for(unsigned i=0;i<6;i++)if(repeats[i].remaining&&repeats[i].left<=0){
    if(!patch.track[i].mute){fire(i,repeats[i].note,repeats[i].velocity);out_on(i,repeats[i].note,repeats[i].velocity,repeats[i].spacing*.5f);}
    repeats[i].remaining--;repeats[i].left+=repeats[i].spacing;
   }
   unsigned to=(!clock_source&&untilstep>0)?(unsigned)ceilf(untilstep):0;if(to&&z>to)z=to;
   for(unsigned i=0;i<6;i++)if(repeats[i].remaining){to=(unsigned)ceilf(repeats[i].left);if(to&&z>to)z=to;}
  }
  if(playing){untilstep-=z;for(unsigned i=0;i<6;i++)if(repeats[i].remaining)repeats[i].left-=z;}
  if(clock_source&&playing){clock_age+=z;if(clock_age>MD_RATE*2){out_all_off();fill_state=0;journey_stop();playing=0;record_mode=0;record_note=record_step=-1;memset(repeats,0,sizeof repeats);}}
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
  quantum(lr,z,stems,offset);
#ifdef MD_MIX_DEVICE
  if(md_mix_worker_failed_device()){playing=0;return;}
#endif
  offset+=z;
#else
  quantum(lr,z);
#endif
  for(unsigned i=0;i<6;i++)if(seq_gate[i]>0){seq_gate[i]-=z;if(seq_gate[i]<=0)voices[i].midi_gate=-1;}lr+=z*2;n-=z;block_pos+=z;
 }
}
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
void md_render(float *lr,unsigned n){md_render_stems(lr,n,{});}
#endif
int md_unsaved(){uint32_t h[2];patch_hash(h);return h[0]!=saved_hash[0]||h[1]!=saved_hash[1];}
void md_mark_saved(){patch_hash(saved_hash);}
void md_mark_saved_from(const md_patch *written){if(written)patch_hash_of(*written,saved_hash);}
/* 2.2 development: saved images stay readable by 2.1, whose validator knows 272 Biomes and
 * 632 tones. A 2.2 factory ID is a label only (the sound is in the patch fields), so a
 * saved copy carries the user form: Biome -1 (USER BIOME) and tone -1 (edited sound). */
/* 2.2 development: sequencer notes out (see out_on). */
void md_set_midi_out(unsigned track,unsigned channel){if(track<6&&channel<=16&&out_channel[track]!=channel){out_off(track);out_channel[track]=(uint8_t)channel;}}
void md_set_keep_setting(int on){keep_setting=on!=0;}
int md_keep_setting(void){return keep_setting;}
void md_set_biome_change(int mode){soft_mode=(uint8_t)bound(mode,0,2);}
int md_biome_change(void){return soft_mode;}
int md_biome_arriving(void){return soft_biome;}
int md_midi_out_channel(unsigned track){return track<6?out_channel[track]:0;}
int md_midi_out_read(uint32_t *packet){const unsigned t=out_tail;if(t==out_head)return 0;fence();*packet=out_ring[t&63u];fence();out_tail=t+1;return 1;}
void md_patch_portable(md_patch *p){
 if(p->biome_id>=MD_V21_BIOMES)p->biome_id=-1;
 for(int i=0;i<MD_TRACKS;i++)if(p->tone_preset[i]>=MD_V21_TONES)p->tone_preset[i]=-1;
 for(auto &a:p->journey.section)if(a.biome!=65535&&a.biome>=MD_V21_BIOMES)a.biome=65535;
}
void md_adopt_portable(void){md_patch_portable(&patch);md_patch_portable(&journey_home);md_patch_portable(&previous);}
int md_pending_biome(){return pending_biome;}
int md_active_tracks(){return patch.kit.enabled?4:6;}
const char *md_kit_name(int n){static const char *s[]={"DUST ROOM","PAPER MACHINES","SOFT STATIC","FOUND OBJECTS","VELOUR TAPE"};return s[bound(n,0,4)];}
const char *md_pad_name(int n){static const char *s[]={"PILLOW KICK","BRUSH SNARE","DUST HAT","WOOD KNOCK","TINY CLICK","TAPE HISS","CRACKLE","AIR TEXTURE"};return s[bound(n,0,7)];}
const md_patch *md_state(){return &patch;}
int md_quality_active(){return quality_on();}
int md_tempo_now(){return tempo_now();}
int md_tempo_tenths(){return clock_source&&clock_tempo?clock_tempo*10:(patch.tempo*10+((1<<tempo_shift)>>1))>>tempo_shift;}   /* 2.4: tenths of a BPM, so TEMPO RANGE shows what plays (3.75 BPM reads 3.8) */
void md_overload(){if(quality_on()){runtime_eco=true;stats.quality_reductions++;eco_settle();}}   /* 2.4: a fall back to ECO on the device (over 90 % of the block) is counted like the admission guard's */
const md_patch *md_project_state(){return journey_active?&journey_home:&patch;}
void md_itinera_read(md_itinera_view *view){
 view->active=itinera.active;
 view->biome=patch.biome_id;
 view->remaining=itinera.active?(itinera_remaining()+MD_RATE-1)/MD_RATE:0;
 view->section=itinera.part;
 view->preset=itinera.preset;
 view->code=itinera.active?itinera.code:0;
 for(unsigned k=0;k<4;k++)view->macro[k]=itinera.active?itinera.user[k]:0;
 view->progress=itinera.active?journey_ticks*1000u/itinera.section_steps:0;
 view->chapter=itinera.active?(itinera.length-itinera_remaining())/(itinera.length/4):0;
 float energy=0;
 if(itinera.active)for(unsigned i=0;i<MD_TRACKS;i++)energy+=stats.track_peak[i];
 view->energy=bound((int)(energy*90.f),0,100);
}
void md_set_vintage(int level){finish_vintage=(uint8_t)bound(level,0,2);}
void md_set_tempo_shift(int shift){tempo_shift=(uint8_t)bound(shift,0,3);}
void md_set_mine(const uint16_t *ids,int count){
 /* only real Biomes (the Blank Biome, 64, is silent); at least 4 */
 uint16_t good[MD_BIOMES];int n=0;
 for(int i=0;i<count&&n<(int)MD_BIOMES;i++)if(ids[i]<MD_BIOMES&&ids[i]!=64)good[n++]=ids[i];
 if(n<4){mine_ready=0;return;}
 /* 32 scenes: the starred Biomes cycled with a stride that is coprime to their number, so every one is visited; the last scene repeats the first */
 unsigned stride=7;
 for(;;){unsigned a=(unsigned)n,b=stride;while(b){unsigned t=a%b;a=b;b=t;}if(a==1)break;stride+=2;}
 for(int i=0;i<(int)itinera_scenes;i++)mine_ids[i]=good[(i*stride)%(unsigned)n];
 mine_ids[itinera_scenes-1]=good[0];
 mine_ready=1;
}
void md_set_width(int level){finish_width=(uint8_t)bound(level,0,3);}
void md_set_chorus(int on){finish_chorus=on?1:0;}
void md_set_air(int level){finish_air=(uint8_t)bound(level,0,3);}
void md_set_level_match(int on){level_match_off=on?0:1;}
const char *md_itinera_name(unsigned p){return itinera_presets[p%MD_ITINERA_PRESETS].name;}
unsigned md_itinera_defaults(unsigned p){const auto &a=itinera_presets[p%MD_ITINERA_PRESETS];return 1+4*a.pace+12*a.hall;}
int md_journey_active(){return journey_active&&!itinera.active;}
int md_journey_position(){return md_journey_active()?journey_at:-1;}
unsigned md_journey_step(){return md_journey_active()&&journey_ticks?journey_ticks-1:0;}
int md_preview_biome_kit(md_patch *scratch,int biome){
 factory_scene(*scratch,biome);
 return scratch->kit.enabled;
}
void md_grain_positions(uint8_t position[4],uint8_t window[4]){
 for(unsigned i=0;i<4;i++){
  const Grain &g=grains[i];
  position[i]=(unsigned)g.pos/(GN/128u);
  window[i]=g.life&&g.age<g.life?(unsigned)(g.phase*127)+1:0;
 }
}
unsigned md_journey_bar(){return md_journey_active()?(journey_ticks?journey_ticks-1:0)/16%patch.journey.section[journey_at].bars+1:0;}
unsigned md_journey_repeat(){return md_journey_active()?(journey_ticks?journey_ticks-1:0)/(16*patch.journey.section[journey_at].bars)+1:0;}const md_stats *md_status(){return &stats;}int md_playing(){return playing;}
int md_validate(const md_patch *p) {
 if(!p||p->magic!=0x4D445231||p->version!=MD_PATCH_VERSION||!p->seed||p->tempo<30||p->tempo>220)return 0;
 if(p->biome_id< -1||p->biome_id>=MD_BIOMES||p->edited_mask>127)return 0;
 for(int i=0;i<6;i++)if(p->tone_preset[i]< -1||p->tone_preset[i]>=MD_TONE_PRESETS)return 0;
 for(int j=0;j<4;j++)if(p->macro[j]<0||p->macro[j]>1000)return 0;
 if(p->locked<0||p->locked>1||p->freeze<0||p->freeze>1||p->quality<0||p->quality>1)return 0;
 if(p->grain_size<0||p->grain_size>1000||p->grain_pos<0||p->grain_pos>1000||p->feedback<0||p->feedback>850)return 0;
 if(p->root<0||p->root>11||p->scale<0||p->scale>12||p->transpose< -24||p->transpose>24||p->swing<0||p->swing>600||p->grain_pitch< -24||p->grain_pitch>24||p->reverse<0||p->reverse>1||p->duck_source< -1||p->duck_source>5||p->duck_amount<0||p->duck_amount>1000||p->duck_release<0||p->duck_release>1000)return 0;
 for(int i=0;i<6;i++){const md_track &t=p->track[i];if(t.attack<0||t.attack>1000||t.ratchet<1||t.ratchet>4||t.accent<0||t.accent>1000)return 0;if(t.model<0||t.model>=MD_MODELS||t.length<1||t.length>32||t.hits<0||t.hits>t.length||t.rotate<0||t.rotate>=t.length||t.note<24||t.note>96||t.mute<0||t.mute>1||t.speed<1||t.speed>8)return 0;
 if(t.filter<0||t.filter>3)return 0;const int q[]={t.tone,t.harm,t.decay,t.level,t.send,t.shape,t.cutoff,t.resonance,t.breathe};for(int j=0;j<9;j++)if(q[j]<0||q[j]>1000)return 0;if(t.prob<0||t.prob>1000||t.pan<0||t.pan>1000)return 0;}
 for(int i=0;i<6;i++){const md_extension &e=p->ext[i];const int values[]={e.delay_send,e.reverb_send,e.glide,e.marbles,e.tides,e.tides_rate,e.warps};for(int v:values)if(v<0||v>1000)return 0;if(e.bass_mode<0||e.bass_mode>2||e.pattern<0||e.pattern>3||e.edit_step<0||e.edit_step>15)return 0;for(int v:e.notes)if(v< -1||v>24)return 0;}
 if(p->delay_ms<40||p->delay_ms>350||p->delay_fb<0||p->delay_fb>800||p->reverb_time<0||p->reverb_time>850||p->fx_level<0||p->fx_level>1000)return 0;
 if(p->grain_noise<0||p->grain_noise>1000||p->chord_pattern<0||p->chord_pattern>8||p->chord_bars<1||p->chord_bars>16||p->chord_tracks<0||p->chord_tracks>63)return 0;
 for(const md_sequence &q:p->sequence){if(q.enabled>1||q.length<1||q.length>32||q.edit>=q.length||q.generator>7)return 0;
  for(const md_step &s:q.steps)if((s.note&&(s.note<24||s.note>96))||s.velocity<1||s.velocity>100||md_step_chance(&s)>100||md_step_gate(&s)<1||md_step_gate(&s)>100)return 0;}
 const md_motion &m=p->motion;if(m.compact>1||m.reserved)return 0;
 for(int i=0;i<6;i++)if(m.direction[i]>4||m.feel[i]>100||m.shift[i]>31||m.target[i]>=MD_LFO_TARGET_COUNT||m.wave[i]>3||m.target2[i]>=MD_LFO_TARGET_COUNT||m.wave2[i]>3||m.depth2[i]>1000||m.rate2[i]>1000)return 0;
 const md_journey &j=p->journey;if(j.mode>2||j.length<1||j.length>8||j.reserved[0]||j.reserved[1])return 0;
 for(const auto &a:j.section)if((a.biome!=65535&&a.biome>=MD_BIOMES)||a.bars<1||a.bars>64||a.repeats<1||a.repeats>16||a.keep>63||a.transpose< -24||a.transpose>24||a.reserved[0]||a.reserved[1])return 0;
 for(const auto &l:p->links)if(l.source>5||l.destination>5||l.signal>3||l.target>5||l.depth< -1000||l.depth>1000||l.release>1000)return 0;
 const auto &d=p->drift;if(d.mode>2||d.captured>3||d.position>1000||d.rate>1000||d.depth>1000||(d.mode&&d.captured!=3))return 0;
 for(const auto &s:d.scene){for(auto v:s.macro)if(v>1000)return 0;for(const auto &t:s.track)for(auto v:t)if(v>1000)return 0;}
 const md_kit &k=p->kit;if(k.enabled>1||k.bank>4||k.pad>7||k.length<1||k.length>32||k.step>=k.length||k.generator>4||k.hiss>100||k.reserved)return 0;
 for(const md_pad &a:k.pads)if(a.tune< -12||a.tune>12||a.level>1000||a.cutoff>1000||a.send>1000||a.decay>1000)return 0;
 return 1;
}
int md_load(const md_patch *p){if(playing||!md_validate(p))return 0;runtime_eco=false;soft_biome=-1;journey_active=journey_prepared=0;patch=*p;previous=patch;seqrng=patch.seed;clear_audio();for(int j=0;j<4;j++)smooth[j]=unit(patch.macro[j]);for(int i=0;i<6;i++)reset_voice(i);pending_biome=-1;md_mark_saved();return 1;}
unsigned md_memory_bytes(){return
#ifdef MD_ENGINE_UNION
 sizeof voice_engine_live+
#endif
#ifndef MD_ELEMENTS_BODY_UNION
 sizeof element_body+
#endif
 sizeof cedar_line+sizeof cedar_state+sizeof element_strike+sizeof echo+sizeof room+sizeof room_diffusion+sizeof diffusion_head+sizeof voices_raw+sizeof tape+sizeof grains+sizeof patch+sizeof previous+sizeof journey_home+sizeof journey_origin+80+sizeof rpn_select+sizeof bend_semitones+sizeof bend_cents+sizeof bend_wheel+sizeof macro14+sizeof macro_lsb_seen+sizeof mappings+sizeof learn_last+sizeof learn_picked+sizeof learn_sender+sizeof bend_port+sizeof expression_port+sizeof sustain_port+sizeof link_envelope+sizeof link_mod+sizeof link_readout+sizeof link_phase+32+sizeof kit_voices+sizeof saved_hash+sizeof stats+sizeof itinera+sizeof itinera_undo+sizeof queue+sizeof repeats+512;}
int md_decode(md_patch *out,const void *data,unsigned bytes){
 if(!out||!data)return 0;
 if(bytes==sizeof(md_patch)||bytes==2356||bytes==2288||bytes==2220||bytes==2100){
  uint32_t version;
  memcpy(&version,(const uint8_t*)data+offsetof(md_patch,version),sizeof version);
  if(version<MD_PATCH_VERSION){
   // Every supported older format stored a plain percentage in both bytes.
   for(unsigned i=0;i<MD_TRACKS;i++)for(unsigned j=0;j<MD_STEPS;j++){
    const uint8_t *s=(const uint8_t*)data+offsetof(md_patch,sequence)+
       i*sizeof(md_sequence)+offsetof(md_sequence,steps)+j*sizeof(md_step);
    if((s[offsetof(md_step,chance)]|s[offsetof(md_step,gate)])&128u)return 0;
   }
  }
 }
 if(bytes==sizeof(md_patch)){
  md_patch p;memcpy(&p,data,sizeof p);
  if(p.version==11)p.version=MD_PATCH_VERSION;
  if(!md_validate(&p))return 0;*out=p;return 1;
 }
 struct OldTrack{int32_t values[14];};
 struct OldPatch{uint32_t magic,version,seed;int32_t macro[4],tempo,locked,freeze,grain_size,grain_pos,feedback,quality;OldTrack track[6];};
 struct V2Track{int32_t values[17];};
 struct V2Patch{uint32_t magic,version,seed;int32_t macro[4],tempo,locked,freeze,grain_size,grain_pos,feedback,quality;V2Track track[6];int32_t globals[9];};
 md_patch p={};
 if(bytes==2356){memcpy(&p,data,2356);if(p.version!=10)return 0;creative_defaults(p);if(!md_validate(&p))return 0;*out=p;return 1;}
 if(bytes==2288){memcpy(&p,data,2288);if(p.version!=9)return 0;journey_defaults(p);if(!md_validate(&p))return 0;*out=p;return 1;}
 if(bytes==2220){memcpy(&p,data,2220);if(p.version!=8)return 0;motion_defaults(p);if(!md_validate(&p))return 0;*out=p;return 1;}
 if(bytes==2100){memcpy(&p,data,2100);if(p.version!=7)return 0;kit_defaults(p);if(!md_validate(&p))return 0;*out=p;return 1;}
 if(bytes==1292){memcpy(&p,data,1292);if(p.version!=5&&p.version!=6)return 0;next_defaults(p);if(!md_validate(&p))return 0;*out=p;return 1;}
 if(bytes==652){memcpy(&p,data,652);if(p.version!=4)return 0;extension_defaults(p);if(!md_validate(&p))return 0;*out=p;return 1;}
 if(bytes==620){
  memcpy(&p,data,620);if(p.version!=3)return 0;
  extension_defaults(p);p.biome_id=-1;p.edited_mask=1;for(int i=0;i<6;i++)p.tone_preset[i]=-1;
  if(!md_validate(&p))return 0;*out=p;return 1;
 }
 if(bytes==sizeof(V2Patch)){
  V2Patch old;memcpy(&old,data,sizeof old);if(old.version!=2)return 0;
  memcpy(&p,&old,offsetof(V2Patch,track));memcpy(&p.root,old.globals,sizeof old.globals);
  for(int i=0;i<6;i++)memcpy(&p.track[i],&old.track[i],sizeof(V2Track));
 }else if(bytes==sizeof(OldPatch)){
  OldPatch old;memcpy(&old,data,sizeof old);if(old.version!=1)return 0;
  memcpy(&p,&old,offsetof(OldPatch,track));p.scale=1;p.duck_source=-1;p.duck_release=350;
  for(int i=0;i<6;i++){memcpy(&p.track[i],&old.track[i],sizeof(OldTrack));p.track[i].ratchet=1;}
 }else return 0;
 extension_defaults(p);p.biome_id=-1;p.edited_mask=1;for(int i=0;i<6;i++)p.tone_preset[i]=-1;
 for(int i=0;i<6;i++){p.track[i].shape=p.track[i].decay;p.track[i].cutoff=850;}
 if(!md_validate(&p))return 0;*out=p;return 1;
}
int md_import(const void *data,unsigned bytes){
 if(playing)return 0;return md_decode(&previous,data,bytes)&&md_load(&previous);
}
int md_import_edit(const void *data,unsigned bytes){uint32_t h[2]={saved_hash[0],saved_hash[1]};int ok=md_import(data,bytes);saved_hash[0]=h[0];saved_hash[1]=h[1];return ok;}
const char *md_biome_name(int id){return id<0?"USER BIOME":biome_names+biome_names_at[bound(id,0,MD_BIOMES-1)];}
int md_biome_card_info(int id,md_biome_card *card){
 if(id<0||id>=MD_BIOMES)return 0;
 const auto &b=biome_bank[id];
 *card={b.seed,b.tempo,b.root,b.scale,b.family};
 return 1;
}
int md_biome_step(int current,int delta){
 int pos=-1;
 for(int i=0;i<MD_BIOMES;i++)if(biome_browse_order[i]==current){pos=i;break;}
 if(pos<0)pos=delta<0?MD_BIOMES:-1;
 return biome_browse_order[bound(pos+delta,0,MD_BIOMES-1)];
}
const char *md_biome_family(int id){return id<0?"CUSTOM":biome_families[biome_bank[bound(id,0,MD_BIOMES-1)].family];}
int md_biome_at(unsigned position){return position<MD_BIOMES?biome_browse_order[position]:-1;}
int md_biome_family_index(int id){return id>=0&&id<MD_BIOMES?(int)biome_bank[id].family:-1;}
unsigned md_biome_family_count(void){return sizeof biome_families/sizeof *biome_families;}
const char *md_biome_family_name(unsigned index){return index<md_biome_family_count()?biome_families[index]:"ALL";}
const char *md_tone_name(int id){return id<0?"BIOME VOICE":tone_names+tone_names_at[bound(id,0,MD_TONE_PRESETS-1)];}
int md_tone_model(int id){return tone_bank[bound(id,0,MD_TONE_PRESETS-1)].identity&31u;}
int md_has_envelope(int model){return model==MD_FM||model==MD_VA||model==MD_HARMONIC||model==MD_CHOIR||model==MD_BASS||model==MD_MELLOW||model==MD_SATIN||model==MD_VELVET_KEYS||model==MD_SPECTRAL||model==MD_BREATH||model==MD_CEDAR||model==MD_REED||model==MD_BOWED||model==MD_AERO||model==MD_FELT||model==MD_SILK||model==MD_COPPER||model==MD_VOWEL;}
int md_cutoff_hz(int value){return (int)cutoff_hz(unit(value));}

int md_quantize(int note,int root,int scale){
 static const unsigned masks[]={0xfff,0x5ad,0xab5,0x6ad,0x4a9,0x295,0x555,0x5ab,0x6cd,0x9ad,0x9cd,0x9b5,0x9b3};
 note=bound(note,24,96);root=bound(root,0,11);unsigned mask=masks[bound(scale,0,12)];
 for(int delta=0;delta<12;delta++){
  int low=note-delta,high=note+delta;
  if(low>=24&&(mask&(1u<<((low-root+12)%12))))return low;
  if(high<=96&&(mask&(1u<<((high-root+12)%12))))return high;
 }return note;
}
int md_record_mode(){return record_mode;}int md_record_track(){return record_track;}
const char *md_chord_name(int p){static const char *names[]={"OFF","I - IV - I - V","I - VI - IV - V","I - VII - IV - I","I - III - VI - IV","DUSK / 8 CHORDS","LANTERNS / 8 CHORDS","TIDAL / 8 CHORDS","STILL WATER / 8 CHORDS"};return names[bound(p,0,8)];}
int md_chord_degree(int p,int pos){return journey_degrees[bound(p,0,8)][bound(pos,0,7)];}
unsigned md_fill_status(){return playing?fill_state:0;}
unsigned md_sequence_position(unsigned i,unsigned local){return sequence_position(i>5?5:i,local);}
int md_sequence_playhead(unsigned track,int *hit){
 if(track>=MD_TRACKS||!playing||(patch.kit.enabled&&track>=4)){*hit=0;return -1;}
 *hit=(sequence_hit>>track)&1u;
 return (int)sequence_head[track]-1;
}
int md_phrase_value(const md_step *step,unsigned index,int mode,int amount){
 static const int8_t velocity[]={20,-12,-7,-14,14,-10,5,-16};
 static const int8_t gate[]={12,-24,-10,-25,8,-23,5,-28};
 const int current=mode==MD_EXPRESSION_GATE?md_step_gate(step):step->velocity;
 if(!amount||!current)return current;
 const int delta=(mode==MD_EXPRESSION_GATE?gate:velocity)[index%8]*amount;
 return bound(current+(delta+(delta<0?-50:50))/100,1,100);
}
unsigned md_sequence_clipboard(){return sequence_clip_kind;}
uint32_t md_sequence_notice(){return sequence_notice;}

const char *md_generator_name(int n){static const char *s[]={"SPARSE","ARP","PULSE","DRIFT","PETALS","TIDELINE","STARS","ANSWERS"};return s[bound(n,0,7)];}
const char *md_direction_name(int n){static const char *s[]={"FORWARD","REVERSE","PENDULUM","ORBIT","SEEDED"};return s[bound(n,0,4)];}
const char *md_lfo_target_name(int n){static const char *s[]={"FILTER","COLOUR","PAN","GRAIN SEND","FX SENDS","WARPS","SOFT SWELL","GRAIN SIZE","GRAIN POSITION"};return s[bound(n,0,MD_LFO_TARGET_COUNT-1)];}
const char *md_lfo_wave_name(int n){static const char *s[]={"TRIANGLE","SINE","BLOOM","SIGH"};return s[bound(n,0,3)];}
const char *md_scale_name(int s){static const char *names[]={"CHROMATIC","MINOR","MAJOR","DORIAN","MINOR PENT","MAJOR PENT","WHOLE TONE","PHRYGIAN","UKR DORIAN","HARM MINOR","DBL HARM MIN","HARM MAJOR","DBL HARM MAJ"};return names[bound(s,0,12)];}
const char *md_model_name(int m){static const char *names[]={"PLAITS KICK","PLAITS SNARE","PLAITS HAT","PLAITS FM","PLAITS VA","RINGS MODAL","PLAITS HARMONIC","SINE CHOIR","ROUND BASS","ELEMENTS STRIKE","MELLOW DRIFT","SATIN MORPH","VELVET KEYS","SPECTRAL BLOOM","BREATH TEXTURE","CEDAR STRING","REED ORGAN","SKIN PERCUSSION","WILLOW BOW","AERO FLUTE","FELT MALLET","SILK ENSEMBLE","COPPER BLOOM","VOWEL BLOOM"};return names[bound(m,0,MD_MODELS-1)];}
}

int md_model_first_tone(int model){for(int i=0;i<MD_TONE_PRESETS;i++)if(md_tone_model(i)==model)return i;return 0;}
#ifdef MD_PROFILE
extern "C" int md_prof_read(uint32_t *w,unsigned page){   /* 6 header words + 2 records of 21 words; returns the words */
 w[0]=1;w[1]=prof.blocks;w[2]=prof.over90;w[3]=prof.overhead;w[4]=prof.top_used;w[5]=page;
 unsigned at=6;
 for(unsigned r=0;r<2;r++){const unsigned k=page*2+r;if(k>=4)break;
  const ProfRec &p=prof.top[k];const uint32_t v[21]={p.total,p.ctl,p.tick,p.pre,p.vox,p.misc,p.fx,p.fin,p.voice[0],p.voice[1],p.voice[2],p.voice[3],p.voice[4],p.voice[5],p.c_scene,p.c_misc,p.c_step,p.c_part,p.c_tempo,p.reads,p.models};
  for(unsigned j=0;j<21;j++)w[at++]=k<prof.top_used?v[j]:0;}
 return (int)at;
}
extern "C" int md_prof_avg(uint32_t *w,unsigned mask,int set){   /* SysEx 24: sets the ablation mask, returns the sums since the reset (ticks) */
 if(set)prof_ablate=mask;
 w[0]=prof_ablate;w[1]=(uint32_t)prof.blocks;w[2]=0;unsigned at=3;
 for(unsigned i=0;i<8;i++){w[at++]=(uint32_t)prof.sum[i];w[at++]=(uint32_t)(prof.sum[i]>>32);}
 for(unsigned i=0;i<6;i++){w[at++]=(uint32_t)prof.vsum[i];w[at++]=(uint32_t)(prof.vsum[i]>>32);}
 return (int)at;
}
extern "C" void md_prof_end(void){prof_end();}
extern "C" void md_prof_reset(void){const uint32_t o=prof.overhead,s=prof.scene,ns=prof.next_scene;memset(&prof,0,sizeof prof);prof.overhead=o;prof.scene=s;prof.next_scene=ns;}
#endif
