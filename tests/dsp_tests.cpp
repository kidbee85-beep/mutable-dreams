/* SPDX-License-Identifier: MIT */
#include "dsp/dreams.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <chrono>
#include <initializer_list>
#include <new>
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/additive_engine.h"
static float out[256];
static void render(unsigned blocks){for(unsigned i=0;i<blocks;i++){md_render(out,128);for(float x:out)if(!std::isfinite(x)||fabsf(x)>=1.0f){fprintf(stderr,"BAD OUTPUT %.9g frames %u nonfinite %u\n",x,md_status()->frames,md_status()->nonfinite);fflush(stderr);exit(2);}}}
static unsigned test_hash(){md_init();md_command(MD_SCENE,0,1);md_command(MD_PLAY,0,1);unsigned h=2166136261u;for(int i=0;i<1500;i++){md_render(out,128);for(float x:out){unsigned u;memcpy(&u,&x,4);h=(h^u)*16777619u;}}return h;}
template<class E> static void aux_equivalence(){
 alignas(E) unsigned char aa[sizeof(E)]={},bb[sizeof(E)]={};float ma[80]={},mb[80]={};
 E *a=new(aa)E(),*b=new(bb)E();stmlib::BufferAllocator pa(ma,sizeof ma),pb(mb,sizeof mb);a->Init(&pa);b->Init(&pb);a->Reset();b->Reset();
 for(int k=0;k<512;k++){
  float x[32]={},y[32]={},aux[32]={};unsigned n=1+k%32;bool done=false;
  plaits::EngineParameters par={1,36.0f+(k%60),(k%101)*.0099f,((k*7)%101)*.0099f,((k*13)%101)*.0099f,.8f};
  a->Render(par,x,aux,n,&done);b->Render(par,y,nullptr,n,&done);
  assert(!memcmp(x,y,n*sizeof(float)));
 }
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 aux_equivalence<plaits::FMEngine>();aux_equivalence<plaits::VirtualAnalogEngine>();aux_equivalence<plaits::AdditiveEngine>();puts("PASS unused AUX removal: bit-identical main output for FM/VA/Additive over 512 parameter blocks each");
 for(unsigned l=1;l<=32;l++)for(unsigned h=0;h<=l;h++)for(unsigned r=0;r<l;r++){unsigned count=0;for(unsigned s=0;s<l;s++)count+=md_hit(s,h,l,r);assert(count==h);}puts("PASS Euclidean hit counts / all lengths and rotations");
 assert(test_hash()==test_hash());puts("PASS deterministic restart / identical audio hash");
 md_init();md_patch p=*md_state();assert(md_validate(&p));p.scale=12;assert(md_validate(&p));p.scale=13;assert(!md_validate(&p));p=*md_state();p.track[0].length=0;assert(!md_load(&p));p=*md_state();p.feedback=999999;assert(!md_load(&p));puts("PASS corrupted patch rejected");
 for(int m=0;m<MD_MODELS;m++){
  md_init();md_command(MD_SCENE,0,1);for(int t=0;t<6;t++){md_command(MD_MODEL,t,m);md_command(MD_MUTE,t,t!=0);}md_command(MD_TRIGGER,0,m<3?40:60);render(1);
  double energy=0;float peak=0;for(int b=0;b<690;b++){md_render(out,128);for(float x:out){assert(std::isfinite(x));energy+=x*x;peak=fmaxf(peak,fabsf(x));}}
  assert(peak>0.0001f);printf("PASS model %d audible; peak %.6f RMS %.6f\n",m,peak,sqrt(energy/(690*256)));
 }
 md_init();md_command(MD_PLAY,0,1);render(300);unsigned w=md_status()->grain_writes;md_command(MD_FREEZE,0,1);render(300);assert(w==md_status()->grain_writes);md_command(MD_FREEZE,0,0);render(1);assert(md_status()->grain_writes>w);puts("PASS freeze stops tape writes / resume writes");
 md_patch before=*md_state();md_command(MD_MUTATE,0,0);render(1);assert(md_state()->seed!=before.seed);md_command(MD_RETURN,0,0);render(1);assert(!memcmp(&before,md_state(),sizeof before));puts("PASS mutation undo restores complete patch");
 // Scale contract: all pitches, all roots, bounded range, and idempotence.
 const unsigned masks[]={0xfff,0x5ad,0xab5,0x6ad,0x4a9,0x295,0x555,0x5ab,0x6cd,0x9ad,0x9cd,0x9b5,0x9b3};
 for(int sc=0;sc<13;sc++)for(int root=0;root<12;root++)for(int note=-24;note<=144;note++){
  int q=md_quantize(note,root,sc);assert(q>=24&&q<=96);assert(masks[sc]&(1u<<((q-root+12)%12)));assert(md_quantize(q,root,sc)==q);
 }puts("PASS all 13 scales / 12 roots / out-of-range notes / idempotence");
 md_init();assert(md_midi_route(0)==1&&md_midi_route(5)==6&&md_midi_macro_enabled());
 md_set_midi_route(0,11);md_set_midi_route(1,11);md_midi(0x9a,60,100,0);
 assert(md_status()->events[0]==1&&md_status()->events[1]==1);
 md_midi(0x90,60,100,0);assert(md_status()->events[0]==1);
 md_set_midi_route(0,0);md_midi(0x9a,62,100,2);
 assert(md_status()->events[0]==1&&md_status()->events[1]==2);
 md_set_midi_route(2,16);md_midi(0x9f,60,100,0);assert(md_status()->events[2]==1);
 int macro_before=md_state()->macro[0];md_set_midi_macro_enabled(0);md_midi(0xbf,16,127,0);assert(md_state()->macro[0]==macro_before);
 md_set_midi_macro_enabled(1);md_midi(0xbf,16,127,0);assert(md_state()->macro[0]==1000);
 md_command(MD_KIT_MODE,0,1);render(1);md_set_midi_route(3,10);md_midi(0x99,36,100,0);assert(md_status()->events[3]>0);
 md_init();assert(md_midi_route(0)==1&&md_midi_route(3)==4&&md_midi_macro_enabled());
 puts("PASS per-track MIDI routes: defaults, CH11 layers, Off, CH16 macros and kit CH10");
 // Old v1 data remains readable, with explicit neutral defaults for new features.
 struct OldPatch{uint32_t magic,version,seed;int32_t macro[4],tempo,locked,freeze,grain_size,grain_pos,feedback,quality;int32_t tracks[6][14];}old={};
 md_init();p=*md_state();memcpy(&old,&p,56);old.version=1;for(int i=0;i<6;i++)memcpy(old.tracks[i],&p.track[i],56);
 assert(md_import(&old,sizeof old));assert(md_state()->version==9&&md_state()->track[5].ratchet==1);assert(md_state()->duck_source==-1);old.tracks[0][7]=0;assert(!md_import(&old,sizeof old));
 puts("PASS v1 patch migration and corrupt legacy rejection");
 // Import the exact v2 layout, preserving its parameter values and migration shape.
 struct V2Patch{uint32_t magic,version,seed;int32_t head[11];int32_t tracks[6][17];int32_t globals[9];}v2={};
 md_init();p=*md_state();memcpy(&v2,&p,56);v2.version=2;memcpy(v2.globals,&p.root,36);
 for(int i=0;i<6;i++){memcpy(v2.tracks[i],&p.track[i],68);v2.tracks[i][0]=3;v2.tracks[i][3]=777;}
 assert(md_import(&v2,sizeof v2));assert(md_state()->track[5].shape==777&&md_state()->track[5].decay==777&&md_state()->version==9);
 v2.tracks[0][0]=99;assert(!md_import(&v2,sizeof v2));puts("PASS v2 patch migration / corrupt model rejection");

 // Exact v3 prefix is preserved; unknown names remain explicitly custom.
 md_init();p=*md_state();unsigned char v3[620];memcpy(v3,&p,620);((uint32_t*)v3)[1]=3;
 assert(md_import(v3,sizeof v3));assert(md_state()->version==9&&md_state()->biome_id==-1&&md_state()->tone_preset[0]==-1);
 assert(!memcmp((const unsigned char*)md_state()+8,v3+8,612));puts("PASS v3 migration preserves all 153 parameter words / marks custom names");
 // Every factory Biome must be valid, audible and genuinely different; Blank stays silent even after frozen tape.
 unsigned biome_hash[MD_BIOMES]={};
 for(int id=0;id<MD_BIOMES;id++){
  md_init();md_command(MD_SCENE,0,id);md_command(MD_PLAY,0,1);render(1);assert(md_validate(md_state()));assert(md_state()->biome_id==id);
  unsigned h=2166136261u;double energy=0;
  for(int b=0;b<4135;b++){md_render(out,128);for(float x:out){assert(std::isfinite(x));energy+=x*x;unsigned u;memcpy(&u,&x,4);h=(h^u)*16777619u;}}
  printf("Biome %d %s energy %.9g hash %08x\n",id,md_biome_name(id),energy,h);
  if(id==MD_BLANK_BIOME)assert(energy==0);else {assert(energy>1e-5);for(int old=0;old<id;old++)assert(biome_hash[old]!=h);}
  biome_hash[id]=h;
 }
 printf("PASS %d audible unique Biomes / 12 seconds each; Blank exactly silent\n",MD_BIOMES-1);
 for(int id=0;id<MD_TONE_PRESETS;id++){
  md_init();md_command(MD_SCENE,0,MD_BLANK_BIOME);render(1);md_track before=md_state()->track[0];md_command(MD_TONE_PRESET,0,id);render(1);const md_track &t=md_state()->track[0];
  assert(t.model==md_tone_model(id)&&md_state()->tone_preset[0]==id&&md_validate(md_state()));
  assert(t.hits==before.hits&&t.length==before.length&&t.rotate==before.rotate&&t.prob==before.prob&&t.ratchet==before.ratchet&&t.accent==before.accent&&t.speed==before.speed&&t.note==before.note&&t.pan==before.pan&&t.send==before.send&&t.level==before.level&&t.mute==before.mute);
  assert((md_state()->edited_mask&3)==1);md_command(MD_TRIGGER,0,md_tone_model(id)<3?40:60);double energy=0;
  for(int b=0;b<2757;b++){md_render(out,128);for(float x:out){assert(std::isfinite(x));energy+=x*x;}}
  if(energy<=1e-6){fprintf(stderr,"Silent preset %d %s energy %g\n",id,md_tone_name(id),energy);return 2;}
 }
 printf("PASS all %d tone presets audible / 8 seconds each; rhythm, notes, mix and mute preserved\n",MD_TONE_PRESETS);
 md_init();md_command(MD_PLAY,0,1);render(2500);md_command(MD_FREEZE,0,1);md_command(MD_SCENE,0,MD_BLANK_BIOME);render(1);assert(md_pending_biome()==MD_BLANK_BIOME);md_command(MD_SCENE_CONFIRM,0,1);render(1);
 for(int b=0;b<500;b++){md_render(out,128);for(float x:out)assert(x==0);}
 md_command(MD_TRIGGER,0,60);render(100);assert(md_status()->events[0]>0&&md_status()->peak>0);puts("PASS Blank clears previous frozen audio / keyboard still plays");
 // A low cutoff must remove high-frequency energy, not just turn everything down.
 double roughness[2]={};
 for(int filtered=0;filtered<2;filtered++){
  md_init();p=*md_state();p.macro[3]=0;
  for(int i=0;i<6;i++){p.track[i].model=MD_VA;p.track[i].mute=i!=0;p.track[i].tone=800;p.track[i].shape=900;p.track[i].filter=filtered?2:0;p.track[i].cutoff=350;p.track[i].resonance=0;p.track[i].breathe=0;p.track[i].attack=0;}
  assert(md_load(&p));md_command(MD_TRIGGER,0,60);render(100);double e=0,d=0;float last=0;
  for(int b=0;b<300;b++){md_render(out,128);for(int k=0;k<256;k+=2){e+=out[k]*out[k];d+=(out[k]-last)*(out[k]-last);last=out[k];}}
  assert(e>1e-6);roughness[filtered]=d/e;
 }
 assert(roughness[1]<roughness[0]*.2);printf("PASS LP24 reduces normalized HF energy: %.6f -> %.6f\n",roughness[0],roughness[1]);
 // New filters at extreme resonance and modulated cutoff, all eight engines / six slots.
 for(int m=0;m<MD_MODELS;m++)for(int mode=1;mode<=3;mode++){
  md_init();p=*md_state();p.quality=1;p.tempo=220;p.feedback=850;p.macro[3]=1000;
  for(int i=0;i<6;i++){p.track[i].model=m;p.track[i].filter=mode;p.track[i].resonance=1000;p.track[i].breathe=1000;p.track[i].cutoff=i%2?0:1000;p.track[i].hits=p.track[i].length;p.track[i].prob=1000;p.track[i].speed=1;p.track[i].ratchet=4;}
  assert(md_load(&p));md_command(MD_PLAY,0,1);render(3446);assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 }
 printf("PASS %d filter/engine combinations at extreme resonance, cutoff, repeats and feedback / 10s each\n",MD_MODELS*3);
 // A stopped, never-triggered rack must not spend time in the six DSP engines.
 md_init();render(100);assert(md_status()->voice_quanta==0&&md_status()->skipped_quanta>0);assert(md_status()->peak==0);
 puts("PASS inactive voice DSP skipped / exact startup silence");
 // Four repeats per event must result in exactly 64 attacks in a 16-step bar.
 for(int repeats=1;repeats<=4;repeats++){
  md_init();p=*md_state();p.tempo=120;p.swing=0;p.macro[0]=500;
  for(int i=0;i<6;i++){p.track[i].mute=i!=0;p.track[i].model=0;p.track[i].length=16;p.track[i].hits=16;p.track[i].speed=1;p.track[i].prob=1000;p.track[i].ratchet=repeats;}
  assert(md_load(&p));md_command(MD_PLAY,0,1);unsigned left=88200;while(left){unsigned n=left>128?128:left;md_render(out,n);left-=n;}
  assert(md_status()->events[0]==(unsigned)(16*repeats));md_command(MD_PLAY,0,0);render(1);unsigned events=md_status()->events[0];render(100);assert(md_status()->events[0]==events);
 }
 puts("PASS exact 1/2/3/4 ratchet counts / stop cancels pending attacks");
 md_init();p=*md_state();p.tempo=120;p.swing=600;p.macro[0]=500;
 for(int i=0;i<6;i++){p.track[i].mute=i!=0;p.track[i].model=0;p.track[i].length=16;p.track[i].hits=16;p.track[i].speed=1;p.track[i].prob=1000;}
 assert(md_load(&p));md_command(MD_PLAY,0,1);unsigned event=0,times[3]={};
 for(unsigned frame=0;event<3;frame++){md_render(out,1);if(md_status()->events[0]>event)times[event++]=frame;assert(frame<12000);}
 assert(times[0]==0&&times[1]>=7166&&times[1]<=7167&&times[2]==11025);
 puts("PASS swing 65:35 / pair duration unchanged to one sample");
 double attackenergy[2]={};
 for(int slow=0;slow<2;slow++){md_init();p=*md_state();p.macro[3]=0;for(int i=0;i<6;i++){p.track[i].mute=i!=0;p.track[i].model=MD_VA;p.track[i].attack=slow?1000:0;}assert(md_load(&p));md_command(MD_TRIGGER,0,60);for(int b=0;b<70;b++){md_render(out,128);for(float x:out)attackenergy[slow]+=x*x;}}
 assert(attackenergy[1]<attackenergy[0]*.01&&attackenergy[1]>0);puts("PASS 8-second attack attenuates first 200ms / remains audible");
 // Ducking never ducks its dry source; any other track can be the source.
 double duckenergy[3]={};
 for(int mode=0;mode<3;mode++){
  md_init();p=*md_state();p.macro[3]=0;p.duck_source=mode==2?5:0;p.duck_amount=mode?1000:0;p.duck_release=1000;
  for(int i=0;i<6;i++){p.track[i].model=MD_VA;p.track[i].attack=0;p.track[i].level=i==0?600:0;}
  assert(md_load(&p));md_command(MD_TRIGGER,0,60);md_command(MD_TRIGGER,5,60);
  for(int b=0;b<35;b++){md_render(out,128);for(float x:out)duckenergy[mode]+=x*x;}
 }
 assert(fabs(duckenergy[1]-duckenergy[0])<1e-8);assert(duckenergy[2]<duckenergy[0]*.1);
 puts("PASS duck source dry signal unchanged / track 6 can duck track 1");
 // Freeze populated tape and confirm direction/pitch change the actual audio.
 unsigned grainhash[3]={};
 for(int mode=0;mode<3;mode++){
  md_init();md_command(MD_PLAY,0,1);render(800);md_command(MD_PLAY,0,0);md_command(MD_FREEZE,0,1);
  for(int i=0;i<6;i++)md_command(MD_MUTE,i,1);
  md_command(MD_MACRO,3,1000);md_command(MD_REVERSE,0,mode==1);md_command(MD_GRAIN_PITCH,0,mode==2?12:0);
  render(200);unsigned h=2166136261u;double e=0;for(int b=0;b<200;b++){md_render(out,128);for(float x:out){unsigned v;memcpy(&v,&x,4);h=(h^v)*16777619u;e+=x*x;}}
  assert(e>.000001);grainhash[mode]=h;
 }
 assert(grainhash[0]!=grainhash[1]&&grainhash[0]!=grainhash[2]);puts("PASS frozen tape audible / reverse and octave change audio");
 for(int quality=0;quality<2;quality++)for(int m=0;m<MD_MODELS;m++){
  md_init();md_command(MD_SCENE,0,1);for(int t=0;t<6;t++){md_command(MD_MODEL,t,m);md_command(MD_HITS,t,16);md_command(MD_DECAY,t,1000);md_command(MD_PROB,t,1000);md_command(MD_SEND,t,1000);md_command(MD_NOTE,t,36+t*7);}
  render(1);for(int t=0;t<6;t++){md_command(MD_RATCHET,t,4);md_command(MD_ACCENT,t,1000);md_command(MD_ATTACK,t,quality?1000:0);}
  md_command(MD_GRAIN_PITCH,0,quality?24:-24);md_command(MD_REVERSE,0,quality);md_command(MD_DUCK_SOURCE,0,3);md_command(MD_DUCK,0,1000);md_command(MD_SWING,0,600);md_command(MD_QUALITY,0,quality);md_command(MD_FEEDBACK,0,850);md_command(MD_MACRO,3,1000);md_command(MD_TEMPO,0,220);md_command(MD_PLAY,0,1);
  auto start=std::chrono::steady_clock::now();render(20672);double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  assert(md_status()->nonfinite==0);assert(md_status()->dropped_commands==0);for(int t=0;t<6;t++){assert(md_state()->track[t].model==m);assert(md_status()->events[t]>100);}
  printf("PASS 60s six model %d requested quality %d actual %d; host CPU %.2f%% peak %.5f\n",m,quality,md_state()->quality,100*sec/60,md_status()->peak);
 }
 // The larger tape clears logically in constant time; stale samples must never leak after Panic.
 md_init();md_command(MD_PLAY,0,1);render(2500);md_command(MD_PANIC,0,0);md_command(MD_FREEZE,0,1);md_command(MD_MACRO,3,1000);
 for(int b=0;b<5000;b++){md_render(out,128);for(float x:out)assert(x==0.0f);}
 assert(md_memory_bytes()>250000);puts("PASS expanded tape: panic/freeze remains exactly silent for 14.5s / no stale audio");
 // ECO's reduced engine clock must retain concert pitch, including odd
 // fragments. A warm pulse can cross zero twice per cycle; measure its actual
 // fundamental instead of mistaking those extra crossings for an octave jump.
 for(int model: {MD_FM,MD_VA,MD_HARMONIC}){
  int measured[2]={};
  for(int quality=0;quality<2;quality++){
   md_init();p=*md_state();p.quality=quality;p.scale=0;p.macro[2]=500;p.macro[3]=0;p.fx_level=0;
   for(int i=0;i<6;i++){p.track[i].model=model;p.track[i].mute=i!=0;p.track[i].attack=0;p.track[i].decay=1000;p.track[i].filter=0;p.track[i].tone=0;p.track[i].harm=model==MD_VA?500:0;p.track[i].shape=model==MD_FM?500:model==MD_HARMONIC?1000:150;p.track[i].breathe=0;p.ext[i].tides=0;}
   assert(md_load(&p));md_command(MD_TRIGGER,0,69);render(100);unsigned at=0,part=0;
   const unsigned fragments[]={127,1,31,97};static float signal[44100];
   while(at<44100){unsigned n=fragments[part++%4];if(n>44100-at)n=44100-at;md_render(out,n);for(unsigned k=0;k<n;k++){float x=out[k*2];assert(std::isfinite(x));signal[at++]=x;}}
   auto energy=[&](double hz){double re=0,im=0;for(int j=0;j<44100;j++){double w=6.283185307179586*j/44100,x=signal[j]*(.5-.5*cos(w));re+=x*cos(w*hz);im-=x*sin(w*hz);}return re*re+im*im;};
   double peak=0;for(int hz=430;hz<=450;hz++){double e=energy(hz);if(e>peak){peak=e;measured[quality]=hz;}}
   assert(peak>1&&peak>16*energy(220)&&peak>energy(880));
  }
  printf("ECO/FULL fundamental model %d: %d/%d Hz\n",model,measured[0],measured[1]);
  assert(abs(measured[0]-measured[1])<=1);assert(measured[0]>=439&&measured[0]<=441);
 }
 puts("PASS ECO pitch compensation / odd fragment continuity");
 for(int voices=2;voices<=3;voices++){
  md_init();p=*md_state();p.quality=1;for(int i=0;i<6;i++){p.track[i].model=MD_RINGS;p.track[i].mute=i>=voices;p.track[i].hits=p.track[i].length;p.track[i].prob=1000;p.track[i].speed=1;}
  assert(md_load(&p));md_command(MD_PLAY,0,1);render(1);assert(md_state()->quality==(voices==2));assert(md_status()->quality_reductions==(unsigned)(voices==3));
 }
 puts("PASS FULL admission guard keeps 2 Rings / sends 3 Rings to ECO before render");
 for(int model : {MD_FM,MD_HARMONIC})for(int count : {4,5}){
  md_init();p=*md_state();p.quality=1;p.fx_level=0; // Explicitly test the dry engine budget, independent of factory defaults.
for(int i=0;i<6;i++){p.track[i].model=model;p.track[i].mute=i>=count;p.track[i].hits=p.track[i].length;p.track[i].prob=1000;p.track[i].speed=1;}
  assert(md_load(&p));md_command(MD_PLAY,0,1);render(1);assert(md_state()->quality==(count==4));assert(md_status()->quality_reductions==(unsigned)(count==5));
 }
 puts("PASS measured FULL limits: 4 FM/Harmonic stay FULL, 5 use ECO before render");
 for(int model : {MD_FM,MD_HARMONIC})for(int count : {3,4}){
  md_init();p=*md_state();p.quality=1;p.fx_level=500;
  for(int i=0;i<6;i++){p.track[i].model=model;p.track[i].mute=i>=count;p.track[i].hits=p.track[i].length;p.track[i].prob=1000;p.track[i].speed=1;}
  assert(md_load(&p));md_command(MD_PLAY,0,1);render(1);assert(md_state()->quality==(count==3));assert(md_status()->quality_reductions==(unsigned)(count==4));
 }
 puts("PASS FULL admission counts shared effects: 3 FM/Harmonic stay FULL, 4 use ECO");
 for(int kind=0;kind<2;kind++){
  md_init();p=*md_state();p.quality=1;const int techno[6]={0,2,1,3,5,0};
  for(int i=0;i<6;i++){p.track[i].model=kind?techno[i]:MD_SNARE;p.track[i].hits=p.track[i].length;p.track[i].prob=1000;p.track[i].speed=1;}
  assert(md_load(&p));md_command(MD_PLAY,0,1);render(1);assert(md_state()->quality==0&&md_status()->quality_reductions==1);
 }
 puts("PASS full-rate tape admission: six snares and dense techno use ECO before rendering");
 md_init();unsigned seed=5678;
 for(int i=0;i<10000;i++){seed=seed*1664525+1013904223;md_command(seed%MD_COMMAND_COUNT,(seed>>8)%6,(int)((seed>>16)%1800)-300);render(1);if(!md_validate(md_state())){const md_patch *p=md_state();fprintf(stderr,"INVALID after %d cmd %u t%u v%d\n",i,seed%MD_COMMAND_COUNT,(seed>>8)%6,(int)((seed>>16)%1800)-300);for(int t=0;t<6;t++){const md_track &a=p->track[t];fprintf(stderr,"t%d model%d len%d hits%d rot%d note%d speed%d mute%d\n",t,a.model,a.length,a.hits,a.rotate,a.note,a.speed,a.mute);}return 1;}}
 puts("PASS 10,000 boundary / all-command / reassignment commands");
 printf("DSP owned storage (incl conservative 512B globals): %u bytes\n",md_memory_bytes());
 puts("ALL DSP TESTS PASSED; host measurements are not FM1 performance measurements.");
}
