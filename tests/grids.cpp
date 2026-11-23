/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * 2.4 · GRIDS (Mutable Instruments' drum sequencer) as kit generator 4: what a re-roll may and may not touch.
 * Mode: tools/build_local.py grids, then app/build/native/grids-tests. */
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
static float lr[256];
static void render(unsigned frames){
 while(frames){unsigned n=frames>128?128:frames;md_render(lr,n);for(unsigned k=0;k<n*2;k++)assert(std::isfinite(lr[k])&&fabsf(lr[k])<1);frames-=n;}
}
static void cmd(int c,int t,int v){assert(md_command(c,t,v));render(1);}
/* a kit patch on GRIDS: tempo 200 (a 32-step pattern lasts 2.4 s), seed locked or free */
static md_patch grids_patch(int locked){
 md_init();md_patch p=*md_state();
 p.journey.mode=0;p.tempo=200;p.locked=(uint8_t)locked;
 for(auto &t:p.track)t.mute=1;
 p.track[3].mute=0;p.track[3].level=900;p.track[3].speed=1;p.track[3].prob=1000;
 p.kit.enabled=1;p.kit.generator=4;p.kit.length=32;
 return p;
}
static const unsigned PATTERN=(unsigned)(MD_RATE*60.0f/(200.0f*4.0f))*32u;
struct Run {uint32_t at[4][3];uint32_t kept[5];unsigned hits;};
/* play four patterns; at each pattern's first step note KICK/SNARE/HAT and what pads 3..7 hold */
static Run play(int locked,const uint32_t *keep){
 md_patch p=grids_patch(locked);
 for(int i=0;i<5;i++)p.kit.gates[3+i]=keep?keep[i]:0;
 assert(md_validate(&p)&&md_load(&p));
 cmd(MD_PLAY,0,1);Run r;memset(&r,0,sizeof r);
 for(int n=0;n<4;n++){
  render(n?PATTERN:200);
  for(int i=0;i<3;i++)r.at[n][i]=md_state()->kit.gates[i];
 }
 for(int i=0;i<5;i++)r.kept[i]=md_state()->kit.gates[3+i];
 r.hits=md_status()->events[3];
 cmd(MD_PLAY,0,0);return r;
}
int main(){
 /* the button: KICK, SNARE and HAT only (pads 0-2), 32 steps, a valid patch */
 md_patch p=grids_patch(0);assert(md_load(&p));
 for(int i=0;i<8;i++)cmd(MD_KIT_GENERATE,3,4);
 {const md_kit &k=md_state()->kit;assert(k.generator==4&&k.length==32);
  assert((k.gates[0]|k.gates[1]|k.gates[2])!=0);
  for(int i=3;i<8;i++)assert(k.gates[i]==0);
  assert(md_validate(md_state()));}
 puts("PASS GRIDS generate: pads 1-3 only, 32 steps, valid");
 /* the range of a saved patch: 4 is GRIDS, 5 stays invalid */
 {md_patch q=*md_state();q.kit.generator=4;assert(md_validate(&q));q.kit.generator=5;assert(!md_validate(&q));}
 puts("PASS kit generator range 0-4");
 /* GRIDS re-rolls at every pattern: the other pads (hits played or recorded by hand) are left alone */
 static const uint32_t keep[5]={0x11111111u,0x80000001u,0x0000ff00u,0x00010001u,0x24924924u};
 Run a=play(0,keep);
 for(int i=0;i<5;i++)assert(a.kept[i]==keep[i]);
 assert(a.hits>10);
 puts("PASS GRIDS re-roll keeps pads 4-8 and plays");
 /* a locked seed repeats: every pattern, and the same patch again, gives the same hits */
 Run b=play(1,0),c=play(1,0);
 for(int n=1;n<4;n++)assert(!memcmp(b.at[0],b.at[n],sizeof b.at[0]));
 assert(!memcmp(b.at,c.at,sizeof b.at));
 puts("PASS GRIDS with a locked seed repeats");
 /* a free seed: bounded, finite audio (checked in render) and still the same pads untouched */
 Run d=play(0,0);
 for(int i=0;i<5;i++)assert(d.kept[i]==0);
 puts("PASS GRIDS with a free seed");
 return 0;
}
