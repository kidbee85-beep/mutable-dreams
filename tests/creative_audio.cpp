// Audible-path checks, separate from UI/storage tests. Host timing is not device headroom.
#include "dsp/dreams.h"
#include <cassert>
#include <vector>
#include <cstring>
#include <cmath>
#include <cstdio>
static std::vector<float> render(const md_patch &p){md_init();assert(md_load(&p));assert(md_command(MD_PLAY,0,1));std::vector<float>a(8*MD_RATE*2);for(unsigned i=0;i<a.size()/2;i+=64)md_render(a.data()+i*2,(a.size()/2-i<64)?a.size()/2-i:64);for(float x:a)assert(std::isfinite(x)&&fabsf(x)<1);return a;}
static bool same(const std::vector<float>&a,const std::vector<float>&b){return a.size()==b.size()&&!memcmp(a.data(),b.data(),a.size()*4);}
int main(){md_init();assert(md_command(MD_SCENE,0,169));float scratch[128];md_render(scratch,64);md_patch base=*md_state();base.fx_level=500;base.macro[3]=500;base.macro[2]=450;for(int i=0;i<6;i++){base.track[i].mute=0;base.track[i].level=350;base.track[i].hits=4;base.track[i].prob=1000;base.track[i].speed=1;base.sequence[i].enabled=0;base.track[i].filter=2;base.track[i].cutoff=600;base.track[i].send=100;base.ext[i].delay_send=100;base.ext[i].reverb_send=100;}
 auto ordinary=render(base);auto p=base;p.drift.captured=3;
 for(auto &s:p.drift.scene){for(int k=0;k<4;k++)s.macro[k]=base.macro[k];for(int i=0;i<6;i++){uint16_t v[8]={(uint16_t)base.track[i].tone,(uint16_t)base.track[i].cutoff,(uint16_t)base.track[i].decay,(uint16_t)base.track[i].level,(uint16_t)base.track[i].pan,(uint16_t)base.track[i].send,(uint16_t)base.ext[i].delay_send,(uint16_t)base.ext[i].reverb_send};memcpy(s.track[i],v,sizeof v);}}
 for(int i=0;i<6;i++){p.drift.scene[0].track[i][0]=120;p.drift.scene[0].track[i][1]=120;p.drift.scene[0].track[i][3]=180;p.drift.scene[1].track[i][0]=700;p.drift.scene[1].track[i][1]=800;p.drift.scene[1].track[i][3]=650;}
 assert(same(ordinary,render(p)));p.drift.mode=1;p.drift.depth=0;assert(same(ordinary,render(p)));p.drift.depth=1000;p.drift.position=0;auto a=render(p);p.drift.position=1000;auto b=render(p);assert(!same(a,b)&&!same(ordinary,a)&&!same(ordinary,b));puts("PASS captured-but-Off and depth-zero Drift are sample-identical; endpoint A and B audibly change the rendered stereo path.");
 for(int target=0;target<6;target++){p=base;p.links[0]={0,0,1,(uint8_t)target,(int16_t)(target==5?-900:650),650};assert(!same(ordinary,render(p)));printf("PASS Living Link destination %s changes rendered stereo output.\n",md_link_target_name(target));}
}
