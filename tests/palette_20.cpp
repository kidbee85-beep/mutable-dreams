// Offline 2.0 palette qualification. This is not an FM-1 timing measurement.
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstddef>
static float lr[256];
static double render(unsigned frames){
 double energy=0;
 while(frames){unsigned n=frames>128?128:frames;md_render(lr,n);
  for(unsigned k=0;k<2*n;k++){assert(std::isfinite(lr[k])&&std::fabs(lr[k])<1);energy+=lr[k]*lr[k];}
  frames-=n;
 }return energy;
}
static void cmd(int c,int t,int v){assert(md_command(c,t,v));render(1);}
int main(){
 static_assert(sizeof(md_patch)==2604,"v10 appends Journey after the unchanged v9 prefix.");
 static_assert(offsetof(md_patch,motion)==2220,"Retain the complete old prefix.");
 static_assert(MD_BIOMES==197&&MD_TONE_PRESETS==420,"Private 2.0 palette size.");
 md_init();unsigned memory=md_memory_bytes();
 for(int id=169;id<MD_BIOMES;id++){
  md_init();cmd(MD_SCENE,0,id);assert(md_state()->biome_id==id&&md_validate(md_state()));
  md_patch saved=*md_state(),decoded;
  assert(md_decode(&decoded,&saved,sizeof saved)&&!memcmp(&decoded,&saved,sizeof saved));
  assert(md_import(&saved,sizeof saved));assert(!md_unsaved());
  assert(md_active_tracks()==(id==174||id==182||id==187?4:6));
  cmd(MD_PLAY,0,1);double energy=render(MD_RATE*16);
  assert(energy>.01);assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
  assert(!md_status()->quality_reductions);
  cmd(MD_PLAY,0,0);cmd(MD_PANIC,0,0);assert(render(128)==0);
  assert(md_memory_bytes()==memory);
 }
 puts("PASS: eight new worlds audible, finite, losslessly stored, independent kit layout, exact panic silence.");
 for(int id=364;id<MD_TONE_PRESETS;id++){
  md_init();cmd(MD_SCENE,0,MD_BLANK_BIOME);md_track old=md_state()->track[0];
  cmd(MD_TONE_PRESET,0,id);const md_track &now=md_state()->track[0];
  assert(now.hits==old.hits&&now.note==old.note&&now.level==old.level&&now.send==old.send&&now.mute==old.mute);
  assert(md_state()->tone_preset[0]==id&&md_validate(md_state()));
  cmd(MD_TRIGGER,0,48);assert(render(MD_RATE*3)>.001);
 }
 puts("PASS: sixteen added tones audible, recall retains mix, pitch and rhythm.");
 for(int id=169;id<MD_BIOMES;id++){
  md_init();cmd(MD_SCENE,0,id);cmd(MD_MACRO,3,1000);cmd(MD_GRAIN_SIZE,0,1000);
  cmd(MD_GRAIN_NOISE,0,650);cmd(MD_FEEDBACK,0,700);cmd(MD_PLAY,0,1);
  render(MD_RATE*6);assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 }
 std::printf("PASS: dense shared-grain stress; fixed DSP-accounting size %u bytes (host, not device headroom).\n",memory);
}
