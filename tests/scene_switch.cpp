/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Biome changes inside one audio callback and the saved-state fingerprint. */
#include "dsp/dreams.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static float lr[512];
static uint64_t audio_hash(unsigned blocks,uint64_t h=1469598103934665603ull){
 while(blocks--){
  md_render(lr,256);
  const uint8_t *b=(const uint8_t*)lr;
  for(unsigned j=0;j<sizeof lr;j++)h=(h^b[j])*1099511628211ull;
 }
 return h;
}
// Play, then queue the given Biome requests so they all reach one callback.
static uint64_t run(const int *ids,unsigned count,bool playing){
 md_init();
 assert(md_command(MD_SCENE,0,244));audio_hash(1);
 if(playing)assert(md_command(MD_PLAY,0,1));
 uint64_t h=audio_hash(40);
 for(unsigned j=0;j<count;j++)assert(md_command(MD_SCENE,0,ids[j]));
 h=audio_hash(600,h);
 assert(md_state()->biome_id==ids[count-1]);
 return h;
}
int main(){
 static const int burst[]={169,207,220,234,251,217},last[]={217};
 for(int playing=0;playing<2;playing++){
  const uint64_t many=run(burst,6,playing),one=run(last,1,playing);
  assert(many==one);
 }
 // A run broken by another command is applied in order, not coalesced.
 {
  md_init();assert(md_command(MD_SCENE,0,169));assert(md_command(MD_TONE,0,333));assert(md_command(MD_SCENE,0,207));
  audio_hash(1);
  assert(md_state()->biome_id==169&&md_pending_biome()==207);
  assert(md_command(MD_SCENE_CONFIRM,0,0));audio_hash(1);assert(md_pending_biome()<0);
 }
 // With unsaved edits the final request of a burst is the one held for SEL.
 {
  md_init();assert(md_command(MD_SCENE,0,169));audio_hash(1);
  assert(md_command(MD_CUTOFF,0,123));audio_hash(1);assert(md_unsaved());
  for(int id:burst)assert(md_command(MD_SCENE,0,id));
  audio_hash(1);
  assert(md_state()->biome_id==169&&md_pending_biome()==217);
  assert(md_command(MD_SCENE_CONFIRM,0,1));audio_hash(1);
  assert(md_state()->biome_id==217&&!md_unsaved());
 }
 // Cursor-only moves stay outside the fingerprint; sound edits do not.
 {
  md_init();assert(md_command(MD_SCENE,0,101));audio_hash(1);assert(!md_unsaved());
  assert(md_command(MD_SEQ_STEP,1,5));assert(md_command(MD_BASS_STEP,2,7));assert(md_command(MD_KIT_PAD,3,4));assert(md_command(MD_KIT_STEP,3,6));
  audio_hash(1);assert(!md_unsaved());
  assert(md_command(MD_SEQ_VELOCITY,1,33));audio_hash(1);assert(md_unsaved());
 }
 // The saved mark follows the written image, not the live patch.
 {
  md_init();assert(md_command(MD_SCENE,0,207));audio_hash(1);
  assert(md_command(MD_CUTOFF,1,410));audio_hash(1);assert(md_unsaved());
  md_patch written=*md_project_state();
  assert(md_command(MD_CUTOFF,1,620));audio_hash(1); // arrives while flash is busy
  md_mark_saved_from(&written);
  assert(md_unsaved());
  assert(md_command(MD_CUTOFF,1,410));audio_hash(1);
  assert(!md_unsaved());
  md_mark_saved_from(nullptr);assert(!md_unsaved());
  md_mark_saved();assert(!md_unsaved());
 }
 puts("PASS one rack rebuild per Biome burst, bit-identical audio stopped/playing; ordered runs and SEL hold kept; cursor-free fingerprint; saved mark follows the written image.");
 return 0;
}
