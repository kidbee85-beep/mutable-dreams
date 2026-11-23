/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#include "mix_pipeline_device_test_backend.h"
#include "dsp/dreams.cpp"
#include <cstdio>
static unsigned partial_runs;
static float phase_after_partial;
static void partial_source(void *p){
 auto &j=*(MixJob*)p;MD_CPU2_CHECK(j.source.kind==1);
 const auto &s=j.source;const float (*table)[4][257]=satin_table;
 const float *ha=table[s.band][s.shape],*hb=table[s.band][s.shape+1];
 const float *la=s.blend_low?table[s.band-1][s.shape]:ha,*lb=s.blend_low?table[s.band-1][s.shape+1]:hb;
 render_wavetable_tile(*j.voice,j.f,s.f2,s.morph,ha,hb,la,lb,s.blend_low,s.fade,j.out,1);
 phase_after_partial=j.voice->sub_phase;partial_runs++;
}
int main(){
 md_init();float out[64]={},aux[64]={},lr[128]={},send[64]={},delay[64]={},room[64]={},duck[64]={};
 MixJob j={};j.voice=voices;j.track=patch.track[0];j.track.model=MD_SATIN;j.n=j.release_at=64;j.out=out;j.aux=aux;j.lr=lr;j.send=send;j.delay_send=delay;j.room_send=room;j.duck=duck;j.source={1,0,0,.001f,.2f,1.f,false};j.check=mix_check(j);MD_CPU2_CHECK(mix_valid(j));
 const MixJob good=j;
 for(unsigned fault=0;fault<10;fault++){
  j=good;switch(fault){case 0:j.source.kind=3;break;case 1:j.track.model=MD_FM;break;case 2:j.source.band=4;break;case 3:j.source.shape=3;break;case 4:j.source.f2=NAN;break;case 5:j.source.morph=-1;break;case 6:j.source.fade=2;break;case 7:j.source.blend_low=true;break;case 8:j.f=NAN;break;case 9:j.source.f2=-.1f;break;}
  j.check=mix_check(j);MD_CPU2_CHECK(!mix_valid(j));
 }
 j=good;j.source.f2=.002f;MD_CPU2_CHECK(!mix_valid(j));
 auto p=*md_state();p.kit.enabled=0;p.journey.mode=0;for(auto &t:p.track){t.model=MD_SATIN;t.mute=0;t.level=600;t.hits=0;}
 MD_CPU2_CHECK(md_load(&p));for(unsigned i=0;i<6;i++)md_midi(0x90|i,60+i,100,0);
 const float voice1_phase=voices[1].sub_phase;device_partial_hook=partial_source;md_render(lr,64);
 uint32_t counts[3];md_source_mix_stats(counts);
#ifdef MD_SOURCE_READY_HOST_TEST
 const unsigned early_sources=0;
#else
 const unsigned early_sources=1; // CPU0 may render source1 before the failed mix0 join.
#endif
 MD_CPU2_CHECK(partial_runs==1&&device_aborted&&counts[0]==1&&counts[1]==0&&counts[2]==early_sources);
 MD_CPU2_CHECK((voices[1].sub_phase!=voice1_phase)==(early_sources!=0));
 MD_CPU2_CHECK(voices[0].sub_phase==phase_after_partial); // accepted partial source never replayed
 MD_CPU2_CHECK(!mix_host.occupied); // descriptor returned only after actual host completion
 std::printf("PASS source bounds/checksum, accepted partial source once, abort and no replay; host mock completed before descriptor release\n");
}
