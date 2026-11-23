#ifdef NDEBUG
#error "CPU2 host verification requires active assertions; NDEBUG is unsupported"
#endif
#include <cstdlib>
#ifndef MD_CPU2_CHECK
#define MD_CPU2_CHECK(condition) do { if(!(condition)) std::abort(); } while(0)
#endif

/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#include "dsp/dreams.cpp"
#include <cstdio>
#include <cassert>
#include <cfenv>
#include <cmath>
#include <cstdlib>
static void put(const void *p,size_t n){MD_CPU2_CHECK(std::fwrite(p,1,n,stdout)==n);}
static void state_trace(){
 const uint32_t randoms[]={rng,seqrng,stem_rng,vint_rng,stmlib::Random::state(),writehead,grainclock};put(randoms,sizeof randoms);
 put(&stats,sizeof stats);put(voice_quiet,sizeof voice_quiet);put(&patch,sizeof patch);
 for(unsigned i=0;i<6;i++){
  // The numeric tail contains no polymorphic engine pointers/ASLR addresses.
  const Voice &v=voices[i];const char *a=reinterpret_cast<const char*>(&v.satin_morph),*b=reinterpret_cast<const char*>(&v.eco_phase)+sizeof v.eco_phase;put(a,size_t(b-a));put(&v.filter1,sizeof v.filter1);put(&v.filter2,sizeof v.filter2);
 }
 put(link_mod,sizeof link_mod);put(link_envelope,sizeof link_envelope);put(vint_off,sizeof vint_off);put(vint_target,sizeof vint_target);put(vint_wait,sizeof vint_wait);
}
static void custom(unsigned mode){
 auto p=*md_state();p.kit.enabled=0;p.journey.mode=0;
 const int models[]={MD_ELEMENTS,MD_RINGS,MD_FM,MD_VA,MD_HARMONIC,MD_MELLOW};
 for(int i=0;i<6;i++){auto &t=p.track[i];t.model=models[i];t.filter=1+i%3;t.mute=mode==1&&i%2;t.level=400+i*60;t.attack=250;t.decay=650;p.ext[i].warps=250;p.ext[i].delay_send=300;p.ext[i].reverb_send=450;}
 p.links[0]={0,1,1,0,420,300};p.links[1]={2,0,4,2,230,550};
 p.drift.mode=2;p.drift.captured=3;p.drift.depth=600;p.drift.rate=420;p.drift.position=200;
 for(int e=0;e<2;e++){
  for(int j=0;j<4;j++)p.drift.scene[e].macro[j]=p.macro[j];
  for(int i=0;i<6;i++){auto &t=p.track[i];uint16_t row[8]={(uint16_t)bound(t.tone+e*20,0,1000),(uint16_t)t.cutoff,(uint16_t)t.decay,(uint16_t)t.level,(uint16_t)t.pan,(uint16_t)t.send,(uint16_t)p.ext[i].delay_send,(uint16_t)p.ext[i].reverb_send};memcpy(p.drift.scene[e].track[i],row,sizeof row);}
 }
 MD_CPU2_CHECK(md_load(&p));
}
int main(){
 if(const char *r=std::getenv("MD_ROUND")){if(*r=='u')MD_CPU2_CHECK(std::fesetround(FE_UPWARD)==0);if(*r=='d')MD_CPU2_CHECK(std::fesetround(FE_DOWNWARD)==0);if(*r=='z')MD_CPU2_CHECK(std::fesetround(FE_TOWARDZERO)==0);}
 const unsigned scene_ids[]={111,0,37,299,282,264,7,220,111,111};
 const unsigned sizes[]={1,7,31,32,63,64,65,127,128,129,255,256};
 constexpr unsigned frames=521;int16_t ring[frames*8];unsigned pos=509;float lr[512];
 for(unsigned c=0;c<10;c++){
  md_init();md_set_air(3);md_set_chorus(1);md_set_vintage(2);md_set_width(3);stem_rng=0x2545f491u^c;
  MD_CPU2_CHECK(md_command(MD_SCENE,0,scene_ids[c]));md_render(lr,1);if(md_pending_biome()>=0){MD_CPU2_CHECK(md_command(MD_SCENE_CONFIRM,0,1));md_render(lr,1);}if(c>=8)custom(c-8);
  MD_CPU2_CHECK(md_command(MD_PLAY,0,1));
  for(unsigned block=0;block<720;block++){
   if(block%41==0)for(int t=0;t<6;t++)md_midi(0x90|t,48+t*3+(block%5),80+t*4,0);
   if(block%41==7)for(int t=0;t<6;t++)md_midi(0x80|t,48+t*3+((block-7)%5),0,0);
   if(block%67==0){journey_prepared=1;journey_fade_mask=0x15;journey_fade_step=.0001f;}
   if(block%67==19){journey_prepared=0;journey_fade_mask=0;}
   const unsigned n=sizes[block%12];for(auto &v:ring)v=0x1234;
   md_render_stems(lr,n,{ring+pos*8,ring,frames-pos});for(unsigned k=0;k<2*n;k++)MD_CPU2_CHECK(std::isfinite(lr[k]));
   put(lr,n*2*sizeof(float));for(unsigned k=0;k<n;k++)put(ring+((pos+k)%frames)*8,16);pos=(pos+n)%frames;state_trace();
  }
 }
 std::fprintf(stderr,"PASS10patches,720fragmentedcalls each,all8stems,noise/stemRNG,numericVoice/filter/patch/stats/link/vintage traces,activeDrift/Links,denseMIDI,fade masks\\n");
}
