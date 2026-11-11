// Full-rate, both-channel bitstream for independent SHA256 regression checks.
#include "dsp/dreams.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstdlib>
int main(int argc,char **argv){
 const int first=argc>1?std::atoi(argv[1]):0;
 const int seconds=argc>2?std::atoi(argv[2]):12;
 const bool queue_fill=argc>3&&std::atoi(argv[3])!=0;
 if(first<0||first>=MD_BIOMES||seconds<1||seconds>180)return 2;
 float lr[256];const unsigned frames=MD_RATE*seconds;
 for(int id=first;id<MD_BIOMES;id++){
  md_init();assert(md_command(MD_SCENE,0,id));md_render(lr,128);assert(md_state()->biome_id==id);
  if(id>=MD_SOFT_FIRST_BIOME){
   md_patch snapshot=*md_state();
   md_patch restored;
   assert(md_decode(&restored,&snapshot,sizeof snapshot)&&md_load(&restored));
  }
  if(id>=MD_RULE_FIRST_BIOME)assert(md_state()->quality==1&&!md_status()->quality_reductions);
  assert(md_command(MD_PLAY,0,1));
  bool armed=false;
  for(unsigned at=0;at<frames;at+=128){
   if(queue_fill&&!armed&&md_status()->step>=16){assert(md_command(MD_FILL,0,1));armed=true;}
   unsigned n=frames-at<128?frames-at:128;md_render(lr,n);
   for(unsigned k=0;k<n*2;k++)assert(std::isfinite(lr[k])&&fabsf(lr[k])<1);
   assert(fwrite(lr,8,n,stdout)==n);
  }
  if(id%16==0)fprintf(stderr,"Full stereo: %d/%d\n",id+1,MD_BIOMES);
 }
}
