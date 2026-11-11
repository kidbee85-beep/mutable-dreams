#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <initializer_list>
static float audio[256];
static double run(unsigned n,FILE *f=0){double energy=0;while(n){unsigned q=n>128?128:n;md_render(audio,q);for(unsigned i=0;i<q*2;i++){assert(std::isfinite(audio[i])&&fabsf(audio[i])<1);energy+=audio[i]*audio[i];}if(f)assert(fwrite(audio,8,q,f)==q);n-=q;}return energy;}
static md_patch dry(){md_init();md_patch p=*md_state();p.journey.mode=0;p.kit.enabled=0;p.fx_level=0;p.macro[1]=p.macro[3]=0;for(int i=0;i<6;i++){p.track[i].mute=i!=0;p.track[i].send=0;p.ext[i].delay_send=p.ext[i].reverb_send=0;}p.track[0].model=MD_SKIN;p.track[0].level=700;p.track[0].filter=0;p.track[0].decay=600;p.track[0].tone=500;p.track[0].harm=300;p.track[0].breathe=0;return p;}
int main(int argc,char**argv){
 static_assert(sizeof(md_patch)==2604&&MD_MODELS>=18&&MD_BIOMES>=197&&MD_TONE_PRESETS>=420,"additive catalog, same save format");
 for(int note:{24,36,48,60,72,84,96}){
  auto p=dry();assert(md_load(&p));md_midi(0x90,note,100,0);assert(run(MD_RATE)>.01);md_midi(0x80,note,0,0);run(MD_RATE*4);assert(run(MD_RATE/4)<1e-7);
  md_command(MD_PANIC,0,0);assert(run(512)==0);
 }
 puts("PASS hand drums: seven registers, audible stereo, finite output, decaying tails and exact panic silence.");
 double energy[2];for(int j=0;j<2;j++){auto p=dry();assert(md_load(&p));md_midi(0x90,60,j?127:32,0);energy[j]=run(MD_RATE);}assert(energy[1]>energy[0]*4);
 std::vector<float>a[2];for(int j=0;j<2;j++){auto p=dry();assert(md_load(&p));md_midi(0x90,60,100,0);run(128);if(j)md_midi(0x80,60,0,0);a[j].resize(44100*2);md_render(a[j].data(),44100);}assert(a[0]==a[1]);
 puts("PASS MIDI touch changes level; releasing a key preserves a naturally decaying drum hit.");
 for(int id=189;id<197;id++){
  md_init();md_command(MD_SCENE,0,id);run(1);assert(md_state()->biome_id==id&&md_validate(md_state()));auto p=*md_state();md_patch decoded;assert(md_decode(&decoded,&p,sizeof p)&&!memcmp(&decoded,&p,sizeof p));
  FILE *f=0;if(argc>1){char path[1024];snprintf(path,sizeof path,"%s/biome-%d.f32",argv[1],id);f=fopen(path,"wb");assert(f);}md_command(MD_PLAY,0,1);assert(run(MD_RATE*16,f)>.1);if(f)fclose(f);
 }
 for(int id=404;id<420;id++){auto p=dry();assert(md_load(&p));md_command(MD_TONE_PRESET,0,id);run(1);assert(md_state()->track[0].model==MD_SKIN);md_midi(0x90,60,100,0);assert(run(MD_RATE)>.001);}
 puts("PASS eight distinct authored hand-drum worlds, 16 tone recalls, canonical save roundtrips and shared FX rendering.");
 auto p=dry();p.tempo=220;p.macro[3]=1000;p.grain_size=1000;p.feedback=850;p.fx_level=650;
 for(int i=0;i<6;i++){p.track[i]=p.track[0];p.track[i].mute=0;p.track[i].length=p.track[i].hits=16;p.track[i].ratchet=4;p.track[i].note=36+i*7;p.track[i].decay=1000;p.track[i].send=700;}
 assert(md_load(&p));md_command(MD_PLAY,0,1);run(MD_RATE*20);assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 puts("PASS six drums, 220 BPM / four repeats, maximum tail and long granular feedback: finite output (offline CPU is not device headroom).");
}
