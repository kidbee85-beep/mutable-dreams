#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <initializer_list>
static float audio[256];
static double run(int blocks){double e=0;for(int b=0;b<blocks;b++){md_render(audio,128);for(float x:audio){assert(std::isfinite(x)&&fabsf(x)<1);e+=x*x;}}return e;}
static void cmd(int c,int t,int v){assert(md_command(c,t,v));run(1);if(c==MD_SCENE&&md_pending_biome()>=0){assert(md_command(MD_SCENE_CONFIRM,0,1));run(1);}}
static md_patch blank(int model){md_init();cmd(MD_SCENE,0,64);md_patch p=*md_state();p.track[0].mute=0;p.track[0].model=model;p.track[0].note=36;p.track[0].level=650;p.track[0].decay=200;p.track[0].attack=0;p.track[0].hits=16;p.track[0].length=16;p.track[0].prob=1000;p.macro[0]=500;p.macro[1]=0;p.macro[2]=500;p.macro[3]=0;return p;}
static uint64_t hash(const md_patch &p){md_init();assert(md_load(&p));cmd(MD_PLAY,0,1);uint64_t h=1469598103934665603ull;for(int b=0;b<2200;b++){md_render(audio,128);for(float x:audio){uint32_t u;memcpy(&u,&x,4);h=(h^u)*1099511628211ull;}}return h;}
int main(){
 static_assert(sizeof(md_patch)==2604,"Browser patch size must match");
 auto p=blank(MD_BASS);assert(md_load(&p));cmd(MD_TRIGGER,0,36);assert(run(300)>1);puts("PASS round bass audible");
 uint8_t legacy[652];memcpy(legacy,&p,652);uint32_t v=4;memcpy(legacy+4,&v,4);assert(md_import(legacy,sizeof legacy));assert(md_state()->version==MD_PATCH_VERSION&&md_state()->fx_level==0);puts("PASS 1.0 migration retains old fields and disables additions");
 p=*md_state();p.fx_level=1001;assert(!md_load(&p));p=*md_state();p.ext[0].notes[15]=25;assert(!md_load(&p));
 p=blank(MD_BASS);p.ext[0].bass_mode=1;for(int &n:p.ext[0].notes)n=-1;assert(md_load(&p));cmd(MD_PLAY,0,1);assert(run(3000)==0&&md_status()->events[0]==0);puts("PASS phrase rests do not trigger");
 p=blank(MD_BASS);p.ext[0].bass_mode=1;p.ext[0].marbles=1000;p.locked=1;assert(hash(p)==hash(p));auto a=hash(p);p.ext[0].marbles=0;assert(a!=hash(p));puts("PASS Marbles remembered variation deterministic and audible");
 p=blank(MD_VA);p.track[0].filter=1;p.track[0].cutoff=500;a=hash(p);p.ext[0].tides=800;assert(a!=hash(p));puts("PASS Tides changes filtered audio");
 p=blank(MD_BASS);a=hash(p);p.ext[0].warps=800;assert(a!=hash(p));puts("PASS Warps ring blend changes audio");
 p=blank(MD_BASS);p.track[0].decay=0;p.ext[0].delay_send=800;p.ext[0].reverb_send=800;p.fx_level=800;p.delay_fb=800;p.reverb_time=850;md_init();assert(md_load(&p));cmd(MD_TRIGGER,0,36);run(300);assert(run(800)>0.001);cmd(MD_PANIC,0,0);assert(run(1000)==0);puts("PASS shared effect tails / panic exact silence");
 for(int quality=0;quality<=1;quality++)for(int model: {MD_BASS,MD_ELEMENTS,MD_VA,MD_RINGS}){p=blank(model);for(int i=0;i<6;i++){p.track[i]=p.track[0];p.ext[i].bass_mode=1;p.ext[i].delay_send=800;p.ext[i].reverb_send=800;p.ext[i].tides=600;p.ext[i].warps=500;p.ext[i].marbles=800;p.ext[i].glide=400;}p.quality=quality;p.fx_level=750;p.delay_fb=800;p.reverb_time=850;p.tempo=220;md_init();assert(md_load(&p));cmd(MD_PLAY,0,1);for(int j=0;j<90;j++){cmd(MD_DELAY_TIME,0,j%2?40:350);cmd(MD_GRAIN_SIZE,0,(j*73)%1001);run(230);}assert(!md_status()->nonfinite);printf("PASS 60-second feature stress model %d\n",model);}
 p=blank(MD_MELLOW);assert(md_load(&p));md_midi(0x90,48,127,0);assert(run(100)>1);md_midi(0x80,48,0,0);run(100);assert(run(300)<.0001);puts("PASS MIDI note off releases Mellow");
 for(int note: {24,48,72,96}){p=blank(MD_SATIN);p.track[0].note=note;p.track[0].tone=950;p.track[0].harm=1000;p.track[0].attack=0;assert(md_load(&p));md_midi(0x90,note,110,0);assert(run(100)>0.01);for(int h=0;h<=1000;h+=10){cmd(MD_HARM,0,h);run(3);}md_midi(0x80,note,0,0);run(100);assert(run(300)<.0001);}puts("PASS Satin morph / high and low notes / MIDI release");
 p=blank(MD_MELLOW);assert(md_load(&p));md_midi(0x90,48,100,0);run(10);md_midi(0xb0,64,127,0);md_midi(0x90,48,0,0);assert(run(100)>1);md_midi(0xb0,64,0,0);run(100);assert(run(300)<.0001);puts("PASS sustain and velocity-zero note off");
 p=blank(MD_MELLOW);assert(md_load(&p));md_midi(0x90,48,40,0);double quiet=run(100);md_init();assert(md_load(&p));md_midi(0x90,48,120,0);double loud=run(100);assert(loud>quiet*7&&loud<quiet*11);md_midi(0xb0,74,32,0);assert(md_state()->track[0].cutoff==32*1000/127);puts("PASS velocity energy and MIDI CC routing");
 p=blank(MD_BASS);p.track[0].hits=0;p.ext[0].bass_mode=2;p.track[0].length=4;for(int &v:p.ext[0].notes)v=-1;p.ext[0].notes[0]=7;assert(md_load(&p));cmd(MD_PLAY,0,1);run(1000);assert(md_status()->events[0]>0&&md_status()->last_note[0]==43);puts("PASS independent Step sequence with Euclidean hits zero");
 md_init();cmd(MD_CLOCK_SOURCE,0,1);md_midi(0xfa,0,0,2);assert(!md_playing());md_midi(0xfa,0,0,0);assert(md_playing());md_midi(0xf8,0,0,0);assert(md_status()->step==0);for(int k=0;k<6;k++)md_midi(0xf8,0,0,0);assert(md_status()->step==1);run(800);assert(!md_playing());puts("PASS selected external clock and loss timeout");
 p=blank(MD_ELEMENTS);for(int i=0;i<6;i++)p.track[i]=p.track[0];assert(md_load(&p));cmd(MD_PLAY,0,1);run(10);assert(md_status()->element_modes==2);for(int i=0;i<6;i++)assert(md_status()->events[i]>0);puts("PASS shared Elements budget keeps six tracks active");
 md_init();md_midi(0xb0,0,1,0);md_midi(0xc0,32,0,0);assert(md_state()->tone_preset[0]==160);md_midi(0xc0,33,0,0);assert(md_state()->tone_preset[0]==161);md_midi(0xbf,16,100,0);assert(md_state()->macro[0]==100*1000/127);puts("PASS program bank retained and global channel macros");
 // Rapid plucks must contain the main impulse, not only the negative primer.
 for(int shape: {334,500,666}){
  p=blank(MD_ELEMENTS);p.track[0].shape=shape;p.track[0].harm=280;
  p.track[0].tone=400;p.track[0].decay=700;p.track[0].ratchet=4;p.tempo=220;
  p.track[0].filter=0;p.macro[3]=0;p.fx_level=0;md_init();assert(md_load(&p));
  cmd(MD_PLAY,0,1);double energy=run(2000);
  printf("Elements rapid pluck shape %d energy %.9f\n",shape,energy);fflush(stdout);
  // Missing main pulses measured 3.46 here; intact plucks exceed 1300.
  assert(energy>100);assert(md_status()->nonfinite==0);
 }
 puts("PASS rapid Plectrum rolls remain audible across the complete shape region");
 puts("ALL NEXT FEATURE TESTS PASSED; no hardware-performance claim");
}
