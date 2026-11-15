/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Real DSP and extracted app.c drain, with a bounded mock USB sink.
 * This proves queue handoff and locking, not hardware USB performance.
 */
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
enum {TO_USB=1};
static float audio[256];static std::vector<uint32_t> usb;
static int discarded,sink[16][128],lock_depth;static bool connected=true;
static uint32_t plat_lock(){return lock_depth++;}
static void plat_unlock(uint32_t prior){assert(lock_depth==(int)prior+1);lock_depth=prior;}
static uint32_t plat_midi_out_room(uint32_t to){assert(to==TO_USB&&lock_depth);return connected?64-usb.size():0;}
static void plat_midi_send(uint32_t p,uint32_t to){assert(to==TO_USB&&lock_depth);if(connected&&usb.size()<64)usb.push_back(p);else discarded++;}
#include "midi_out_drain_test.inc"
static void receive(uint32_t p){unsigned cin=p&15,ch=(p>>8)&15,n=(p>>16)&127;if(cin==9){assert(!sink[ch][n]);sink[ch][n]=1;}else if(cin==8){assert(sink[ch][n]);sink[ch][n]=0;}}
static int sounding(){int n=0;for(auto &c:sink)for(int x:c)n+=x;return n;}
static void consume(){for(auto p:usb)receive(p);usb.clear();}
static void run(){md_render(audio,128);for(float x:audio)assert(std::isfinite(x));}
static void command(int c,int t,int v){assert(md_command(c,t,v));run();}
static void scenario(bool disconnect){
 discarded=0;usb.clear();for(auto &c:sink)for(int &x:c)x=0;
 md_init();uint32_t p;while(md_midi_out_read(&p)){}
 md_patch patch=*md_state();patch.journey.mode=0;patch.kit.enabled=0;patch.tempo=30;
 for(unsigned i=0;i<6;i++){patch.track[i].model=MD_VA;patch.track[i].mute=0;patch.track[i].speed=8;patch.track[i].level=300;patch.sequence[i].enabled=1;patch.sequence[i].length=32;for(auto &s:patch.sequence[i].steps)s={(uint8_t)(48+i),90,100,100};}
 assert(md_load(&patch));for(unsigned i=0;i<6;i++)md_set_midi_out(i,i+1);
 command(MD_PLAY,0,1);midi_out_drain();consume();assert(sounding()==6);
 connected=!disconnect;
 for(unsigned frames=0;frames<MD_RATE*40;frames+=128){run();midi_out_drain();assert(lock_depth==0);}
 command(MD_PLAY,0,0);midi_out_drain();
 connected=true;consume();
 for(unsigned n=0;n<4;n++){midi_out_drain();consume();}
 assert(!discarded&&!sounding()&&!md_midi_out_read(&p)&&!lock_depth);
 printf("PASS actual app.c drain with %s: no discarded events or held notes after mock sink resumes\n",disconnect?"disconnected USB sink":"full 64-packet USB sink");
}
int main(){scenario(false);scenario(true);puts("Offline queue model only; no hardware USB timing claim.");}
