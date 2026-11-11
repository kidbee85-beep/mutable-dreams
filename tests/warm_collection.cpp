// Verify the append-only bank through the real shared DSP and patch API.
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
static float lr[MD_QUANTUM*2];
static double render(unsigned frames,FILE *sink=nullptr){
 double energy=0;
 while(frames){unsigned n=frames>MD_QUANTUM?MD_QUANTUM:frames;md_render(lr,n);
  for(unsigned k=0;k<n*2;k++){assert(std::isfinite(lr[k])&&fabsf(lr[k])<1);energy+=lr[k]*lr[k];}
  if(sink)assert(fwrite(lr,8,n,sink)==n);frames-=n;
 }return energy;
}
static void c(int code,int track,int value){assert(md_command(code,track,value));render(MD_QUANTUM);}
int main(int argc,char **argv){
 assert(argc==1||argc==2);
 static_assert(MD_RULE_FIRST_BIOME-MD_WARM_FIRST_BIOME==20&&MD_FELT_FIRST_TONE-MD_WARM_FIRST_TONE==64,"Warm Collection size");
 static_assert(sizeof(md_patch)==2604,"Do not change saved patch layout");
 for(int id=MD_WARM_FIRST_TONE;id<MD_FELT_FIRST_TONE;id++){
  md_init();c(MD_SCENE,0,MD_BLANK_BIOME);
  md_patch p=*md_state();p.scale=0;p.fx_level=0;p.macro[1]=p.macro[3]=0;
  for(int t=0;t<6;t++){p.track[t].mute=t!=0;p.track[t].send=0;}
  p.track[0].level=650;p.track[0].pan=500;
  assert(md_load(&p));
  // Cross the program-511 boundary through real USB-compatible Bank Select.
  md_midi(0xb0,0,id/128,0);md_midi(0xc0,id%128,0,0);render(MD_QUANTUM);
  p=*md_state();assert(p.tone_preset[0]==id&&p.track[0].model==md_tone_model(id));
  assert(md_validate(&p));md_patch q;assert(md_decode(&q,&p,sizeof p)&&!memcmp(&p,&q,sizeof p));
  int base=p.track[0].model==MD_BASS?36:p.track[0].model==MD_SKIN?48:55;
  FILE *f=nullptr;
  if(argc==2){char file[1024];snprintf(file,sizeof file,"%s/tone-%d.f32",argv[1],id);f=fopen(file,"wb");assert(f);}
  for(int j=0;j<3;j++){
   int note=base+(j==0?0:j==1?7:12);
   md_midi(0x90,note,82+8*j,0);assert(render(MD_RATE*3/2,f)>1e-4);
   md_midi(0x80,note,0,0);render(MD_RATE/2,f);
  }
  if(f)fclose(f);c(MD_PANIC,0,0);assert(render(512)==0);
  assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 }
 puts("PASS 64 presets: bank-select, three registers, saved roundtrip, finite audio and exact panic silence.");
 for(int id=MD_WARM_FIRST_BIOME;id<MD_RULE_FIRST_BIOME;id++){
  md_init();c(MD_SCENE,0,id);md_patch p=*md_state(),q;
  assert(p.biome_id==id&&md_validate(&p)&&md_decode(&q,&p,sizeof p)&&!memcmp(&p,&q,sizeof p));
  for(int i=0;i<6;i++)assert(p.tone_preset[i]>=MD_WARM_FIRST_TONE&&p.tone_preset[i]<MD_TONE_PRESETS);
  c(MD_SCENE,0,MD_BLANK_BIOME);q=*md_state();q.journey.mode=1;q.journey.length=1;
  q.journey.section[0]={(uint16_t)id,4,1,0,0,{0,0}};
  assert(md_load(&q));c(MD_PLAY,0,1);assert(render(MD_RATE)>.00001);
  const auto &a=*md_state();assert(a.biome_id==id&&a.fx_level==p.fx_level&&a.reverb_time==p.reverb_time);
  assert(a.delay_ms==p.delay_ms&&a.chord_pattern==p.chord_pattern);
  assert(!memcmp(a.sequence,p.sequence,sizeof p.sequence));
  c(MD_PANIC,0,0);assert(render(512)==0);
  // Sparse parts can enter much later than a 12-second listening excerpt.
  // Prove all six authored lines actually trigger in a longer arrangement.
  md_init();c(MD_SCENE,0,id);c(MD_PLAY,0,1);assert(render(MD_RATE*130)>.01);
  for(int i=0;i<6;i++)if(!md_status()->events[i]){
   fprintf(stderr,"Biome %d, track %d: no event within 130 seconds\n",id,i+1);return 1;
  }
  assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 }
 puts("PASS 20 Biomes: exact 2604-byte roundtrip, Journey FX and phrase parity; every part speaks over 130 seconds.");
}
