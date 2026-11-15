// Exercise the real sequencer and output queue, including a stalled USB consumer.
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
static float audio[256];
static int failures;
static void run(unsigned frames=128) {
 while(frames){unsigned n=frames>128?128:frames;md_render(audio,n);
  for(unsigned k=0;k<n*2;k++)assert(std::isfinite(audio[k]));frames-=n;}
}
static std::vector<uint32_t> drain(){std::vector<uint32_t> v;uint32_t p;while(md_midi_out_read(&p))v.push_back(p);return v;}
static void check(const char *name,bool good){std::printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good;}
static void command(int c,int t,int v){assert(md_command(c,t,v));run();}
static void fresh(bool external=false){
 md_init();drain();
 command(MD_CLOCK_SOURCE,0,0);command(MD_PLAY,0,0);
 md_patch p=*md_state();p.journey.mode=0;p.kit.enabled=0;p.tempo=30;
 for(unsigned i=0;i<6;i++){
  p.track[i].model=MD_VA;p.track[i].mute=0;p.track[i].speed=8;p.track[i].level=300;
  p.sequence[i].enabled=1;p.sequence[i].length=32;
  for(auto &s:p.sequence[i].steps)s={static_cast<uint8_t>(48+i),90,100,100};
 }
 assert(md_load(&p));for(unsigned i=0;i<6;i++)md_set_midi_out(i,i+1);drain();
 if(external){command(MD_CLOCK_SOURCE,0,1);md_midi(0xfa,0,0,0);md_midi(0xf8,0,0,0);run();}
 else command(MD_PLAY,0,1);
}
static int notes(const std::vector<uint32_t>&v,unsigned cin){int n=0;for(auto p:v)n+=(p&15)==cin;return n;}
int main(){
 fresh();auto first=drain();check("six generated notes on six USB channels",notes(first,9)==6);
 command(MD_MUTE,2,1);auto v=drain();check("mute ends its long external note in the next audio block",v.size()==1&&(v[0]&0xffffff)==(0x08u|(0x82u<<8)|(50u<<16)));
 command(MD_MUTE,2,1);check("repeated mute does not create duplicate note-offs",drain().empty());
 command(MD_PLAY,0,0);check("Stop ends the five remaining notes",notes(drain(),8)==5);
 fresh(true);drain();md_midi(0xfc,0,0,0);run();check("external MIDI Stop immediately ends all generated notes",notes(drain(),8)==6);
 fresh();drain();command(MD_CLOCK_SOURCE,0,1);check("changing clock source stops outgoing notes",notes(drain(),8)==6);
 fresh(true);drain();run(MD_RATE*2+256);check("clock timeout ends generated notes when playback stops",notes(drain(),8)==6);
 fresh();drain();command(MD_KIT_MODE,0,1);check("enabling a kit releases the three replaced synth tracks",notes(drain(),8)==3);
 command(MD_PANIC,0,0);check("PANIC releases the remaining synth tracks",notes(drain(),8)==3);
 // Never drain while rendering: exercise saturation using normal public DSP calls.
 fresh();run(MD_RATE*24);command(MD_PLAY,0,0);v=drain();
 int held[16][128]={};bool balanced=true;int ons=0,offs=0;
 for(auto p:v){unsigned st=(p>>8)&255,n=(p>>16)&127,ch=st&15;
  if((p&15)==9){if(held[ch][n])balanced=false;held[ch][n]++;ons++;}
  else if((p&15)==8){if(!held[ch][n])balanced=false;held[ch][n]--;offs++;}
 }
 for(auto &ch:held)for(int n:ch)balanced&=n==0;
 check("a stalled USB consumer cannot strand notes after queue saturation",balanced&&ons>6&&ons==offs&&v.size()<=64);
 std::printf("queue: %zu packets, %d note-ons, %d note-offs\n",v.size(),ons,offs);
 // Channel switches must release the old channel, not the new channel.
 fresh();drain();md_set_midi_out(0,16);v=drain();check("channel change sends note-off on the original channel",v.size()==1&&((v[0]>>8)&255)==0x80);
 command(MD_PANIC,0,0);drain();
 return failures?1:0;
}
