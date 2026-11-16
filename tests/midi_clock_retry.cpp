/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * A local PLAY retry must get the existing two-second external-clock wait.
 * The selected clock remains authoritative; missing clock never falls back.
 */
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static float audio[256];
static unsigned ons,offs;
static int held[16][128];
static void render(unsigned frames=1){
 while(frames){
  unsigned n=frames>128?128:frames;md_render(audio,n);
  for(unsigned k=0;k<n*2;k++)assert(std::isfinite(audio[k]));
  uint32_t p;while(md_midi_out_read(&p)){
   unsigned ch=(p>>8)&15,note=(p>>16)&127;
   if((p&15)==9){assert(!held[ch][note]);held[ch][note]++;ons++;}
   else if((p&15)==8){assert(held[ch][note]);held[ch][note]--;offs++;}
  }
  frames-=n;
 }
}
static void command(int c,int t,int v){assert(md_command(c,t,v));render();}
static void balanced(){assert(ons==offs);for(auto &ch:held)for(int n:ch)assert(!n);}
static void fresh(){
 md_init();uint32_t unused;while(md_midi_out_read(&unused)){}
 ons=offs=0;memset(held,0,sizeof held);
 md_patch p=*md_state();p.journey.mode=0;p.kit.enabled=0;p.tempo=120;p.locked=1;
 for(unsigned i=0;i<MD_TRACKS;i++){
  p.track[i].model=MD_VA;p.track[i].mute=0;p.track[i].speed=1;p.track[i].ratchet=1;
  p.sequence[i].enabled=1;p.sequence[i].length=1;
  p.sequence[i].steps[0]={(uint8_t)(60+i),80,100,100};
 }
 assert(md_load(&p));for(unsigned i=0;i<MD_TRACKS;i++)md_set_midi_out(i,i+1);
}
int main(){
 for(int source=1;source<=3;source++){
  fresh();command(MD_CLOCK_SOURCE,0,source);command(MD_PLAY,0,1);
  render(MD_RATE*2-2);assert(md_playing()&&!ons&&md_clock_source()==source);
  render(2);assert(!md_playing()&&!ons&&md_clock_source()==source);
  command(MD_PLAY,0,1);assert(md_playing()&&!ons);
  render(MD_RATE*2-2);assert(md_playing()&&!ons);
  render(2);assert(!md_playing()&&!ons);balanced();
  // Retry again, then let clock arrive late, before its renewed deadline.
  command(MD_PLAY,0,1);render(MD_RATE*3/2);assert(md_playing()&&!ons);
  const int port=source==1?0:source==2?2:1;
  for(unsigned k=0;k<96;k++){md_midi(0xf8,0,0,port);render(919);}
  assert(md_playing()&&ons&&md_clock_source()==source);
  md_midi(0xfc,0,0,port);render();assert(!md_playing());balanced();
  printf("PASS clock %d: no F8 stays silent, two-second retry wait, late F8 resumes, external Stop releases\n",source);
 }
 fresh();command(MD_PLAY,0,1);assert(md_playing()&&ons==6);
 render(MD_RATE*3);assert(md_playing()&&ons>6&&md_clock_source()==0);
 command(MD_PLAY,0,0);balanced();
 puts("PASS Internal: local PLAY still starts six channels immediately and continues");
 fresh();command(MD_CLOCK_SOURCE,0,1);command(MD_ITINERA_START,0,0);
 assert(md_clock_source()==0&&md_playing());render(MD_RATE*4);assert(ons&&md_playing());
 command(MD_PLAY,0,0);assert(md_clock_source()==1&&!md_playing());balanced();
 command(MD_PLAY,0,1);render(MD_RATE);assert(md_playing());
 render(MD_RATE+128);assert(!md_playing()&&md_clock_source()==1);balanced();
 command(MD_ITINERA_START,0,0);render(MD_RATE);
 command(MD_PLAY,0,2);assert(md_clock_source()==1&&md_playing());
 render(MD_RATE*2+128);assert(!md_playing()&&md_clock_source()==1);balanced();
 puts("PASS Itinera: temporary Internal, USB restored on Stop and keep, no silent clock fallback");
 return 0;
}
