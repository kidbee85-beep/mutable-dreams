// Measuring tree: the stage profile (MD_PROFILE) on the host, the way app_render calls it: one callback = md_block_begin,
// TWO md_render calls of 128 frames (app_render(256) splits so), md_prof_end. Prints the peak callbacks as md_prof_read
// returns them. Host time: the numbers check the plumbing (sums, 256 frames, flags, ranks), not the FM-1's cost.
//   profile [preset|-1 plain play] [seconds] [air] [chorus] [vintage]      exit 1 if the checks fail
#define MD_PROFILE 1
#include "dsp/dreams.cpp"
#include <cstdio>
#include <cstdlib>
#include <time.h>
#ifndef MD_MODELS_MAX_
#define MD_MODELS_MAX_ 32
#endif
extern "C" uint32_t plat_ticks24(void){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint32_t)(t.tv_sec*24000000ull+t.tv_nsec*24ull/1000ull);}
int main(int argc,char**argv){
 const int preset=argc>1?atoi(argv[1]):8;const int seconds=argc>2?atoi(argv[2]):120;
 const int air=argc>3?atoi(argv[3]):3,chorus=argc>4?atoi(argv[4]):1,vintage=argc>5?atoi(argv[5]):2;
 md_init();md_set_air(air);md_set_chorus(chorus);md_set_vintage(vintage);
 if(preset>=0)md_command(MD_ITINERA_START,preset,(1)+36*37);   // settings 1 + code 37
 else md_command(MD_PLAY,0,1);
 static float lr[512];unsigned frames=0;const unsigned total=seconds*44100u;
 while(frames<total){md_block_begin();md_render(lr,128);md_render(lr+256,128);md_prof_end();frames+=256;}
 uint32_t w[56];int fails=0;
 const unsigned want_flags=(chorus?1u:0u)|(air?2u:0u)|(vintage?4u:0u);
 for(unsigned page=0;page<2;page++){
  md_prof_read(w,page);
  if(page==0){printf("blocks %u over90 %u overhead %u ticks, ranks %u\n",w[1],w[2],w[3],w[4]);if(w[1]!=frames/256)fails++;}
  for(int r=0;r<2;r++){const uint32_t *v=w+6+r*21;if(!v[0])continue;
   const uint32_t sum=v[1]+v[3]+v[4]+v[5]+v[6]+v[7];   // ctl+pre+vox+misc+fx+fin
   const unsigned fl=(v[14]>>17)&7,it=(v[14]>>20)&1,fr=v[15]&511;
   printf("rank %u: total %.1f us  ctl %.1f (tick %.1f) pre %.1f vox %.1f misc %.1f fx %.1f fin %.1f | scene %u q%u fx%u itinera%u | frames %u mask %x quanta %u | step %u part %u tempo %.1f reads %u\n",
    page*2+r,v[0]/24.f,v[1]/24.f,v[2]/24.f,v[3]/24.f,v[4]/24.f,v[5]/24.f,v[6]/24.f,v[7]/24.f,v[14]&0xffff,(v[14]>>16)&1,fl,it,fr,(v[15]>>9)&63,(v[15]>>15)&255,v[16],v[17],v[18]/10.f,v[19]);
   {const uint32_t mo=v[20];int ok=1;for(int i=0;i<6;i++)if(((mo>>(5*i))&31)>=MD_MODELS_MAX_)ok=0;if(!ok)fails++;}
   if(sum>v[0]+48)fails++;                      // the stages add up to no more than the callback (plus 2 us)
   if(fr!=256||((v[15]>>15)&255)<2)fails++;     // the whole 256-frame callback, two or more quanta
   if(fl!=want_flags)fails++;                   // bool flags, not the raw AIR/VINTAGE levels
   if(it!=(preset>=0?1u:0u))fails++;            // bit 20 is the dream flag, not an effect level
  }
 }
 // ablation: the same blocks with echo, room, grains, width, chorus, air and trim switched off must cost less in the effects stage (host time)
 {uint32_t a[40];double fx[2];
  for(int m=0;m<2;m++){
   md_prof_reset();md_prof_avg(a,m?(1|2|4|16|32|64|128):0,1);md_prof_reset();
   frames=0;while(frames<44100u*20){md_block_begin();md_render(lr,128);md_render(lr+256,128);md_prof_end();frames+=256;}
   const int n=md_prof_avg(a,0,0);
   fx[m]=(double)a[3+2*5]/(double)a[1]/24.0;
   printf("ablate %u: blocks %u  fx %.1f us  fin %.1f us  (words %d)\n",a[0],a[1],fx[m],(double)a[3+2*6]/a[1]/24.0,n);
  }
  md_prof_avg(a,0,1);
  if(!(fx[1]<fx[0]*0.8))fails++;
 }
 puts(fails?"FAIL":"PASS profile plumbing (256-frame callback, flags)");return fails?1:0;
}
