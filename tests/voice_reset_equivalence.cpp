#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
static float lr[512];static uint64_t h;
static void render(unsigned n){while(n){unsigned z=n>256?256:n;md_render(lr,z);for(unsigned i=0;i<z*2;i++)assert(std::isfinite(lr[i]));const auto b=(const uint8_t*)lr;for(unsigned i=0;i<z*2*sizeof(float);i++)h=(h^b[i])*1099511628211ull;n-=z;}}
static void command(int c,int t,int v){assert(md_command(c,t,v));render(1);}
int main(){for(int model=0;model<MD_MODELS;model++){
 md_init();h=1469598103934665603ull;md_patch p=*md_state();p.seed=0xabcdef01u;p.quality=0;p.journey.mode=0;p.macro[3]=0;p.fx_level=0;for(unsigned i=0;i<6;i++){p.track[i].mute=i!=0;p.track[i].level=650;p.track[i].filter=0;p.track[i].send=0;p.ext[i].delay_send=p.ext[i].reverb_send=0;}assert(md_load(&p));
 command(MD_MODEL,0,MD_ELEMENTS);command(MD_TRIGGER,0,60);render(MD_RATE);
 command(MD_MODEL,0,model);command(MD_TRIGGER,0,69);render(MD_RATE/4);
 command(MD_MODEL,0,MD_ELEMENTS);command(MD_TRIGGER,0,60);render(MD_RATE);
 assert(!md_status()->nonfinite&&!md_status()->dropped_commands);printf("%d,%016llx\n",model,(unsigned long long)h);
 } }
