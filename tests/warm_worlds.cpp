#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
static float lr[256];
static double run(unsigned n,FILE *f=0){double e=0;while(n){unsigned z=n>128?128:n;md_render(lr,z);for(unsigned k=0;k<z*2;k++){assert(std::isfinite(lr[k])&&fabsf(lr[k])<1);e+=lr[k]*lr[k];}if(f)assert(fwrite(lr,8,z,f)==z);n-=z;}return e;}
static void command(int c,int t,int v){assert(md_command(c,t,v));run(1);}
static md_patch dry(unsigned model){md_init();command(MD_SCENE,0,MD_BLANK_BIOME);auto p=*md_state();p.fx_level=0;p.macro[1]=p.macro[3]=0;p.quality=0;for(int i=0;i<6;i++){p.track[i].mute=i!=0;p.track[i].send=0;memset(&p.ext[i],0,sizeof p.ext[i]);}auto &t=p.track[0];t.model=model;t.level=700;t.attack=0;t.decay=850;t.tone=600;t.harm=400;t.shape=450;t.filter=0;t.pan=500;return p;}
int main(int argc,char **argv){
 static_assert(sizeof(md_patch)==2604&&MD_MODELS>=20&&MD_WARM_FIRST_BIOME==213&&MD_WARM_FIRST_TONE==452,"Additive worlds; stable save schema");
 for(int model:{MD_BOWED,MD_AERO,MD_FELT})for(int note:{24,36,48,60,72,84,96}){
  auto p=dry(model);assert(md_load(&p));md_midi(0x90,note,100,0);assert(run(MD_RATE*2)>.01);md_midi(0x80,note,0,0);run(MD_RATE*2);assert(run(MD_RATE/4)<1e-7);command(MD_PANIC,0,0);assert(run(512)==0);
 }
 puts("PASS Willow Bow / Aero Flute / Felt Mallet: seven registers, audible finite stereo, release and exact panic silence.");
 for(int model=0;model<MD_MODELS;model++){
  int first=md_model_first_tone(model);auto p=dry(model);assert(md_load(&p));command(MD_TONE_PRESET,0,first);assert(md_state()->track[0].model==model);
 }
 assert(md_model_first_tone(MD_SKIN)==404&&md_model_first_tone(MD_BOWED)==420&&md_model_first_tone(MD_AERO)==428);
 puts("PASS first sound-preset lookup uses actual model IDs, including Skin (previous hardcoded fallback was wrong).");
 for(int id=420;id<452;id++){
  auto p=dry(18);assert(md_load(&p));command(MD_TONE_PRESET,0,id);assert(md_validate(md_state()));md_midi(0x90,60,100,0);assert(run(MD_RATE*2)>.0001);command(MD_PANIC,0,0);assert(run(512)==0);
 }
 puts("PASS all 32 appended warm tones, valid controls and finite audible output.");
 for(int id=0;id<MD_BIOMES;id++)assert(md_biome_family(id)&&*md_biome_family(id));
 for(int id=197;id<213;id++){
  md_init();command(MD_SCENE,0,id);assert(md_validate(md_state()));auto p=*md_state();md_patch restored;assert(md_decode(&restored,&p,sizeof p)&&!memcmp(&restored,&p,sizeof p));
  FILE *f=0;if(argc>1){char path[1024];snprintf(path,sizeof path,"%s/biome-%d.f32",argv[1],id);f=fopen(path,"wb");assert(f);}
  command(MD_PLAY,0,1);assert(run(MD_RATE*24,f)>.01);if(f)fclose(f);assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 }
 puts("PASS 16 authored worlds, direct recall, exact saved roundtrip, audible stereo and finite long tails.");
 for(int id:{189,190,195,197,200,208,212}){
  md_init();command(MD_SCENE,0,id);auto reference=*md_state();command(MD_SCENE,0,64);auto p=*md_state();p.journey.mode=1;p.journey.length=1;p.journey.section[0]={(uint16_t)id,4,1,0,0,{0,0}};assert(md_load(&p));command(MD_PLAY,0,1);
  assert(md_state()->fx_level==reference.fx_level&&md_state()->reverb_time==reference.reverb_time&&md_state()->delay_ms==reference.delay_ms&&md_state()->grain_noise==reference.grain_noise);
  for(int i=0;i<6;i++)assert(md_state()->ext[i].reverb_send==reference.ext[i].reverb_send&&md_state()->ext[i].delay_send==reference.ext[i].delay_send);
 }
 puts("PASS Journey and direct recall use the same authored factory effects, including hand-percussion worlds.");
 for(int model:{MD_BOWED,MD_AERO,MD_FELT}){
  auto p=dry(model);for(int i=0;i<6;i++){p.track[i]=p.track[0];p.track[i].mute=0;p.track[i].hits=p.track[i].length=16;p.track[i].ratchet=4;p.track[i].note=36+i*7;p.track[i].send=700;}p.grain_size=1000;p.feedback=850;p.macro[3]=1000;p.fx_level=700;p.quality=1;assert(md_load(&p));command(MD_PLAY,0,1);assert(run(MD_RATE*8)>.01);assert(md_state()->quality==1&&!md_status()->quality_reductions);assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 }
 puts("PASS six Bow / six Aero / six Felt voices retain FULL with dense repeats, long grains and shared effects (offline; device CPU separate).");
}
