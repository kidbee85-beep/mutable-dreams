// Deterministic boundary/mutation soak of the real DSP. Device load is measured
// separately; sanitizers catch memory/undefined behaviour in every model here.
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
static float audio[256];static unsigned random_state=0x25001115u;
static unsigned rnd(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void run(unsigned n){while(n){unsigned q=n>128?128:n;md_render(audio,q);for(unsigned k=0;k<q*2;k++)assert(std::isfinite(audio[k])&&fabsf(audio[k])<1);n-=q;}}
int main(){
 for(int model=0;model<MD_MODELS;model++){
  md_init();md_patch p=*md_state();p.tempo=220;p.macro[3]=1000;p.grain_size=1000;p.feedback=850;p.quality=0;p.fx_level=700;
  for(int i=0;i<6;i++){auto &t=p.track[i];t.model=(model+i)%MD_MODELS;t.mute=0;t.hits=t.length=16;t.ratchet=4;t.prob=1000;t.level=600;t.send=700;t.filter=2;t.cutoff=1000;t.resonance=1000;t.decay=1000;t.note=24+i*12;}
  assert(md_load(&p));assert(md_command(MD_PLAY,0,1));run(128);
  for(int k=0;k<1200;k++){
   unsigned track=rnd()%6,port=rnd()%3,ch=track;
   switch(k%10){
   case 0:md_midi(0x90|ch,24+rnd()%73,1+rnd()%127,port);break;
   case 1:md_midi(0x80|ch,24+rnd()%73,0,port);break;
   case 2:md_midi(0xe0|ch,rnd()%128,rnd()%128,port);break;
   case 3:{const unsigned cc[]={64,11,121,123,101,100,6,38,74,71,91,13};md_midi(0xb0|ch,cc[rnd()%12],rnd()%128,port);break;}
   case 4:md_midi_lost(port);break;
   case 5:assert(md_command(MD_GRAIN_PITCH,0,(int)(rnd()%49)-24));assert(md_command(MD_REVERSE,0,rnd()%2));break;
   case 6:assert(md_command(MD_CUTOFF,track,rnd()%1001));assert(md_command(MD_RESONANCE,track,rnd()%1001));break;
   case 7:assert(md_command(MD_GRAIN_SIZE,0,rnd()%1001));assert(md_command(MD_GRAIN_POS,0,rnd()%1001));break;
   case 8:assert(md_command(MD_MUTE,track,rnd()%2));assert(md_command(MD_MACRO,rnd()%4,rnd()%1001));break;
   case 9:md_midi(0xc0|ch,rnd()%128,0,port);break;
   }
   run(256);
  }
  assert(md_command(MD_PLAY,0,0));run(1);assert(md_validate(md_project_state()));
  assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
  md_patch saved=*md_project_state(),decoded;assert(md_decode(&decoded,&saved,sizeof saved));
  assert(md_command(MD_PANIC,0,0));run(128);for(float x:audio)assert(x==0);
 }
 printf("PASS %d deterministic mutation steps: all %d models, six slots, three MIDI transports, controller loss, program changes, grain extremes, filters, clocked repeats and exact Panic silence (ASan/UBSan).\n",1200*MD_MODELS,MD_MODELS);
}
