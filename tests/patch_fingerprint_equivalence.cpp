#include "dsp/dreams.cpp"
#include <cstdio>
int main(){
 md_init();md_patch q;uint32_t h[2];
 printf("counts,%u,%u\n",(unsigned)MD_BIOMES,(unsigned)sizeof(md_patch));
 for(int id=0;id<MD_BIOMES;id++){factory_scene(q,id);patch_hash_of(q,h);printf("factory,%d,%08x,%08x\n",id,h[0],h[1]);}
 const md_patch base=*md_state();
 for(unsigned at=0;at<sizeof(md_patch);at++){
  q=base;reinterpret_cast<unsigned char*>(&q)[at]^=0x81;patch_hash_of(q,h);
  printf("byte,%u,%08x,%08x\n",at,h[0],h[1]);
 }
}
