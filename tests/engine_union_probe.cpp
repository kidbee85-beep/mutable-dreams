/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Host-only regression for the optional classic-engine storage union.
 * Build against both the pinned baseline and the prototype; compare stdout.
 * No hardware backend, firmware installation, or user data is involved. */
#ifdef NDEBUG
#error "Engine union verification needs assertions"
#endif
#include "dsp/dreams.cpp"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <type_traits>
#ifndef ELEMENT_BODY
#define ELEMENT_BODY(i) (element_body[(i)])
#endif
#ifndef VOICE_ENGINE
#define VOICE_ENGINE(v,name) ((v).name)
#endif
static void put(const void *p,size_t n){assert(std::fwrite(p,1,n,stdout)==n);}
// Host ABI oracle: remove the leading polymorphic vptr and rebase pointers
// into this voice's unchanged shared allocator buffer. Every numeric byte,
// including the active engine's complete private histories, is retained.
// This is not a portable wire format or a PI32 runtime test.
template<class T> static void engine_state(const T &e,const Voice &v){
 unsigned char bytes[sizeof(T)];memcpy(bytes,&e,sizeof e);
 if(std::is_polymorphic<T>::value)memset(bytes,0,sizeof(void*));
 const uintptr_t lo=(uintptr_t)v.mem,hi=lo+sizeof v.mem;
 // These are the only two active-engine classes with a pointer field. Their
 // last declared member is float*, and the host ABI puts it in the last word.
 // Assert its exact allocator offset before rebasing; never scan numeric
 // histories for values that merely resemble an address.
 if(std::is_same<T,plaits::VirtualAnalogEngine>::value||std::is_same<T,plaits::AdditiveEngine>::value){
  const size_t offset=sizeof(T)-sizeof(uintptr_t);uintptr_t p;memcpy(&p,bytes+offset,sizeof p);
  const uintptr_t expected=lo+(std::is_same<T,plaits::AdditiveEngine>::value?plaits::kMaxBlockSize*sizeof(float):0u);
  assert(p==expected&&p>=lo&&p<hi);p=p-lo+1u;memcpy(bytes+offset,&p,sizeof p);
 }
 put(bytes,sizeof bytes);
}
static bool classic(int model){return
#ifdef MD_ELEMENTS_BODY_IN_VOICE
 model==MD_ELEMENTS||
#endif
 model==MD_KICK||model==MD_SNARE||model==MD_HAT||model==MD_FM||model==MD_VA||model==MD_RINGS||model==MD_HARMONIC;}
static void state(){
 const uint32_t r[]={rng,seqrng,stem_rng,vint_rng,stmlib::Random::state(),writehead,grainclock};put(r,sizeof r);
 put(&stats,sizeof stats);put(&patch,sizeof patch);put(voice_quiet,sizeof voice_quiet);
 for(unsigned i=0;i<6;i++){
  const Voice &v=voices[i];assert(v.model==patch.track[i].model);
#ifdef MD_ENGINE_UNION
  assert(!!(voice_engine_live&(1u<<i))==classic(v.model));
#endif
  const char *a=(const char*)&v.satin_morph,*b=(const char*)&v.eco_phase+sizeof v.eco_phase;put(a,size_t(b-a));
  put(&v.filter1,sizeof v.filter1);put(&v.filter2,sizeof v.filter2);
  switch(v.model){
  case MD_KICK:engine_state(VOICE_ENGINE(v,kick),v);break;case MD_SNARE:engine_state(VOICE_ENGINE(v,snare),v);break;
  case MD_HAT:engine_state(VOICE_ENGINE(v,hat),v);break;case MD_FM:engine_state(VOICE_ENGINE(v,fm),v);break;
  case MD_VA:engine_state(VOICE_ENGINE(v,va),v);break;case MD_HARMONIC:engine_state(VOICE_ENGINE(v,additive),v);break;
  case MD_RINGS:engine_state(VOICE_ENGINE(v,rings),v);break;
  case MD_ELEMENTS:put(&ELEMENT_BODY(i),sizeof ELEMENT_BODY(i));put(&element_strike[i],sizeof element_strike[i]);break;
  default:break;
  }
 }
 put(cedar_state,sizeof cedar_state);put(cedar_line,sizeof cedar_line);
}
static float lr[512];static int16_t stems[8*521];static unsigned pos;
static void render(unsigned block){
 const unsigned sizes[]={1,7,31,32,63,64,65,127,128,129,255,256};const unsigned n=sizes[block%12];
 for(auto &x:stems)x=0x1234;
 md_render_stems(lr,n,{stems+pos*8,stems,521-pos});
 for(unsigned i=0;i<n*2;i++)assert(std::isfinite(lr[i]));
 put(lr,n*2*sizeof(float));for(unsigned i=0;i<n;i++)put(stems+((pos+i)%521)*8,8*sizeof(int16_t));pos=(pos+n)%521;
 state();assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
}

#ifndef MD_ENGINE_STATE_NO_MAIN
int main(){
 unsigned transitions=0;
 for(unsigned quality=0;quality<2;quality++)for(unsigned before=0;before<MD_MODELS;before++)for(unsigned after=0;after<MD_MODELS;after++){
  md_init();pos=509;stem_rng=0x2545f491u;
  md_patch p=*md_state();p.seed=0xabcdef01u;p.quality=quality;p.journey.mode=0;p.kit.enabled=0;p.fx_level=0;p.macro[1]=p.macro[3]=0;
  for(unsigned i=0;i<6;i++){
   auto &t=p.track[i];t.model=before;t.mute=0;t.level=400+i*30;t.note=48+i;t.attack=70;t.decay=830;t.filter=i%4;t.cutoff=730;t.resonance=410;t.send=0;
   p.ext[i].delay_send=p.ext[i].reverb_send=0;p.ext[i].warps=110;
  }
  assert(md_load(&p));for(unsigned i=0;i<6;i++)assert(md_command(MD_TRIGGER,i,48+i));
  for(unsigned b=0;b<12;b++)render(b);
  // Live model edits end the previous engine lifetime through reset_voice.
  for(unsigned i=0;i<6;i++)assert(md_command(MD_MODEL,i,after));render(12);
  for(unsigned i=0;i<6;i++)assert(md_command(MD_TRIGGER,i,60+i));
  for(unsigned b=13;b<36;b++){
   if(b==22)for(unsigned i=0;i<6;i++)md_midi(0x80|i,60+i,0,0);
   if(b==27)for(unsigned i=0;i<6;i++)assert(md_command(MD_TRIGGER,i,43+i));
   render(b);
  }
  transitions+=6;
 }
 std::fprintf(stderr,"PASS %u live voice takeovers: 24x24 model pairs x2quality x6tracks; complete active-engine bytes, common/filter states, all8stems and RNG; ASLR pointers normalized only\n",transitions);
}

#endif
