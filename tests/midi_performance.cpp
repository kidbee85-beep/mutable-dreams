// MIDI control extensions; no synthesizer or saved-patch format change.
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
static float lr[256];
static void run(unsigned n=128){while(n){unsigned z=n>128?128:n;md_render(lr,z);for(unsigned k=0;k<z*2;k++)assert(std::isfinite(lr[k]));n-=z;}}
static void cc(int port,int ch,int n,int v){md_midi(0xb0|ch,n,v,port);}
static void touch_render(int model,unsigned touch,int velocity,float *out,bool reinit=false){
 md_init();
 if(reinit){md_set_velocity_touch(100);md_init();}
 assert(md_command(MD_SCENE,0,MD_BLANK_BIOME));run();
 md_patch p=*md_state();
 p.track[0].model=model;p.track[0].level=700;p.track[0].mute=0;
 assert(md_load(&p));
 if(!reinit)md_set_velocity_touch(touch);
 md_midi(0x90,57,velocity,0);
 for(unsigned at=0;at<8192;at+=128)md_render(out+2*at,128);
}
int main(){
 md_init();for(unsigned i=0;i<6;i++)assert(md_midi_bend_range(i)==2);
 for(int p=0;p<3;p++)for(int k=0;k<4;k++)for(int v=0;v<128;v++){cc(p,15,16+k,v);assert(md_state()->macro[k]==v*1000/127);}
 puts("PASS every legacy MSB-only macro value is unchanged on USB/TRS/BLE.");
 for(int p=0;p<3;p++)for(int k=0;k<4;k++)for(int value=0;value<16384;value++){
  cc(p,15,16+k,value>>7);cc(p,15,48+k,value&127);assert(md_state()->macro[k]==value*1000/16383);
 }
 puts("PASS all 16384 high-resolution values / four macros / three transports, exact endpoints.");
 md_init();cc(0,15,16,64);cc(0,15,48,127);assert(md_state()->macro[0]==8319*1000/16383);cc(2,15,16,64);assert(md_state()->macro[0]==64*1000/127);md_midi_lost(0);cc(0,15,16,127);assert(md_state()->macro[0]==1000);
 md_set_midi_macro_enabled(0);int old=md_state()->macro[0];cc(0,15,16,3);cc(0,15,48,1);assert(md_state()->macro[0]==old);md_set_midi_macro_enabled(1);cc(0,15,16,127);assert(md_state()->macro[0]==1000);
 puts("PASS fine-value opt-in stays per port/macro, disconnect and enable reset it, Off preserves legacy track control.");
 md_init();md_midi(0xe0,0,0,0);assert(md_midi_bend(0)==-2);cc(0,0,6,12);assert(md_midi_bend_range(0)==2);cc(0,0,101,0);cc(2,0,100,0);cc(0,0,6,12);assert(md_midi_bend_range(0)==2);
 cc(0,0,100,0);cc(0,0,6,12);assert(md_midi_bend_range(0)==12&&md_midi_bend(0)==-12);cc(0,0,38,50);assert(md_midi_bend_range(0)==12.5f&&md_midi_bend(0)==-12.5f);
 assert(md_midi_bend_range(1)==2);cc(0,0,101,127);cc(0,0,100,127);cc(0,0,6,1);assert(md_midi_bend_range(0)==12.5f);
 cc(0,0,101,0);cc(0,0,100,0);cc(0,0,6,127);cc(0,0,38,127);assert(md_midi_bend_range(0)==24);cc(0,0,6,0);cc(0,0,38,0);assert(md_midi_bend_range(0)==0&&md_midi_bend(0)==0);
 cc(0,0,96,0);assert(md_midi_bend_range(0)==1);cc(0,0,97,0);cc(0,0,97,0);assert(md_midi_bend_range(0)==0);
 cc(0,0,99,4);cc(0,0,98,8);cc(0,0,6,19);assert(md_midi_bend_range(0)==0);
 cc(0,0,101,0);cc(0,0,6,19);assert(md_midi_bend_range(0)==0);cc(0,0,100,0);cc(0,0,6,5);assert(md_midi_bend_range(0)==5);
 puts("PASS NRPN selection cancels RPN; a partial re-selection cannot change pitch sensitivity.");
 puts("PASS RPN0,0 coarse/fine sensitivity, active bend rescale, Null, channel/port isolation and 0..24 semitone bounds.");
 md_set_midi_route(0,9);assert(md_midi_bend_range(0)==2&&md_midi_bend(0)==0);md_set_midi_route(1,9);cc(1,8,101,0);cc(1,8,100,0);cc(1,8,6,7);assert(md_midi_bend_range(0)==7&&md_midi_bend_range(1)==7);md_midi(0xe8,127,127,1);assert(md_midi_bend(0)>6.99f&&md_midi_bend(1)>6.99f);cc(1,8,121,0);assert(md_midi_bend(0)==0&&md_midi_bend_range(0)==7);cc(1,8,6,3);assert(md_midi_bend_range(0)==7);
 md_midi(0x98,96,100,1);run(MD_RATE);assert(!md_status()->nonfinite);md_init();assert(md_midi_bend_range(0)==2);
 puts("PASS shared channel fans out, route changes reset sensitivity, Reset Controllers centers wheel and nulls selection, reboot returns two semitones.");
 static float without[16384],with[16384],again[16384];
 for(int model: {MD_BOWED,MD_AERO,MD_FELT,MD_MELLOW}){
  for(int velocity: {25,112}){
   touch_render(model,0,velocity,without);touch_render(model,100,velocity,with);
   touch_render(model,0,velocity,again,true);
   assert(!memcmp(without,again,sizeof without));
   double energy=0,delta=0;
   for(unsigned k=0;k<16384;k++){
    assert(std::isfinite(with[k]));
    energy+=without[k]*without[k];const float d=without[k]-with[k];delta+=d*d;
   }
   assert(energy>.00001);
   if(model==MD_MELLOW)assert(delta==0);
   else assert(delta>energy*1e-7);
  }
 }
 puts("PASS MIDI note touch colours Bow, Aero and Felt at low and high velocities, remains opt-in and leaves the other models alone.");
}
