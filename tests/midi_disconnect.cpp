// Real DSP/MIDI integration: removal of one controller must not strand pitch,
// silence the next player, hold the pedal, or disarm another transport's pickup.
#include "dsp/dreams.h"
#include <cmath>
#include <cstdio>
#include <cassert>
static float audio[256];static int failures;
static double run(unsigned n){double energy=0;while(n){unsigned q=n>128?128:n;md_render(audio,q);for(unsigned i=0;i<q*2;i++){assert(std::isfinite(audio[i]));energy+=audio[i]*audio[i];}n-=q;}return energy;}
static void check(const char *name,bool good){printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good;}
static void cc(int port,int n,int v){md_midi(0xb0,n,v,port);}
static void fresh(){md_init();md_patch p=*md_state();p.journey.mode=0;p.kit.enabled=0;p.macro[3]=0;p.fx_level=0;for(int i=0;i<6;i++){p.track[i].mute=i!=0;p.track[i].send=0;p.ext[i].delay_send=p.ext[i].reverb_send=0;}p.track[0].model=MD_REED;p.track[0].attack=0;p.track[0].level=700;assert(md_load(&p));}
int main(){
 fresh();md_midi(0xe0,0,0,0);md_midi_lost(0);check("unplugging a bent controller centers future notes",md_midi_bend(0)==0);
 fresh();md_midi(0xe0,0,0,0);md_midi(0xe0,0,96,1);float latest=md_midi_bend(0);md_midi_lost(0);check("unplugging another transport keeps the latest bend",md_midi_bend(0)==latest);md_midi_lost(1);check("unplugging the bend owner centers its wheel",md_midi_bend(0)==0);
 fresh();cc(0,11,0);md_midi_lost(0);md_midi(0x90,60,100,1);run(MD_RATE/2);check("expression-zero controller cannot leave the next player silent",run(MD_RATE/8)>.01);
 fresh();cc(0,64,127);md_midi_lost(0);md_midi(0x90,60,100,1);run(128);md_midi(0x80,60,0,1);check("a removed pedal cannot latch a later transport's note",md_midi_held_port(0)==-1);
 fresh();md_midi(0x90,60,100,1);cc(0,64,127);md_midi(0x80,60,0,1);check("cross-transport pedal can hold an existing note",md_midi_held_port(0)==1);md_midi_lost(0);check("unplugged pedal releases an already sustained note",md_midi_held_port(0)==-1);
 fresh();md_midi(0x90,60,100,1);cc(1,64,127);md_midi_lost(0);md_midi(0x80,60,0,1);check("unrelated disconnect preserves a live pedal and note",md_midi_held_port(0)==1);cc(1,64,0);check("remaining pedal releases normally",md_midi_held_port(0)==-1);
 fresh();md_midi(0xe0,0,0,0);md_midi_lost(-1);md_midi_lost(3);check("invalid disconnect IDs have no side effects",md_midi_bend(0)==-2);
 fresh();md_midi_mapping m={1,0,1,74,4,0,{0,0}};assert(md_set_mapping(0,&m));cc(0,74,0);cc(0,74,127);assert(md_learn_picked(0));md_midi_lost(1);check("another transport's removal keeps MIDI Learn pickup",md_learn_picked(0));md_midi_lost(0);check("removed controller requires pickup again",!md_learn_picked(0));
 fresh();m.port=3;assert(md_set_mapping(0,&m));cc(0,74,0);cc(0,74,127);assert(md_learn_picked(0));md_midi_lost(2);check("Any-port mapping keeps pickup when its actual sender remains",md_learn_picked(0));md_midi_lost(0);check("Any-port mapping rearms when its actual sender disappears",!md_learn_picked(0));
 return failures?1:0;
}
