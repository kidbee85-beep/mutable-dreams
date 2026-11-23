#include "dsp/dreams.cpp"
#include <cstdio>
#include <cstdlib>
int main(int argc,char**argv){
 const unsigned secs=argc>1?atoi(argv[1]):4;uint64_t all=1469598103934665603ull;
 for(int id=0;id<MD_BIOMES;id++){
  md_init();md_set_air(3);md_set_chorus(1);md_set_vintage(2);md_set_width(3);
  md_command(MD_SCENE,0,id);md_command(MD_PLAY,0,1);
  static float lr[256];uint64_t h=1469598103934665603ull;
  for(unsigned f=0;f<44100u*secs;f+=128){md_render(lr,128);for(int i=0;i<256;i++){uint32_t u;memcpy(&u,&lr[i],4);h=(h^u)*1099511628211ull;}}
  printf("%d %016llx\n",id,(unsigned long long)h);all=(all^h)*1099511628211ull;
 }
 printf("ALL %016llx\n",(unsigned long long)all);
}
