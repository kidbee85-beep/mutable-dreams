/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static md_itinera_view view(){md_itinera_view v;md_itinera_read(&v);return v;}
static double render(unsigned n){
 float lr[256];
 double energy=0;
 while(n){
  unsigned frames=n>128?128:n;
  md_render(lr,frames);
  for(unsigned k=0;k<frames*2;k++){
   assert(std::isfinite(lr[k])&&fabsf(lr[k])<1.f);
   energy+=lr[k]*lr[k];
  }
  n-=frames;
 }
 return energy;
}
static void command(int c,int t,int v){assert(md_command(c,t,v));render(1);}
int main(){
 static const unsigned minutes[]={30,60,120,240};
 for(unsigned p=0;p<MD_ITINERA_PRESETS;p++)for(unsigned d=0;d<4;d++){
  md_init();
  command(MD_SCENE,0,169);
  command(MD_CLOCK_SOURCE,0,1);
  const md_patch undo=*md_state();
  command(MD_MUTATE,0,0);
  md_mark_saved();
  const md_patch home=*md_state();
  // The transport counter can roll over during a four-hour trip.
  md_stats *stats=const_cast<md_stats*>(md_status());
  stats->frames=0xfffffff0u;
  const unsigned start=stats->frames,seconds=minutes[d]*60;
  command(MD_ITINERA_START,p,(md_itinera_defaults(p)&~3u)|d);
  auto state=view();
  assert(state.active&&state.preset==p&&state.remaining==seconds&&state.section==1);
  assert(!md_journey_active()&&md_journey_position()==-1&&!md_journey_bar()&&!md_journey_repeat());
  assert(md_playing()&&md_clock_source()==0);
  assert(!md_unsaved()&&!memcmp(&home,md_project_state(),sizeof home));
  assert(!md_command(MD_SCENE,0,0));
  assert(!md_command(MD_ITINERA_START,0,0));
  const int old_biome=state.biome;
  md_midi(0xfa,0,0,0);
  assert(view().biome==old_biome&&md_playing());
  stats->frames=start+(seconds*3/4)*MD_RATE;
  assert(view().chapter==3);
  stats->frames=start+seconds*MD_RATE-20;
  render(128);
  assert(!view().active&&!md_playing()&&md_clock_source()==1);
  assert(!md_unsaved()&&!memcmp(&home,md_state(),sizeof home));
  command(MD_RETURN,0,0);
  assert(!memcmp(md_state(),&undo,sizeof undo));
 }
 md_init();
 const md_patch home=*md_state();
 command(MD_ITINERA_START,5,12+8);
 int last=view().biome;
 unsigned changes=0;
 double energy=0;
 for(unsigned sec=0;sec<260;sec++){
  energy+=render(MD_RATE);
  auto state=view();
  assert(state.active&&state.progress<=1000&&md_validate(md_state()));
  if(state.biome!=last){changes++;last=state.biome;}
 }
 assert(changes>=2&&energy>1&&md_status()->nonfinite==0);
 command(MD_PLAY,0,0);
 assert(!view().active&&!md_playing()&&!memcmp(&home,md_state(),sizeof home));
 command(MD_PLAY,0,1);
 assert(!md_command(MD_ITINERA_START,0,0));
 command(MD_PLAY,0,0);
 // K-2/K-3: each new scene's key is the last one's, a fifth or a fourth away;
 // room, return, echo feedback and tempo start where the last scene was (one
 // second in they have moved by at most the whole difference) and stay valid.
 unsigned bridged=0;
 static const unsigned bridge_presets[]={2u,6u,12u};
 for(unsigned preset:bridge_presets){
  md_init();
  command(MD_ITINERA_START,preset,md_itinera_defaults(preset)&~3u);
  int biome=view().biome,root=md_state()->root;
  md_patch was=*md_state();
  for(unsigned sec=0;sec<600;sec++){
   render(MD_RATE);
   const md_patch *p=md_state();
   assert(md_validate(p)&&p->root>=0&&p->root<12&&p->transpose>=-24&&p->transpose<=24);
   if(view().biome!=biome){
    const int move=((p->root-root)%12+12)%12;
    assert(move==0||move==5||move==7);
    biome=view().biome;root=p->root;bridged++;
    assert(p->tempo>=48&&p->tempo<=100&&p->reverb_time<=820&&p->fx_level<=1000);
   }
   was=*p;
  }
  command(MD_PLAY,0,0);
 }
 assert(bridged>=8);
 printf("Itinera passed %u duration/preset pairs, timer wrap, undo and clock restoration, manual stop, %u transitions in 260 rendered seconds and %u bridged keys.\n",4*MD_ITINERA_PRESETS,changes,bridged);
}
