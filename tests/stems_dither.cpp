/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * K-6: the six USB stems are dithered (triangular, one LSB) and rounded; the
 * stereo mix is the same with or without stems. */
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static float lr[512];
// One long, quiet, decaying note on track 0; the other tracks muted, the
// sequencer stopped. Returns the mix's hash; *tail gets track 0's stem.
static uint64_t run(bool stems,std::vector<int16_t> *tail){
 md_init();
 assert(md_command(MD_SCENE,0,244));md_render(lr,1);
 for(int i=1;i<6;i++)assert(md_command(MD_MUTE,i,1));
 assert(md_command(MD_MODEL,0,MD_MELLOW));assert(md_command(MD_ATTACK,0,0));
 assert(md_command(MD_DECAY,0,1000));assert(md_command(MD_LEVEL,0,40));
 assert(md_command(MD_SEND,0,0));assert(md_command(MD_DELAY_SEND,0,0));assert(md_command(MD_REVERB_SEND,0,0));
 md_render(lr,1);
 assert(md_command(MD_TRIGGER,0,60));
 static int16_t buf[256*8];uint64_t h=1469598103934665603ull;
 for(unsigned b=0;b<180u*44100u/256u;b++){
  memset(buf,0,sizeof buf);
  if(stems)md_render_stems(lr,256,{buf,buf,256});else md_render(lr,256);
  const uint8_t *p=(const uint8_t*)lr;for(unsigned j=0;j<sizeof lr;j++)h=(h^p[j])*1099511628211ull;
  if(tail)for(unsigned k=0;k<256;k++)tail->push_back(buf[k*8]);
 }
 return h;
}
int main(){
 std::vector<int16_t> tail;
 const uint64_t with=run(true,&tail),without=run(false,nullptr);
 assert(with==without);
 // Where the note has faded below about one LSB (a second in which no stem
 // value exceeds 1): truncation gives a run of exact zeros there, the dither
 // a symmetric +-1 noise around zero until the voice ends.
 size_t at=0;
 for(size_t i=0;i+44100<tail.size();i+=441){
  bool quiet=true;for(size_t j=i;j<i+44100&&quiet;j++)quiet=tail[j]>=-1&&tail[j]<=1;
  if(quiet){at=i;break;}
 }
 assert(at);
 long sum=0;unsigned nonzero=0;
 for(size_t j=at;j<at+44100;j++){sum+=tail[j];if(tail[j])nonzero++;}
 const double share=nonzero/44100.0,mean=sum/44100.0;
 printf("stems: tail below one LSB from %.1f s: %.0f%% of samples dithered, mean %.4f LSB\n",at/44100.0,share*100,mean);
 assert(share>.15&&fabs(mean)<.05); // (TPDF around zero: about a quarter of samples round to +-1)
 printf("PASS stems: the mix identical with and without them; a fading stem ends in +-1 LSB triangular dither, not in truncated zeros.\n");
 return 0;
}
