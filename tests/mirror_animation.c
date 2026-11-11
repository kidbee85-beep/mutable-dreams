/* Exact base encoder and decoder, with the private 2.0 memory settings. */
#define MR_SEGW 240u
#define MRQ 2048u
#define main mirror_base_main
#include "../base/tests/mirror_test.c"
#undef main
int main(void){
 unsigned ever[240]={0};ring_cap=MRQ;on();
 for(unsigned frame=0;frame<400;frame++){
  for(unsigned y=0;y<240;y++)for(unsigned x=0;x<240;x++)shown[y*240+x]=(uint16_t)(1+(x*13+y*113+frame)%65534);
  for(unsigned y=0;y<240;y+=40)lcd_blit(0,y,240,40,shown+y*240);
  drain();decode();now+=58;
  if(frame%20==0)mirror_cmd(1);
  for(unsigned y=0;y<240;y++)if(view[y*240])ever[y]=1;
 }
 unsigned covered=0;for(unsigned y=0;y<240;y++)covered+=ever[y];
 printf("Animated full-width screen: %u / 240 rows delivered at least once; %u bad frames\n",covered,dec_bad);
 if(covered!=240||dec_bad)return 1;
 mirror_cmd(0);unsigned sent=mr.frames;blit_all();if(mr.frames!=sent)return 1;
 puts("PASS animation cannot starve the initial full-screen seed; no frames while viewer is off");return 0;
}
