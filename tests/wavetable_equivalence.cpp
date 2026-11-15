#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
static float lr[512];static int16_t stems[256*8];static uint64_t hash;
static void bytes(const void *data,unsigned n){auto p=(const uint8_t*)data;for(unsigned i=0;i<n;i++)hash=(hash^p[i])*1099511628211ull;}
static void render(unsigned n){while(n){unsigned z=n>256?256:n;md_render_stems(lr,z,{stems,stems,256});for(unsigned i=0;i<2*z;i++)assert(std::isfinite(lr[i]));bytes(lr,2*z*sizeof(float));bytes(stems,8*z*sizeof(int16_t));n-=z;}}
int main(){unsigned count=0;for(int model:{MD_SATIN,MD_SPECTRAL})for(int note:{24,48,72,96})for(int harm:{0,500,1000})for(int wheel:{0,8192,16383}){
 md_init();md_patch p=*md_state();p.seed=0x1f2e3d4cu;p.locked=1;p.quality=1;p.journey.mode=0;p.kit.enabled=0;p.fx_level=650;
 for(unsigned i=0;i<6;i++){p.track[i].mute=i!=0;p.track[i].send=0;}
 auto &t=p.track[0];t.model=model;t.note=note;t.level=700;t.tone=960;t.harm=harm;t.shape=1000;t.filter=2;t.cutoff=800;t.resonance=250;t.attack=100;t.decay=800;
 p.ext[0].delay_send=350;p.ext[0].reverb_send=550;assert(md_load(&p));
 for(unsigned i=0;i<6;i++)md_set_midi_route(i,i?0:1);
 md_midi(0xb0,101,0,0);md_midi(0xb0,100,0,0);md_midi(0xb0,6,24,0);md_midi(0xe0,wheel&127,wheel>>7,0);
 hash=1469598103934665603ull;md_midi(0x90,note,100,0);render(MD_RATE/2);md_midi(0x80,note,0,0);render(MD_RATE/4);
 assert(!md_status()->nonfinite&&!md_status()->dropped_commands);printf("%d,%d,%d,%d,%016llx\n",model,note,harm,wheel,(unsigned long long)hash);count++;
 }assert(count==72);}
