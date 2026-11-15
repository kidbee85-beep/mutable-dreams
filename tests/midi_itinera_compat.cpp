/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Host-only cross-feature regression, including the three observed MIDI gaps.
 * No firmware source changes; frame jumps do not claim real-time performance.
 */
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static float audio[256];
static int held[16][128], on_count, off_count, checks;
static void drain(){
 uint32_t p;
 while(md_midi_out_read(&p)){
  unsigned cin=p&15, ch=(p>>8)&15, note=(p>>16)&127;
  if(cin==9){assert(!held[ch][note]);held[ch][note]++;on_count++;}
  else if(cin==8){assert(held[ch][note]);held[ch][note]--;off_count++;}
 }
}
static int active_notes(){int n=0;for(auto &ch:held)for(int v:ch)n+=v;return n;}
static void render(unsigned frames=1){
 while(frames){unsigned n=frames>128?128:frames;md_render(audio,n);
  for(unsigned k=0;k<n*2;k++)assert(std::isfinite(audio[k]));
  drain();frames-=n;}
}
static void command(int c,int t,int v){assert(md_command(c,t,v));render();}
static md_itinera_view view(){md_itinera_view v;md_itinera_read(&v);return v;}
static void fresh(bool external=false){
 md_init();uint32_t unused;while(md_midi_out_read(&unused)){}
 memset(held,0,sizeof held);on_count=off_count=0;
 if(external)command(MD_CLOCK_SOURCE,0,1);
 for(unsigned i=0;i<6;i++)md_set_midi_out(i,i+1);
}
static bool find_on(){
 const int before=on_count;
 for(unsigned n=0;on_count==before&&n<MD_RATE*30;n+=128)render(128);
 return on_count>before&&active_notes()>0;
}
static void wait_on(){assert(find_on());}
static void pass(const char *what){printf("PASS %s\n",what);checks++;}
int main(){
 for(unsigned preset=0;preset<MD_ITINERA_PRESETS;preset++){
  fresh();md_patch home=*md_state();
  command(MD_ITINERA_START,preset,md_itinera_defaults(preset)&~3u);
  assert(view().active&&md_playing());const bool observed_notes=find_on();
  const int values[4]={700,350,800,550};
  for(unsigned k=0;k<4;k++){
   command(MD_MACRO,k,values[k]);assert(view().macro[k]==values[k]-500);
  }
  md_patch sounding=*md_state();
  const int before_off=off_count, before_held=active_notes();
  command(MD_PLAY,0,2);
  assert(!view().active&&md_playing()&&!md_journey_active());
  assert(!memcmp(&sounding,md_state(),sizeof sounding));
  assert(off_count==before_off&&active_notes()==before_held);
  command(MD_PLAY,0,0);assert(!md_playing()&&!active_notes());
  assert(on_count==off_count);
  command(MD_RETURN,0,0);assert(!memcmp(&home,md_state(),sizeof home));
  char label[200];snprintf(label,sizeof label,"dream %u: four macros, SEL keep retains sounding patch, Stop/ENV; note continuity %s",preset,observed_notes?"checked on held generated notes":"not exercised (no Note On in first 30 virtual seconds)");
  pass(label);
 }
 fresh();command(MD_ITINERA_START,0,0);wait_on();
 command(MD_PANIC,0,0);assert(!view().active&&!md_playing()&&!active_notes());
 pass("Panic during dream ends generated MIDI notes");
 fresh(true);command(MD_ITINERA_START,0,0);wait_on();
 command(MD_PLAY,0,2);assert(md_clock_source()==1&&md_playing());
 for(unsigned n=0;n<24;n++){md_midi(0xf8,0,0,0);render(MD_RATE/48);}
 assert(md_playing());render(MD_RATE*2+256);
 assert(!md_playing()&&!active_notes()&&on_count==off_count);
 pass("SEL keep restores external clock; received clock continues, missing clock stops with balanced Note Off");

 // Show the exact scope of an already-existing candidate limitation.
 fresh();md_set_midi_macro_enabled(1);command(MD_MACRO,0,0);
 md_midi(0xbf,16,127,0);render();assert(md_state()->macro[0]==1000);
 command(MD_ITINERA_START,0,0);
 md_midi(0xbf,16,0,0);render();
 assert(view().macro[0]==-500);
 pass("channel-16 macro CC controls the active dream");
 for(unsigned port=0;port<3;port++){
  md_midi(0xbf,16,127,port);md_midi(0xbf,48,127,port);render();assert(view().macro[0]==500);
  md_midi(0xbf,16,0,port);md_midi(0xbf,48,0,port);render();assert(view().macro[0]==-500);
 }
 pass("14-bit macro endpoints work on USB, BLE and TRS DSP input ports");
 md_patch protected_patch=*md_state();const int protected_play=md_playing();
 md_midi(0x90,60,100,0);md_midi(0xb0,7,0,0);md_midi(0xb0,101,0,0);md_midi(0xfc,0,0,0);render();
 assert(md_playing()==protected_play&&!memcmp(&protected_patch,md_state(),sizeof protected_patch));
 pass("notes, unrelated CC, RPN and external Stop remain isolated during a dream");
 command(MD_PLAY,0,0);
 fresh();const md_midi_mapping mapping={1,0,1,20,0,0,{0,0}};
 assert(md_set_mapping(0,&mapping));
 command(MD_MACRO,0,0);md_midi(0xb0,20,0,0);md_midi(0xb0,20,127,0);render();
 assert(md_state()->macro[0]==1000);
 command(MD_ITINERA_START,0,0);
 md_midi(0xb0,20,0,0);md_midi(0xb0,20,127,0);render();
 assert(view().macro[0]==500);
 pass("learned macro CC crosses pickup and reaches +500 during a dream");
 command(MD_MACRO,0,500);
 md_midi(0xb0,20,127,0);render();assert(view().macro[0]==0);
 md_midi(0xb0,20,64,0);render();assert(view().macro[0]>=0&&view().macro[0]<=16);
 md_midi(0xb0,20,127,0);render();assert(view().macro[0]==500);
 pass("physical macro change resets learned pickup around neutral 500");
 command(MD_PLAY,0,0);

 fresh();const unsigned begin=md_status()->frames;
 command(MD_ITINERA_START,0,0);wait_on();
 md_stats *stats=const_cast<md_stats*>(md_status());
 stats->frames=begin+30u*60u*MD_RATE;render();
 assert(!view().active&&!md_playing());
 const int lingering=active_notes();unsigned elapsed=0;
 while(active_notes()&&elapsed<MD_RATE*4){render(128);elapsed+=128;}
 assert(!active_notes()&&on_count==off_count);
 assert(!lingering&&elapsed==0);
 pass("natural dream end immediately releases generated MIDI notes");
 printf("RESULT %d compatibility checks; finite host audio only, no hardware/performance claim\n",checks);
 return 0;
}
