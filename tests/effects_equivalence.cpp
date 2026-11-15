/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * A/B probe of real mix/stem output during dream controls and effect transitions.
 * Compare the output with a separately built reference using test_effects_equivalence.py. */
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
static float audio[512];static int16_t stems[256*8];
static uint64_t hash=1469598103934665603ull;static unsigned frames;
static void add(const void *p,unsigned bytes){const auto *b=(const uint8_t*)p;for(unsigned i=0;i<bytes;i++)hash=(hash^b[i])*1099511628211ull;}
static void render(unsigned n){while(n){unsigned z=n>256?256:n;md_render_stems(audio,z,{stems,stems,256});for(unsigned i=0;i<z*2;i++)assert(std::isfinite(audio[i]));add(audio,z*2*sizeof(float));add(stems,z*8*sizeof(int16_t));frames+=z;n-=z;}}
static void cmd(int c,int t,int v){assert(md_command(c,t,v));render(1);}
static void done(int c){printf("%d,%u,%016llx,%u,%u\n",c,frames,(unsigned long long)hash,md_status()->nonfinite,md_status()->dropped_commands);assert(!md_status()->nonfinite&&!md_status()->dropped_commands);}
int main(){
 for(unsigned id=0;id<MD_ITINERA_PRESETS;id++){
  hash=1469598103934665603ull;frames=0;md_init();cmd(MD_ITINERA_START,id,md_itinera_defaults(id)&~3u);
  render(44100*8);cmd(MD_MACRO,3,1000);render(44100*2);cmd(MD_PLAY,0,2);render(44100);cmd(MD_PLAY,0,0);render(44100*3);done(id);
 }
 for(unsigned id=0;id<8;id++){
  hash=1469598103934665603ull;frames=0;md_init();cmd(MD_SCENE,0,189+id*7);md_patch p=*md_state();p.seed=0x12345678;p.locked=1;p.journey.mode=0;p.quality=id&1;p.fx_level=id==0?0:800;p.grain_noise=id*100;p.grain_pitch=(int)id*6-24;p.reverse=id&1;p.feedback=800;p.macro[3]=1000;
  for(auto &e:p.ext){e.delay_send=700;e.reverb_send=800;}assert(md_load(&p));cmd(MD_PLAY,0,1);render(44100*4);cmd(MD_FREEZE,0,1);render(44100*2);cmd(MD_FX_LEVEL,0,id==0?800:0);render(44100);cmd(MD_FREEZE,0,0);cmd(MD_PLAY,0,0);render(44100*4);done(100+id);
 }
}
