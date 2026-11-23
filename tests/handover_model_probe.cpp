/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Public-API regression for a model edit between HANDOVER stages.
 * Internal state is observed only; all setup/edits/transitions use public APIs.
 * Fixtures independently found by the source audit:144/SATIN and212/VA.
 * No device ports, saved objects, installation or target build. */
#ifdef MD_REFERENCE_SOURCE
#include MD_REFERENCE_SOURCE
#else
#include "dsp/dreams.cpp"
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static float audio[512];static bool fixed;static uint32_t audio_hash=2166136261u;
static bool classical(int m){return
#ifdef MD_ELEMENTS_BODY_IN_VOICE
 m==MD_ELEMENTS||
#endif
 m==MD_KICK||m==MD_SNARE||m==MD_HAT||m==MD_FM||m==MD_VA||m==MD_RINGS||m==MD_HARMONIC;}
static void check_ownership(){
 for(unsigned i=0;i<6;i++){
  assert(voices[i].model==md_state()->track[i].model);
#ifdef MD_ENGINE_UNION
  assert(!!(voice_engine_live&(1u<<i))==classical(voices[i].model));
#endif
 }
}
static void render(unsigned n=256){
 md_render(audio,n);for(unsigned i=0;i<2*n;i++)assert(std::isfinite(audio[i]));
 const auto *bytes=(const unsigned char*)audio;for(unsigned i=0;i<2*n*sizeof(float);i++)audio_hash=(audio_hash^bytes[i])*16777619u;
 assert(!md_status()->nonfinite&&!md_status()->dropped_commands);if(fixed)check_ownership();
}
static void command(int c,int t,int value){assert(md_command_valid(c));assert(md_command_allowed(c));assert(md_command(c,t,value));}
int main(int argc,char **argv){
 assert(argc==4);const int target=std::atoi(argv[1]);assert(target==144||target==212);const char *kind=argv[2];fixed=std::strcmp(argv[3],"fixed")==0;assert(fixed||std::strcmp(argv[3],"old")==0);
 md_init();md_patch p=*md_state();p.seed=0x11223345u;p.biome_id=-1;p.tempo=220;p.quality=0;p.fx_level=0;p.journey.mode=0;p.kit.enabled=0;p.macro[3]=0;
 for(unsigned i=0;i<6;i++){p.track[i].model=MD_SATIN;p.track[i].mute=0;p.track[i].level=300;p.track[i].note=48+i;p.track[i].length=16;p.track[i].hits=1;p.ext[i].delay_send=p.ext[i].reverb_send=0;}
 assert(md_load(&p));md_set_biome_change(2);assert(md_biome_change()==2);
 command(MD_PLAY,0,1);command(MD_SCENE,0,target);command(MD_SCENE_CONFIRM,0,1);render(1);assert(md_biome_arriving()==target&&soft_count>=2);
 unsigned blocks=0;while(md_biome_arriving()>=0&&soft_stage==0&&blocks++<20000)render();assert(blocks<20000&&soft_stage==1);
 const int track=target==144?3:0,factory=md_state()->track[track].model,factory_tone=md_state()->track[track].tone;
 assert((soft_mask[0]&(1u<<track))&&!(soft_mask[1]&(1u<<track)));assert(factory==(target==144?MD_SATIN:MD_VA));check_ownership();
 const bool tone_value=std::strcmp(kind,"tone-value")==0;const int edited=tone_value?factory:MD_RINGS;
 const int preset=md_model_first_tone(MD_RINGS);assert(preset>=0&&md_tone_model(preset)==MD_RINGS);
 if(std::strcmp(kind,"model")==0)command(MD_MODEL,track,edited);
 else if(std::strcmp(kind,"tone-preset")==0)command(MD_TONE_PRESET,track,preset);
 else if(std::strcmp(kind,"program")==0){assert(md_midi_route(track)==track+1);md_midi(0xb0|track,0,preset/128,0);md_midi(0xc0|track,preset%128,0,0);}
 else{assert(tone_value);command(MD_TONE,track,(factory_tone+137)%1001);}
 render(1);assert(md_state()->track[track].model==edited&&voices[track].model==edited);check_ownership();
 std::printf("{\"event\":\"edit_accepted\",\"target\":%d,\"track\":%d,\"factory_model\":%d,\"edited_model\":%d,\"kind\":\"%s\",\"stage\":%u}\n",target,track,factory,edited,kind,soft_stage);std::fflush(stdout);
 const unsigned edited_stage=soft_stage;blocks=0;while(md_biome_arriving()>=0&&soft_stage==edited_stage&&blocks++<20000)render();assert(blocks<20000);
 const int now=md_state()->track[track].model,tag=voices[track].model;const bool mismatch=now!=tag;
 std::printf("{\"event\":\"after_next_stage\",\"target\":%d,\"track\":%d,\"kind\":\"%s\",\"patch_model\":%d,\"voice_model\":%d,\"mismatch\":%s,\"stage\":%u,\"audio_hash\":%u}\n",target,track,kind,now,tag,mismatch?"true":"false",soft_stage,audio_hash);std::fflush(stdout);
 assert(now==factory&&md_state()->track[track].tone==factory_tone); // Existing Factory-rebuild policy retained.
 if(!fixed&&!tone_value){assert(mismatch&&tag==edited);return 0;} // Expected old negative; classicalcase may abort sooner under UBSan.
 assert(!mismatch);check_ownership();
 // Continue after the boundary: the corrected model must really render safely.
 for(unsigned i=0;i<64;i++)render();command(MD_PLAY,0,0);render();assert(!md_playing());check_ownership();
 std::printf("{\"event\":\"safe_audio_and_stop\",\"kind\":\"%s\",\"audio_hash\":%u,\"nonfinite\":%u}\n",kind,audio_hash,md_status()->nonfinite);
}
