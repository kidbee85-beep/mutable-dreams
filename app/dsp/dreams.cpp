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
#ifndef PLAT_HOST
#define POOL __attribute__((section(".pool")))
#else
#define POOL
#endif
namespace {
#include "satin_tables.inc"
constexpr unsigned Q=64, GN=131072, GM=GN-1;
struct Voice {
 plaits::AdditiveEngine additive; stmlib::Svf filter1,filter2;
 plaits::FMEngine fm; plaits::VirtualAnalogEngine va;
 plaits::AnalogBassDrum kick; plaits::AnalogSnareDrum snare;
 plaits::HiHat<plaits::SquareNoise,plaits::SwingVCA,false,true> hat;
 rings::Resonator rings;
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
// Strike resonators use ordinary RAM; the large tape/effect pool stays bounded.
static elements::Resonator element_body[6];
static elements::Exciter element_strike[6];
static int16_t tape[GN] POOL;
// Independent effect storage: never borrow or shorten the granular tape.
static int16_t echo[16384] POOL;
static int16_t room[4][1201];
// Two short input allpasses soften discrete room echoes without more voices.
static float room_diffusion[2][181];static unsigned diffusion_head[2];
static bool fx_active;
static unsigned echo_head,room_head[4];
static float echo_lp,room_lp[4],fx_smooth,delay_smooth=8820,fx_highpass_in,fx_highpass_out;
static float diffuse_room(float x){
 static const unsigned length[2]={113,181};
 for(unsigned j=0;j<2;j++){float delayed=room_diffusion[j][diffusion_head[j]];float y=delayed-.62f*x;room_diffusion[j][diffusion_head[j]]=x+.62f*y;x=y;if(++diffusion_head[j]>=length[j])diffusion_head[j]=0;}
 return x;
}
static void reset_fx(){fx_active=false;memset(room_diffusion,0,sizeof room_diffusion);memset(diffusion_head,0,sizeof diffusion_head);memset(echo,0,sizeof echo);memset(room,0,sizeof room);memset(room_head,0,sizeof room_head);memset(room_lp,0,sizeof room_lp);echo_head=0;echo_lp=fx_smooth=fx_highpass_in=fx_highpass_out=0;delay_smooth=8820;}
static void motion_defaults(md_patch &p){p.version=9;memset(&p.motion,0,sizeof p.motion);}
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
static md_patch patch,previous;
static uint32_t saved_hash[2];static int pending_biome=-1;
// Canonical fingerprint ignores navigation cursors and the descriptive edit flag.
// Compute only on a change request or successful save, never in the sample loop.
static void patch_hash(uint32_t out[2]){
 const uint8_t *b=(const uint8_t*)&patch;uint32_t a=2166136261u,c=5381;
 for(unsigned j=0;j<sizeof patch;j++){
  if(j>=offsetof(md_patch,edited_mask)&&j<offsetof(md_patch,ext))continue;
  bool cursor=false;for(unsigned i=0;i<6;i++)if((j>=offsetof(md_patch,ext)+i*sizeof(md_extension)+offsetof(md_extension,edit_step)&&j<offsetof(md_patch,ext)+i*sizeof(md_extension)+offsetof(md_extension,edit_step)+4)||j==offsetof(md_patch,sequence)+i*sizeof(md_sequence)+offsetof(md_sequence,edit))cursor=true;
  if(cursor||j==offsetof(md_patch,kit)+offsetof(md_kit,pad)||j==offsetof(md_patch,kit)+offsetof(md_kit,step))continue;
  a=(a^b[j])*16777619u;c=(c*33u)^b[j];
 }out[0]=a;out[1]=c;
}

static md_stats stats;
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
static int held[6],sustain[6],midi_bank[6];
static uint8_t midi_route[6],midi_macro_enabled;
static float bend[6],bend_smooth[6],expression[6];
struct Command { int c,t,v; };
static Command queue[64];
static volatile unsigned qread,qwrite;
static inline void fence() { __asm__ volatile("" ::: "memory"); }
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
static float tape_at(unsigned i){i&=GM;return (i<tape_valid?tape[i]:0)*(1.0f/32768.0f);}
static float tape_read(float pos){
 unsigned a=(unsigned)pos;float t=pos-a;
 float xm=tape_at(a-1),x0=tape_at(a),x1=tape_at(a+1),x2=tape_at(a+2);
 // Four-point Hermite interpolation: exact at stored samples, no linear droop.
 float c=.5f*(x1-xm),b=xm-2.5f*x0+2*x1-.5f*x2,d=.5f*(x2-xm)+1.5f*(x0-x1);
 return ((d*t+b)*t+c)*t+x0;
}
static void reset_voice(unsigned i) {
 memset(static_cast<void*>(&voices[i]),0,sizeof(Voice));
 Voice *v=new (&voices[i]) Voice();
 stmlib::BufferAllocator a(v->mem,sizeof v->mem);
 v->fm.Init(&a); v->va.Init(&a);v->kick.Init();v->snare.Init();v->hat.Init();v->rings.Init();
 v->additive.Init(&a);v->additive.Reset();v->filter1.Init();v->filter2.Init();
 v->cut_smooth=patch.track[i].cutoff*.001f;v->res_smooth=patch.track[i].resonance*.001f;v->breath_phase=i*.137f;
 v->colour_tone=unit(patch.track[i].tone);v->colour_harm=unit(patch.track[i].harm);v->colour_shape=unit(patch.track[i].shape);
 v->expression_smooth=1;v->pan_smooth=unit(patch.track[i].pan);held[i]=-1;sustain[i]=0;bend[i]=bend_smooth[i]=0;expression[i]=1;
 v->model=patch.track[i].model;v->note=(float)patch.track[i].note;v->target_note=v->note;v->fade=0;v->velocity=.8f;
 memset(static_cast<void*>(&element_body[i]),0,sizeof(element_body[i]));element_body[i].Init();element_body[i].set_modulation_frequency(.5f/MD_RATE);element_body[i].set_modulation_offset(.25f);
 memset(static_cast<void*>(&element_strike[i]),0,sizeof(element_strike[i]));element_strike[i].Init();
 repeats[i].remaining=0;
}
#include "preset_bank.inc"
#include "lofi_kit.inc"
static void clear_audio() {
 kit_reset();reset_fx();tape_valid=0;writehead=grainclock=0;
 memset(grains,0,sizeof grains);memset(repeats,0,sizeof repeats);
 memset(dcx,0,sizeof dcx);memset(dcy,0,sizeof dcy);feedback_lp=duckenv=grain_noise_smooth=grain_noise_lp=0;memset(seq_gate,0,sizeof seq_gate);record_mode=0;record_note=record_step=-1;
}
static void scene(int id) {
 id=bound(id,0,MD_BIOMES-1);
 memset(&patch,0,sizeof patch);memcpy(&patch,&biome_bank[id].patch,sizeof(FactoryPatch));extension_defaults(patch);
 if(id!=MD_BLANK_BIOME){
  unsigned family=biome_bank[id].family;
  int atlas_arch=id>=MD_V13_BIOMES?(id-MD_V13_BIOMES)%8:-1;
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
 if(id>=MD_V13_BIOMES){
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
 clear_audio();for(int i=0;i<6;i++){reset_voice(i);}for(int j=0;j<4;j++)smooth[j]=unit(patch.macro[j]);
 previous=patch;step=0;untilstep=0;seqrng=patch.seed;pending_biome=-1;md_mark_saved();
}
static void tone_preset(int track,int id) {
 id=bound(id,0,MD_TONE_PRESETS-1);const TonePreset &a=tone_bank[id];md_track &t=patch.track[track];
 t.model=a.model;t.tone=a.tone;t.harm=a.harm;t.decay=a.decay;t.attack=a.attack;t.shape=a.shape;
 t.cutoff=a.cutoff;t.resonance=a.resonance;t.filter=a.filter;t.breathe=a.breathe;
 patch.tone_preset[track]=id;patch.edited_mask=(patch.edited_mask&~(2u<<track))|1u;reset_voice(track);
}
static int32_t *track_field(md_track &t,int c) {
switch(c){case MD_MODEL:return &t.model;case MD_TONE:return &t.tone;case MD_HARM:return &t.harm;case MD_DECAY:return &t.decay;case MD_LEVEL:return &t.level;case MD_SEND:return &t.send;case MD_HITS:return &t.hits;case MD_LENGTH:return &t.length;case MD_ROTATE:return &t.rotate;case MD_PROB:return &t.prob;case MD_NOTE:return &t.note;case MD_PAN:return &t.pan;case MD_MUTE:return &t.mute;default:return &t.speed;}
}
static void fire(unsigned i,float note,float velocity) {
 if(patch.kit.enabled){if(i>=4)return;if(i==3){kit_fire(kit_note((int)note),velocity);stats.events[3]++;stats.last_note[3]=(int)note;return;}}
 seq_gate[i]=0;Voice &v=voices[i];v.trigger=1;v.midi_gate=0;v.target_note=note;if(!patch.ext[i].glide||!v.active)v.note=note;v.velocity=velocity;v.active=1;
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
 int d=journey_degrees[patch.chord_pattern][(step/(16*patch.chord_bars))%(patch.chord_pattern>4?8:4)];
 int offset=intervals[patch.scale][d];return patch.motion.compact&&offset>6?offset-12:offset;
}
static void capture_note(unsigned i,int note,float velocity){
 if(!record_mode||i!=(unsigned)record_track||(record_mode==2&&!playing))return;
 md_sequence &q=patch.sequence[i];q.enabled=1;
 unsigned at=record_mode==1?q.edit:sequence_position(i,(step?step-1:0)/patch.track[i].speed);
 q.steps[at]={(uint8_t)bound(note,24,96),(uint8_t)bound((int)(velocity*100+.5f),1,100),100,85};
 record_note=note;record_step=at;record_start=stats.frames;patch.edited_mask|=1;
 if(record_mode==1)q.edit=(at+1)%q.length;
}
static void capture_off(unsigned i,int note){
 if(record_mode==2&&i==(unsigned)record_track&&record_step>=0&&record_note==note){
  float duration=MD_RATE*60.0f/(patch.tempo*4.0f)*patch.track[i].speed;
  // Convert before multiplication: the integer product overflowed after a
  // sixteen-minute hold, occasionally turning a long note into a tiny gate.
  float percent=(float)(stats.frames-record_start)*100.f/duration;
  patch.sequence[i].steps[record_step].gate=(uint8_t)fmaxf(1.f,fminf(percent,100.f));
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
static void apply(Command e) {
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
  else if(e.c==MD_MUTE) t.mute=!!v;
  else if(e.c==MD_SPEED) t.speed=bound(v,1,8);
  else *p=bound(v,0,1000);
 } else switch(e.c) {
 case MD_MACRO:patch.macro[bound(e.t,0,3)]=bound(v,0,1000);break;
 case MD_TEMPO:patch.tempo=bound(v,30,220);break;
 case MD_PLAY:playing=!!v;if(!playing){record_mode=0;record_note=record_step=-1;}memset(repeats,0,sizeof repeats);if(playing){step=0;untilstep=0;seqrng=patch.seed;}break;
 case MD_LOCK:patch.locked=!!v;break;
 case MD_MUTATE: previous=patch;patch.edited_mask|=1u;patch.seed=random_word(seqrng)|1;for(unsigned j=0;j<6;j++)patch.track[j].rotate=random_word(seqrng)%patch.track[j].length;break;
 case MD_RETURN:record_mode=0;record_note=record_step=-1;patch=previous;seqrng=patch.seed;for(unsigned j=0;j<6;j++){repeats[j].remaining=0;if(voices[j].model!=patch.track[j].model)reset_voice(j);}break;
 case MD_SCENE:v=bound(v,0,MD_BIOMES-1);if(md_unsaved())pending_biome=v;else scene(v);break;
 case MD_SCENE_CONFIRM:if(v==1&&pending_biome>=0)scene(pending_biome);else pending_biome=-1;break;
 case MD_KIT_MODE:if(patch.kit.enabled!=!!v){patch.kit.enabled=!!v;kit_reset();for(int j=3;j<6;j++)reset_voice(j);record_mode=0;}break;
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
 case MD_KIT_GENERATE:previous=patch;kit_pattern(patch.kit,bound(v,0,3));break;
 case MD_KIT_LENGTH:patch.kit.length=bound(v,1,32);patch.kit.step=bound(patch.kit.step,0,patch.kit.length-1);break;
 case MD_KIT_TRIGGER:if(patch.kit.enabled){unsigned pad=bound(v,0,7);kit_fire(pad,.8f);stats.events[3]++;if(record_mode&&record_track==3){unsigned at=record_mode==1?patch.kit.step:((step?step-1:0)/patch.track[3].speed)%patch.kit.length;patch.kit.gates[pad]|=1u<<at;if(record_mode==1)patch.kit.step=(at+1)%patch.kit.length;}}break;
 case MD_TONE_PRESET:tone_preset(i,v);break;
 case MD_GRAIN_NOISE:patch.grain_noise=bound(v,0,1000);break;
 case MD_LFO2_TARGET:patch.motion.target2[i]=bound(v,0,6);break;
 case MD_LFO2_SHAPE:patch.motion.wave2[i]=bound(v,0,3);break;
 case MD_LFO2_DEPTH:patch.motion.depth2[i]=bound(v,0,1000);break;
 case MD_LFO2_RATE:patch.motion.rate2[i]=bound(v,0,1000);break;
 case MD_MIDI_ROUTE:md_set_midi_route(i,bound(v,0,16));break;
 case MD_MIDI_MACRO:md_set_midi_macro_enabled(v);break;
 case MD_SEQ_DIRECTION:patch.motion.direction[i]=bound(v,0,4);break;
 case MD_SEQ_FEEL:patch.motion.feel[i]=bound(v,0,100);break;
 case MD_SEQ_SHIFT:patch.motion.shift[i]=bound(v,0,31);break;
 case MD_LFO_TARGET:patch.motion.target[i]=bound(v,0,6);break;
 case MD_LFO_SHAPE:patch.motion.wave[i]=bound(v,0,3);break;
 case MD_CHORD_COMPACT:patch.motion.compact=!!v;break;
 case MD_SEQ_ENABLE:patch.sequence[i].enabled=!!v;break;
 case MD_SEQ_STEP:patch.sequence[i].edit=bound(v,0,patch.sequence[i].length-1);break;
 case MD_SEQ_NOTE:patch.sequence[i].steps[patch.sequence[i].edit].note=v<24?0:bound(v,24,96);break;
 case MD_SEQ_VELOCITY:patch.sequence[i].steps[patch.sequence[i].edit].velocity=bound(v,1,100);break;
 case MD_SEQ_CHANCE:patch.sequence[i].steps[patch.sequence[i].edit].chance=bound(v,0,100);break;
 case MD_SEQ_GATE:patch.sequence[i].steps[patch.sequence[i].edit].gate=bound(v,1,100);break;
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
 case MD_QUALITY:if(patch.quality!=bound(v,0,1)){patch.quality=bound(v,0,1);for(unsigned j=0;j<6;j++){voices[j].eco_phase=0;voices[j].eco_previous=voices[j].eco_next=0;voices[j].fade=0;}}break;
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
 case MD_CLOCK_SOURCE:clock_source=bound(v,0,2);playing=0;record_mode=0;record_note=record_step=-1;clock_pulse=0;clock_age=clock_last=0;clock_interval=0;break;
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
 case MD_PANIC:playing=0;clear_audio();for(unsigned j=0;j<6;j++)reset_voice(j);break;
 }
}
static void tick() {
 float duration=MD_RATE*60.0f/(patch.tempo*4.0f)*(1+(step%2?-1:1)*unit(patch.swing)*.5f);
 if((step%32)==0) {if(patch.locked)seqrng=patch.seed;else patch.seed=random_word(seqrng)|1;}
 static const int degree[7]={0,2,3,5,7,10,12};
 for(unsigned i=0;i<(patch.kit.enabled?4u:6u);i++) {
  const md_track &t=patch.track[i];
  if(patch.kit.enabled&&i==3){if(step%(unsigned)t.speed==0&&!t.mute){unsigned at=(step/t.speed)%patch.kit.length;for(unsigned pad=0;pad<8;pad++)if((patch.kit.gates[pad]>>at)&1u){if(random_word(seqrng)%1000<(unsigned)t.prob){kit_fire(pad,at%4?.67f:.8f);stats.events[3]++;}}}continue;}
if(step%(unsigned)t.speed)continue;
  unsigned local=step/t.speed;
  int hits=bound(t.hits+(patch.macro[0]-500)*t.length/1500,0,t.length);
  unsigned roll=random_word(seqrng);
  const md_sequence &sq=patch.sequence[i];
  if(sq.enabled){const md_step &st=sq.steps[sequence_position(i,local)];
   if(st.note&&st.velocity&&!t.mute&&roll%100<st.chance&&(roll/100)%1000<(unsigned)t.prob){
    int note=st.note;if(t.model>=MD_FM){if(patch.chord_tracks&(1u<<i))note+=chord_offset();note=md_quantize(note+patch.transpose,patch.root,patch.scale);}
    unsigned human=patch.seed^((i+1)*0x27d4eb2du)^((local%sq.length+1)*0x85ebca6bu);
    float feel=1+((int)(random_word(human)%25)-12)*patch.motion.feel[i]*.0001f;
    fire(i,note,fminf(1.f,st.velocity*.01f*feel));if(md_has_envelope(t.model)){
     // Swing alternates every tick. A slow track spans whole pairs, with only
     // one extra long/short tick for odd speeds; do not multiply the swing.
     float span=MD_RATE*60.0f/(patch.tempo*4.0f)*(t.speed+(t.speed%2?(step%2?-1:1)*unit(patch.swing)*.5f:0));
     voices[i].midi_gate=1;seq_gate[i]=span*st.gate*.01f;
    }
   }continue;
  }
  if((patch.ext[i].bass_mode==2||md_hit(local,hits,t.length,t.rotate))&&roll%1000<(unsigned)t.prob&&!t.mute) {
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
   fire(i,(float)note,velocity);
   repeats[i]={(duration/t.ratchet),duration/t.ratchet,(float)note,velocity,(unsigned)t.ratchet-1};
  }
 }
 stats.step=step++;untilstep+=duration;
}
static void quantum(float *lr,unsigned n) {
 // Conservative admission guard from 0.4.1 FM-1 costs with full-rate tape, LP24 and maximum repeats.
 // Count unmuted slots before playback so the tape cannot prefill four FULL grains
 // immediately before a dense six-voice attack. Existing audible tails also count.
 if(patch.quality){static const unsigned weight[MD_MODELS]={8,13,8,18,10,27,17,11,8,30,10,12,10,12,10};unsigned cost=12;
  // Provisional additional weights, pending physical-device measurement.
  if(fx_active||patch.fx_level)cost+=10;if(patch.kit.enabled)cost+=12;
  for(unsigned j=0;j<(patch.kit.enabled?3u:6u);j++)if(!patch.track[j].mute&&patch.ext[j].warps)cost+=2;
  for(unsigned i=0;i<(patch.kit.enabled?3u:6u);i++)if(voices[i].active||!patch.track[i].mute)cost+=weight[patch.track[i].model];
  if(cost>85){patch.quality=0;stats.quality_reductions++;for(unsigned i=0;i<6;i++){voices[i].eco_phase=0;voices[i].eco_previous=voices[i].eco_next=0;voices[i].fade=0;}}
 }

 // A shared mode budget also applies in ECO. All six tracks remain audible.
 unsigned element_count=0;
 for(unsigned j=0;j<(patch.kit.enabled?3u:6u);j++)if(patch.track[j].model==MD_ELEMENTS&&(voices[j].active||!patch.track[j].mute))element_count++;
 unsigned element_modes=element_count>1?16/element_count:(patch.quality?16:8);
 element_modes=element_modes<2?2:element_modes;stats.element_modes=element_modes;
 const float block_ratio=n>32?n/32.0f:1.0f;
 const float slew25=n>32?.025f*block_ratio*(1-.0125f*(block_ratio-1)):.025f;
 const float slew40=n>32?.04f*block_ratio*(1-.02f*(block_ratio-1)):.04f;
 const float slew35=n>32?.035f*block_ratio*(1-.0175f*(block_ratio-1)):.035f;
 for(unsigned i=0;i<6;i++)stats.track_peak[i]*=n>32?.984064f:.992f;
 for(unsigned j=0;j<4;j++)smooth[j]+=(unit(patch.macro[j])-smooth[j])*slew25;
 bool source_active=playing;float delay_send[Q]={},room_send[Q]={};
 float send[Q]={},out[Q],aux[Q],tmp[Q],exc[Q],duck[Q];memset(lr,0,n*2*sizeof(float));
 float duckdec=1.0f-1.0f/(MD_RATE*(.04f+unit(patch.duck_release)*1.46f));
 for(unsigned k=0;k<n;k++){duck[k]=1-duckenv*unit(patch.duck_amount)*.9f;duckenv*=duckdec;}
 float grainrate=stmlib::SemitonesToRatio((float)patch.grain_pitch)*(patch.reverse?-1:1);
 float fb=unit(patch.feedback);
 for(unsigned i=0;i<(patch.kit.enabled?3u:6u);i++) {
  md_track &t=patch.track[i];Voice &v=voices[i];bool trig=v.trigger;v.trigger=0;
  if(trig) {v.active=1;v.attacking=md_has_envelope(t.model)||t.attack>0; if(!v.attacking)v.env=1;v.excite=1;v.lpg=1;}
  /* Only FM/VA have an observable final envelope. Never cut resonator/drum tails. */
  if(!v.active){stats.skipped_quanta++;continue;}stats.voice_quanta++;source_active=true;
  const md_extension &e=patch.ext[i];
  v.note+=(v.target_note-v.note)*(e.glide?n/(float)(n+MD_RATE*.35f*unit(e.glide)):1.0f);
  v.tide_phase+=n/(MD_RATE*(.5f+unit(e.tides_rate)*19.5f));if(v.tide_phase>=1)v.tide_phase-=1;
  const unsigned route=patch.motion.target[i],route2=patch.motion.target2[i];
  float wave=tidal_wave(v.tide_phase,e.tides?patch.motion.wave[i]:0);
  float tidal=wave*unit(e.tides),tide_gain=1-unit(e.tides)*.5f+tidal*.5f;
  float depth2=unit(patch.motion.depth2[i]),tidal2=0,tide_gain2=1;
  if(depth2){v.tide2_phase+=n/(MD_RATE*(.5f+unit(patch.motion.rate2[i])*19.5f));if(v.tide2_phase>=1)v.tide2_phase-=1;
   tidal2=tidal_wave(v.tide2_phase,patch.motion.wave2[i])*depth2;tide_gain2=1-depth2*.5f+tidal2*.5f;
  }
  v.breath_phase+=n/(MD_RATE*(7.0f+i*2.3f));if(v.breath_phase>=1)v.breath_phase-=1;
  const float breathing=unit(t.breathe)*plaits::Sine(v.breath_phase);
  float tone=fminf(.98f,fmaxf(t.model==MD_HARMONIC?0.0f:.02f,unit(t.tone)+(smooth[2]-.5f)*.65f));
  if(route==1||route2==1)tone=fminf(.98f,fmaxf(0.f,tone+((route==1?tidal:0)+(route2==1?tidal2:0))*.2f));
  float harm=unit(t.harm),shape=unit(t.shape);
  if(t.model==MD_CHOIR||t.model==MD_BASS||t.model==MD_MELLOW||t.model==MD_VELVET_KEYS||t.model==MD_BREATH){
   // These original voices have no internal Mutable parameter interpolator.
   // A 25 ms control slew removes abrupt spectral jumps from live knob/CC edits.
   float c=n/(MD_RATE*.025f+n);
   v.colour_tone+=(tone-v.colour_tone)*c;v.colour_harm+=(harm-v.colour_harm)*c;v.colour_shape+=(shape-v.colour_shape)*c;
   tone=v.colour_tone;harm=v.colour_harm;shape=v.colour_shape;
  }
  bend_smooth[i]+=(bend[i]-bend_smooth[i])*slew40;
  float played_note=v.note+bend_smooth[i];
  float f=440.0f*stmlib::SemitonesToRatio(played_note-69.0f)/MD_RATE;
  plaits::EngineParameters p={trig?1:0,played_note,tone,unit(t.shape),unit(t.harm)*.999f,v.velocity};bool done=false;
  memset(out,0,sizeof out);
  // Only the resonators write/read AUX. Keep its zero baseline there while
  // skipping a redundant tile clear for the other thirteen sound engines.
  // Rings::Resonator::Process overwrites both outputs on every sample.
  // Elements still uses its auxiliary buffer as an accumulator.
  if(t.model==MD_ELEMENTS)memset(aux,0,sizeof aux);
  bool eco=patch.quality==0&&(t.model==MD_FM||t.model==MD_VA||t.model==MD_HARMONIC);
  unsigned engine_n=eco?(n+(v.eco_phase?0:1))/2:n;
  if(eco)p.note+=12.0f; // same concert pitch at 22.05 kHz; linear reconstruction below.
  switch(t.model) {
   case MD_KICK:v.kick.Render(false,trig,v.velocity,f,tone*(.65f+.35f*tone),unit(t.decay),unit(t.harm)*.75f,.12f,out,n);break;
   case MD_SNARE:v.snare.Render(false,trig,v.velocity,f,tone*(.7f+.3f*tone),unit(t.decay),unit(t.harm)*.85f,out,n);break;
   case MD_HAT:v.hat.Render(false,trig,v.velocity,f,tone,unit(t.decay),unit(t.harm),tmp,exc,out,n);break;
   case MD_FM:
    // Keep the original feedback and ratio ranges. Articulate the index with
    // the amplitude contour and touch instead of a static bright sine bell.
    // Breathe adds very slow colour drift, not pitch randomness or detuning.
    p.timbre=(tone+.24f*tone*(1-tone))*fminf(1.0f,(.28f+.72f*v.env)*(.55f+.45f*v.velocity)*(1+.18f*breathing));
    if(engine_n)v.fm.Render(p,out,nullptr,engine_n,&done);break;
   case MD_VA:
    // A wider low-register pulse region instead of thin pulses at low Tone.
    // The upper half still exposes the original sync range.
    if(p.timbre<.5f)p.timbre=.28f+.44f*p.timbre;
    p.morph=fminf(1.0f,fmaxf(0.0f,p.morph+.065f*breathing));
    if(engine_n)v.va.Render(p,out,nullptr,engine_n,&done);break;
   case MD_HARMONIC:
    p.timbre=fminf(1.0f,fmaxf(0.0f,tone*(.55f+.45f*tone)+.018f*breathing));
    p.harmonics*=.85f;
    if(engine_n)v.additive.Render(p,out,nullptr,engine_n,&done);break;
   case MD_ELEMENTS:{
    elements::Exciter &strike=element_strike[i];elements::Resonator &body=element_body[i];
    if(trig)v.strike_gate=MD_RATE*(.03f+.5f*unit(t.shape));
    unsigned strike_flags=(trig?elements::EXCITER_FLAG_RISING_EDGE:0)|(v.strike_gate>0?elements::EXCITER_FLAG_GATE:0);v.strike_gate=fmaxf(0,v.strike_gate-n);
    strike.set_model(t.shape<334?elements::EXCITER_MODEL_MALLET:t.shape<667?elements::EXCITER_MODEL_PLECTRUM:elements::EXCITER_MODEL_PARTICLES);
    // In the Plectrum region, keep its main pulse 1.7-5.2 ms after the primer.
    // The original 14-38 ms mapping was continually restarted by 220 BPM
    // four-ratchet rolls (17 ms spacing), leaving only the tiny primer audible.
    strike.set_parameter(t.shape>=334&&t.shape<667?.05f+.15f*(t.shape-334)/332.0f:.1f+.8f*unit(t.shape));
    strike.set_timbre(.08f+.82f*tone*tone);strike.Process(strike_flags,exc,n);
    body.set_frequency(f);body.set_geometry(fminf(.999f,unit(t.harm)));body.set_brightness(tone*(.6f+.4f*tone));body.set_damping(unit(t.decay)*.8f);body.set_position(.2f+.16f*tone+.07f*breathing);body.set_resolution(element_modes);
    memset(tmp,0,sizeof tmp);body.Process(tmp,exc,out,aux,n);break;}
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
     v.mellow_phase+=f*ratio;v.mellow_phase-=floorf(v.mellow_phase);
     // The shared sine lookup expects a nonnegative phase before wrapping.
     float wave=plaits::Sine(1.0f+v.sub_phase+index*.15915494f*plaits::Sine(v.mellow_phase));
     out[k]=.66f*wave+body*plaits::Sine(v.sub_phase*2);
    }break;}
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
    for(unsigned k=0;k<n;k++){
     v.sub_phase+=f;if(v.sub_phase>=1)v.sub_phase-=1;
     v.mellow_phase+=f2;if(v.mellow_phase>=1)v.mellow_phase-=1;
     float sum=0;float phases[2]={v.sub_phase,v.mellow_phase};
     for(int osc=0;osc<2;osc++){
      float index=phases[osc]*256;int at=(int)index;float frac=index-at;
      const float (*table)[4][257]=t.model==MD_SPECTRAL?spectral_table:satin_table;
      const float *a=table[band][shape],*b=table[band][shape+1];
      float x=a[at]+(a[at+1]-a[at])*frac,y=b[at]+(b[at+1]-b[at])*frac;
      float wave=x+(y-x)*morph;
      if(band&&fade<1){a=table[band-1][shape];b=table[band-1][shape+1];x=a[at]+(a[at+1]-a[at])*frac;y=b[at]+(b[at+1]-b[at])*frac;float low=x+(y-x)*morph;wave=low+(wave-low)*fade;}
      sum+=wave;
     }
     float fundamental=plaits::Sine(v.sub_phase);
     out[k]=.72f*fundamental+(sum*.46f-.72f*fundamental)*v.satin_tone;
    }break;}
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
    v.rings.set_frequency(f);v.rings.set_structure(unit(t.harm)*.999f);v.rings.set_brightness(tone*(.6f+.4f*tone));
    v.rings.set_damping(unit(t.decay)*.9f);v.rings.set_position(.22f+.18f*tone+.07f*breathing);v.rings.set_resolution(patch.quality?24:8);
    // A repeatable damped mallet carries the strike; a little decaying air
    // retains texture. A noise-only strike made equal-velocity notes vary
    // wildly at individual resonant modes. Leave the sounding body intact.
    const float mallet=.08f+.55f*tone;
    for(unsigned k=0;k<n;k++){v.noise_lp+=((trig&&k==0?2.6f:0)-v.noise_lp)*mallet;exc[k]=(v.noise_lp+noise()*v.excite*.085f)*v.velocity;v.excite*=.99f;}
    v.rings.Process(exc,out,aux,n);break;
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
  float track_peak=stats.track_peak[i];
  // Release inside this voice's envelope loop. Splitting the entire rack at
  // each track's gate deadline made six short staggered gates overload the
  // FM-1. No oscillator/filter reconfiguration is needed to end one gate.
  const unsigned release_at=seq_gate[i]>0&&seq_gate[i]<n?(unsigned)ceilf(seq_gate[i]):n;
  // Keep the exact envelope -> filter -> gain order, but let the SVF retain
  // coefficients and state in registers across a tile instead of the mixer.
  for(unsigned k=0;k<n;k++){
   float x=t.model==MD_ELEMENTS?(out[k]+aux[k]*.3f)*3.0f*v.velocity:t.model==MD_RINGS?(out[k]+aux[k])*12.0f:out[k];
   if(melodic){if(k==release_at)v.midi_gate=-1;if(v.midi_gate<0){v.env*=.9975f;v.attacking=0;}else if(v.attacking){v.env+=attackstep;if(v.env>=1){v.env=1;v.attacking=0;}}else if(v.midi_gate==0)v.env*=dec;x*=v.env;}
   out[k]=x;
  }
  if(t.filter){v.filter1.Process<stmlib::FILTER_MODE_LOW_PASS>(out,out,n);if(t.filter==2)v.filter2.Process<stmlib::FILTER_MODE_LOW_PASS>(out,out,n);}
  // Local scalars prevent output-buffer aliasing from forcing repeated Voice
  // loads/stores. Same arithmetic and order; commit once after the tile.
  float mix_amp=v.amp;float mix_fade=v.fade;float mix_lpg=v.lpg;float mix_warp_smooth=v.warp_smooth;float mix_warp_phase=v.warp_phase;float mix_expression_smooth=v.expression_smooth;float mix_pan_smooth=v.pan_smooth;float mix_delay_send_smooth=v.delay_send_smooth;float mix_reverb_send_smooth=v.reverb_send_smooth;
  const bool duck_this_voice=(int)i!=patch.duck_source;
  for(unsigned k=0;k<n;k++) {
   mix_amp+=(target-mix_amp)*.004f;mix_fade+=(1-mix_fade)*.004f;
   float x=out[k];if(t.filter==3)x*=mix_lpg;
   mix_lpg*=dec;
   if(!isfinite(x)) {stats.nonfinite++;x=0;}
   // Individual oscillator outputs remain linear; tape and final mix retain peak protection.
   // Warps-inspired ring blending, not a claim to port the complete module.
   mix_warp_smooth+=(warp_target-mix_warp_smooth)*.002f;
   if(mix_warp_smooth>.000001f){mix_warp_phase+=f;if(mix_warp_phase>=1)mix_warp_phase-=1;x*=1-mix_warp_smooth*.5f+mix_warp_smooth*.5f*plaits::Sine(mix_warp_phase);}
   mix_expression_smooth+=(expr-mix_expression_smooth)*.003f;mix_pan_smooth+=(pan-mix_pan_smooth)*.003f;
   x=x*mix_amp*mix_fade*mix_expression_smooth;if(duck_this_voice)x*=duck[k];
   track_peak=fmaxf(track_peak,fabsf(x));
   lr[2*k]+=x*(1-mix_pan_smooth);lr[2*k+1]+=x*mix_pan_smooth;v.grain_send_smooth+=(sendlevel-v.grain_send_smooth)*.002f;send[k]+=x*v.grain_send_smooth;
   mix_delay_send_smooth+=(ds-mix_delay_send_smooth)*.002f;mix_reverb_send_smooth+=(rs-mix_reverb_send_smooth)*.002f;
   delay_send[k]+=x*mix_delay_send_smooth;room_send[k]+=x*mix_reverb_send_smooth;
  }
  v.amp=mix_amp;v.fade=mix_fade;v.lpg=mix_lpg;v.warp_smooth=mix_warp_smooth;v.warp_phase=mix_warp_phase;v.expression_smooth=mix_expression_smooth;v.pan_smooth=mix_pan_smooth;v.delay_send_smooth=mix_delay_send_smooth;v.reverb_send_smooth=mix_reverb_send_smooth;
  stats.track_peak[i]=track_peak;
  if(melodic&&!v.attacking&&v.env<.000001f)v.active=0;
 }
 if(patch.kit.enabled)kit_mix(lr,send,delay_send,room_send,n);
 const float echo_feedback=unit(patch.delay_fb),room_decay=.45f+.5f*unit(patch.reverb_time),effect_return=unit(patch.fx_level),echo_target=patch.delay_ms*44.1f;
 unsigned maxgrains=patch.quality?4:2;
 for(unsigned k=0;k<n;k++) {
  float wet[2]={};
  if(!grainclock){
   unsigned active=0;int slot=-1;
   for(unsigned j=0;j<4;j++){if(grains[j].life&&grains[j].age<grains[j].life)active++;else if(slot<0)slot=(int)j;}
   grainclock=32; // Defer rather than truncate a grain after a size/quality change.
   if(slot>=0&&active<maxgrains){
    unsigned life=2205+(unsigned)(unit(patch.grain_size)*11025);
    float travel=((patch.freeze?0.0f:1.0f)-grainrate)*life;
    float lo=260+fmaxf(0.0f,-travel),hi=(float)tape_valid-260-fmaxf(0.0f,travel);
    if(hi>lo){
     Grain &g=grains[slot];g.life=life;g.age=0;g.phase=0;g.inc=1.0f/life;
     float delay=lo+unit(patch.grain_pos)*(hi-lo);
     delay=fminf(hi,fmaxf(lo,delay+noise()*250));
     g.pos=(float)writehead-delay;while(g.pos<0)g.pos+=GN;
     g.pan=.5f+noise()*.4f;g.rate=grainrate;g.gain=2.0f/maxgrains;
     grainclock=(life+maxgrains-1)/maxgrains;
    }
   }
  }
  --grainclock;
  // Finish existing FULL grains when entering ECO; never silence a live slot.
  for(unsigned j=0;j<4;j++){Grain &g=grains[j];if(!g.life||g.age>=g.life)continue;
   float distance=(float)writehead-g.pos;if(distance<0)distance+=GN;
   float edge=fminf(1.0f,fmaxf(0.0f,fminf(distance,(float)tape_valid-distance))*(1.0f/256));
   float x=tape_read(g.pos),w=plaits::Sine(g.phase*.5f);w=w*w*g.gain*edge;
   wet[0]+=x*w*(1-g.pan);wet[1]+=x*w*g.pan;
   g.pos+=g.rate;if(g.pos>=GN)g.pos-=GN;if(g.pos<0)g.pos+=GN;
   g.age++;g.phase=g.age*g.inc;
  }
  float w=(wet[0]+wet[1])*.5f;feedback_lp+=(w-feedback_lp)*.65f;
  // Full-rate tape preserves the dry send's bandwidth; 131072 PCM16 samples.
  // Same 256 KiB memory footprint, now 2.97 s at 44.1 kHz rather than 5.94 s.
  grain_noise_smooth+=((source_active?unit(patch.grain_noise):0)-grain_noise_smooth)*.0005f;
  float air=0;if(grain_noise_smooth>.00001f){grain_noise_lp+=(noise()-grain_noise_lp)*.06f;air=grain_noise_lp*grain_noise_smooth*.08f;}
  if(!patch.freeze){float input=clip(send[k]+feedback_lp*fb+air);
   tape[writehead]=int16_t(input*32767);writehead=(writehead+1)&GM;if(tape_valid<GN)tape_valid++;stats.grain_writes++;
  }
  float effect[2]={};
  if(patch.fx_level)fx_active=true;
  if(fx_active){
  // Full-rate independent echo, interpolated time movement, bounded feedback.
  delay_smooth+=(echo_target-delay_smooth)*.00005f;
  float read=echo_head+16384.0f-delay_smooth;unsigned ri=(unsigned)read;float frac=read-ri;
  float delayed=(echo[ri&16383]*(1-frac)+echo[(ri+1)&16383]*frac)/32768.0f;
  echo_lp+=(delayed-echo_lp)*.25f;
  echo[echo_head]=int16_t(clip(delay_send[k]+echo_lp*echo_feedback)*32767);echo_head=(echo_head+1)&16383;
  float room_input=room_send[k];float hp=room_input-fx_highpass_in+.985f*fx_highpass_out;fx_highpass_in=room_input;fx_highpass_out=hp;
  // Schroeder diffusion before the existing four-line room. Unit-magnitude
  // allpasses distribute transients; tails and tape allocation stay unchanged.
  hp=diffuse_room(hp);
  static const unsigned lens[4]={887,1013,1151,1201};float rv[4],sum=0;
  for(unsigned j=0;j<4;j++){rv[j]=room[j][room_head[j]]/32768.0f;room_lp[j]+=(rv[j]-room_lp[j])*.3f;sum+=room_lp[j];}
  for(unsigned j=0;j<4;j++){float value=hp*.3f+(sum*.5f-room_lp[j])*room_decay;room[j][room_head[j]]=int16_t(clip(value)*32767);if(++room_head[j]>=lens[j])room_head[j]=0;}
  fx_smooth+=(effect_return-fx_smooth)*.002f;
  effect[0]=fx_smooth*(delayed*.5f+(rv[0]+rv[2])*.35f);effect[1]=fx_smooth*(delayed*.5f+(rv[1]+rv[3])*.35f);
  }
  for(unsigned c=0;c<2;c++) {
   float x=effect[c]+lr[2*k+c]+wet[c]*smooth[3]*1.7f*duck[k];
   float y=x-dcx[c]+.9997f*dcy[c];dcx[c]=x;dcy[c]=y;
   x=clip(y*1.9f);lr[2*k+c]=x;
   if(fabsf(x)>stats.peak)stats.peak=fabsf(x);
  }
 }
 stats.frames+=n;
}
}
extern "C" {
int md_hit(unsigned s,unsigned h,unsigned l,unsigned r) {if(!l||l>32)return 0;h=h>l?l:h;return (((s+r)%l)*h)%l<h;}
void md_init() {
 voices=reinterpret_cast<Voice*>(voices_raw);memset(voices_raw,0,sizeof voices_raw);memset(tape,0,sizeof tape);memset(grains,0,sizeof grains);
 memset(&stats,0,sizeof stats);memset(smooth,0,sizeof smooth);memset(dcx,0,sizeof dcx);memset(dcy,0,sizeof dcy);
 memset(midi_bank,0,sizeof midi_bank);for(unsigned i=0;i<6;i++)midi_route[i]=i+1;midi_macro_enabled=1;qread=qwrite=writehead=grainclock=tape_valid=0;feedback_lp=0;rng=0x88888;stmlib::Random::Seed(rng);scene(0);playing=0;clock_source=midi_quantize=clock_pulse=0;clock_age=clock_last=0;clock_interval=0;
}
int md_clock_source(void){return clock_source;}
int md_midi_quantize(void){return midi_quantize;}
int md_midi_route(unsigned track){return track<6?midi_route[track]:0;}
int md_midi_macro_enabled(void){return midi_macro_enabled;}
void md_set_midi_route(unsigned track,unsigned channel){if(track>=6||channel>16||midi_route[track]==channel)return;midi_route[track]=channel;held[track]=-1;sustain[track]=0;voices[track].midi_gate=-1;}
void md_set_midi_macro_enabled(int enabled){midi_macro_enabled=!!enabled;}
void md_midi(unsigned st,unsigned a,unsigned b,int port){
 a&=127;b&=127;
 if(st>=0xf8){
  if(!clock_source||port!=(clock_source==1?0:2))return;
  if(st==0xfa){step=0;seqrng=patch.seed;clock_pulse=0;playing=1;clock_age=0;clock_last=stats.frames;clock_interval=0;memset(repeats,0,sizeof repeats);}
  else if(st==0xfb){playing=1;clock_age=0;}
  else if(st==0xfc){playing=0;record_mode=0;record_note=record_step=-1;memset(repeats,0,sizeof repeats);}
  else if(st==0xf8&&playing){unsigned delta=stats.frames-clock_last;clock_last=stats.frames;
   if(delta>=MD_RATE*60/(220*24)&&delta<=MD_RATE*60/(30*24)){clock_interval=clock_interval?clock_interval*.85f+delta*.15f:delta;patch.tempo=bound((int)(MD_RATE*60/(24*clock_interval)+.5f),30,220);}
   clock_age=0;if(clock_pulse==0){untilstep=0;tick();}clock_pulse=(clock_pulse+1)%6;}
  return;
 }
 unsigned channel=(st&15)+1,type=st&240;
 if(channel==16&&midi_macro_enabled&&type==0xb0&&a>=16&&a<=19){apply({MD_MACRO,(int)a-16,(int)b*1000/127});return;}
 for(unsigned i=0;i<(patch.kit.enabled?4u:6u);i++){
 if(midi_route[i]!=channel)continue;
 Voice &v=voices[i];
 if(patch.kit.enabled&&i==3){if(type==0x90&&b){kit_fire(kit_note(a),b/127.f);stats.events[3]++;if(record_mode&&record_track==3){unsigned at=record_mode==1?patch.kit.step:((step?step-1:0)/patch.track[3].speed)%patch.kit.length;patch.kit.gates[kit_note(a)]|=1u<<at;if(record_mode==1)patch.kit.step=(at+1)%patch.kit.length;}}else if(type==0xb0&&(a==120||a==123))kit_reset();if(type!=0xb0)continue;}
 if(type==0x90&&b){int note=midi_quantize?md_quantize(a,patch.root,patch.scale):bound(a,24,96);capture_note(i,bound(a,24,96),b/127.0f);fire(i,note,b/127.0f);held[i]=a;v.midi_gate=md_has_envelope(v.model)?1:0;}
 else if(type==0x80||(type==0x90&&!b)){capture_off(i,bound(a,24,96));if(held[i]==(int)a){held[i]=-1;if(!sustain[i])v.midi_gate=-1;}}
 else if(type==0xc0){unsigned id=midi_bank[i]*128+a;if(id<MD_TONE_PRESETS)tone_preset(i,id);}
 else if(type==0xe0)bend[i]=((int)(a|(b<<7))-8192)*(2.0f/8192);
 else if(type==0xb0){int value=b*1000/127;
  if(a==0)midi_bank[i]=b;
  else if(a==32){} /* Only LSB zero supported; reserved, never a send. */
  else if(a==121){expression[i]=1;bend[i]=0;sustain[i]=0;if(held[i]<0)v.midi_gate=-1;}
  else if(a==64){sustain[i]=b>=64;if(!sustain[i]&&held[i]<0)v.midi_gate=-1;}
  else if(a==120)reset_voice(i);
  else if(a==123){held[i]=-1;sustain[i]=0;v.midi_gate=-1;}
  else if(a==11)expression[i]=b/127.0f;
  else {int c=a==7?MD_LEVEL:a==10?MD_PAN:a==74?MD_CUTOFF:a==71?MD_RESONANCE:a==91?MD_REVERB_SEND:a==12?MD_DELAY_SEND:a==13?MD_SEND:a==5?MD_GLIDE:-1;if(c>=0)apply({c,(int)i,value});}
 }
 }
}
unsigned md_command_space(void){return 64-(qwrite-qread);}
int md_command(int c,int t,int v) {if(c<0||c>=MD_COMMAND_COUNT||(c>=99&&c<120))return 0;unsigned w=qwrite;if(w-qread>=64){stats.dropped_commands++;return 0;}queue[w&63]={c,t,v};fence();qwrite=w+1;return 1;}
void md_render(float *lr,unsigned n) {
 while(n){while(qread!=qwrite){fence();Command e=queue[qread&63];apply(e);fence();qread++;}
  unsigned z=n<Q?n:Q;
  if(playing){
   if(!clock_source&&untilstep<=0)tick();
   for(unsigned i=0;i<6;i++)if(repeats[i].remaining&&repeats[i].left<=0){
    if(!patch.track[i].mute)fire(i,repeats[i].note,repeats[i].velocity);
    repeats[i].remaining--;repeats[i].left+=repeats[i].spacing;
   }
   unsigned to=(!clock_source&&untilstep>0)?(unsigned)ceilf(untilstep):0;if(to&&z>to)z=to;
   for(unsigned i=0;i<6;i++)if(repeats[i].remaining){to=(unsigned)ceilf(repeats[i].left);if(to&&z>to)z=to;}
  }
  if(playing){untilstep-=z;for(unsigned i=0;i<6;i++)if(repeats[i].remaining)repeats[i].left-=z;}
  if(clock_source&&playing){clock_age+=z;if(clock_age>MD_RATE*2){playing=0;record_mode=0;record_note=record_step=-1;memset(repeats,0,sizeof repeats);}}
  quantum(lr,z);for(unsigned i=0;i<6;i++)if(seq_gate[i]>0){seq_gate[i]-=z;if(seq_gate[i]<=0)voices[i].midi_gate=-1;}lr+=z*2;n-=z;
 }
}
int md_unsaved(){uint32_t h[2];patch_hash(h);return h[0]!=saved_hash[0]||h[1]!=saved_hash[1];}
void md_mark_saved(){patch_hash(saved_hash);}
int md_pending_biome(){return pending_biome;}
int md_active_tracks(){return patch.kit.enabled?4:6;}
const char *md_kit_name(int n){static const char *s[]={"DUST ROOM","PAPER MACHINES","SOFT STATIC","FOUND OBJECTS","VELOUR TAPE"};return s[bound(n,0,4)];}
const char *md_pad_name(int n){static const char *s[]={"PILLOW KICK","BRUSH SNARE","DUST HAT","WOOD KNOCK","TINY CLICK","TAPE HISS","CRACKLE","AIR TEXTURE"};return s[bound(n,0,7)];}
const md_patch *md_state(){return &patch;}const md_stats *md_status(){return &stats;}int md_playing(){return playing;}
int md_validate(const md_patch *p) {
 if(!p||p->magic!=0x4D445231||p->version!=9||!p->seed||p->tempo<30||p->tempo>220)return 0;
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
  for(const md_step &s:q.steps)if((s.note&&(s.note<24||s.note>96))||s.velocity<1||s.velocity>100||s.chance>100||s.gate<1||s.gate>100)return 0;}
 const md_motion &m=p->motion;if(m.compact>1||m.reserved)return 0;
 for(int i=0;i<6;i++)if(m.direction[i]>4||m.feel[i]>100||m.shift[i]>31||m.target[i]>6||m.wave[i]>3||m.target2[i]>6||m.wave2[i]>3||m.depth2[i]>1000||m.rate2[i]>1000)return 0;
 const md_kit &k=p->kit;if(k.enabled>1||k.bank>4||k.pad>7||k.length<1||k.length>32||k.step>=k.length||k.generator>3||k.hiss>100||k.reserved)return 0;
 for(const md_pad &a:k.pads)if(a.tune< -12||a.tune>12||a.level>1000||a.cutoff>1000||a.send>1000||a.decay>1000)return 0;
 return 1;
}
int md_load(const md_patch *p){if(playing||!md_validate(p))return 0;patch=*p;previous=patch;seqrng=patch.seed;clear_audio();for(int j=0;j<4;j++)smooth[j]=unit(patch.macro[j]);for(int i=0;i<6;i++)reset_voice(i);pending_biome=-1;md_mark_saved();return 1;}
unsigned md_memory_bytes(){return sizeof element_body+sizeof element_strike+sizeof echo+sizeof room+sizeof room_diffusion+sizeof diffusion_head+sizeof voices_raw+sizeof tape+sizeof grains+sizeof patch+sizeof previous+sizeof kit_voices+sizeof saved_hash+sizeof stats+sizeof queue+sizeof repeats+512;}
int md_decode(md_patch *out,const void *data,unsigned bytes){
 if(!out||!data)return 0;
 if(bytes==sizeof(md_patch)){md_patch p;memcpy(&p,data,sizeof p);if(!md_validate(&p))return 0;*out=p;return 1;}
 struct OldTrack{int32_t values[14];};
 struct OldPatch{uint32_t magic,version,seed;int32_t macro[4],tempo,locked,freeze,grain_size,grain_pos,feedback,quality;OldTrack track[6];};
 struct V2Track{int32_t values[17];};
 struct V2Patch{uint32_t magic,version,seed;int32_t macro[4],tempo,locked,freeze,grain_size,grain_pos,feedback,quality;V2Track track[6];int32_t globals[9];};
 md_patch p={};
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
 if(playing)return 0;md_patch decoded;return md_decode(&decoded,data,bytes)&&md_load(&decoded);
}
int md_import_edit(const void *data,unsigned bytes){uint32_t h[2]={saved_hash[0],saved_hash[1]};int ok=md_import(data,bytes);saved_hash[0]=h[0];saved_hash[1]=h[1];return ok;}
const char *md_biome_name(int id){return id<0?"USER BIOME":biome_bank[bound(id,0,MD_BIOMES-1)].name;}
int md_biome_step(int current,int delta){
 int pos=-1;
 for(int i=0;i<MD_BIOMES;i++)if(biome_browse_order[i]==current){pos=i;break;}
 if(pos<0)pos=delta<0?MD_BIOMES:-1;
 return biome_browse_order[bound(pos+delta,0,MD_BIOMES-1)];
}
const char *md_biome_family(int id){return id<0?"CUSTOM":biome_families[biome_bank[bound(id,0,MD_BIOMES-1)].family];}
const char *md_tone_name(int id){return id<0?"BIOME VOICE":tone_bank[bound(id,0,MD_TONE_PRESETS-1)].name;}
int md_tone_model(int id){return tone_bank[bound(id,0,MD_TONE_PRESETS-1)].model;}
int md_has_envelope(int model){return model==MD_FM||model==MD_VA||model==MD_HARMONIC||model==MD_CHOIR||model==MD_BASS||model==MD_MELLOW||model==MD_SATIN||model==MD_VELVET_KEYS||model==MD_SPECTRAL||model==MD_BREATH;}
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
unsigned md_sequence_position(unsigned i,unsigned local){return sequence_position(i>5?5:i,local);}
const char *md_generator_name(int n){static const char *s[]={"SPARSE","ARP","PULSE","DRIFT","PETALS","TIDELINE","STARS","ANSWERS"};return s[bound(n,0,7)];}
const char *md_direction_name(int n){static const char *s[]={"FORWARD","REVERSE","PENDULUM","ORBIT","SEEDED"};return s[bound(n,0,4)];}
const char *md_lfo_target_name(int n){static const char *s[]={"FILTER","COLOUR","PAN","GRAIN SEND","FX SENDS","WARPS","SOFT SWELL"};return s[bound(n,0,6)];}
const char *md_lfo_wave_name(int n){static const char *s[]={"TRIANGLE","SINE","BLOOM","SIGH"};return s[bound(n,0,3)];}
const char *md_scale_name(int s){static const char *names[]={"CHROMATIC","MINOR","MAJOR","DORIAN","MINOR PENT","MAJOR PENT","WHOLE TONE","PHRYGIAN","UKR DORIAN","HARM MINOR","DBL HARM MIN","HARM MAJOR","DBL HARM MAJ"};return names[bound(s,0,12)];}
const char *md_model_name(int m){static const char *names[]={"PLAITS KICK","PLAITS SNARE","PLAITS HAT","PLAITS FM","PLAITS VA","RINGS MODAL","PLAITS HARMONIC","SINE CHOIR","ROUND BASS","ELEMENTS STRIKE","MELLOW DRIFT","SATIN MORPH","VELVET KEYS","SPECTRAL BLOOM","BREATH TEXTURE"};return names[bound(m,0,MD_MODELS-1)];}
}
