#include "dsp/dreams.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <vector>
static float lr[256];
static void run(unsigned frames){while(frames){unsigned n=frames>128?128:frames;md_render(lr,n);for(unsigned i=0;i<2*n;i++)assert(std::isfinite(lr[i])&&fabsf(lr[i])<1);frames-=n;}}
static void cmd(int c,int t,int v){assert(md_command(c,t,v));run(1);}
static md_patch base(){md_init();cmd(MD_SCENE,0,64);md_patch p=*md_state();for(auto &t:p.track)t.mute=1;auto &t=p.track[0];t.mute=0;t.model=MD_MELLOW;t.level=600;t.attack=100;t.decay=900;t.filter=1;t.cutoff=650;t.tone=400;t.note=48;t.speed=1;t.prob=1000;p.macro[3]=500;p.fx_level=600;p.ext[0].delay_send=500;p.ext[0].reverb_send=500;p.ext[0].warps=300;return p;}
static std::vector<float> audio(md_patch p,int seconds=2){md_init();assert(md_load(&p));md_midi(0x90,48,100,0);std::vector<float>a;for(int i=0;i<seconds*MD_RATE/128;i++){md_render(lr,128);for(float v:lr)assert(std::isfinite(v)&&fabsf(v)<1);a.insert(a.end(),lr,lr+256);}return a;}
int main(int argc,char **argv){
 static_assert(sizeof(md_patch)==2604,"v9 size");static_assert(offsetof(md_patch,motion)==2220,"v8 prefix");static_assert(MD_LFO2_TARGET==120&&MD_LFO2_RATE==123&&MD_MIDI_ROUTE==125&&MD_MIDI_MACRO==126&&MD_FILL==160&&MD_SEQ_EXPRESSION==161&&MD_ITINERA_START==162&&MD_COMMAND_COUNT==163,"protocol/UI boundary including retained MIDI routes, phrase expression and Itinera");
 auto p=base();unsigned char old[2220];memcpy(old,&p,2220);((uint32_t*)old)[1]=8;md_patch m;assert(md_decode(&m,old,sizeof old));assert(m.version==MD_PATCH_VERSION&&!memcmp((char*)&m+8,old+8,2212));md_motion zero={};assert(!memcmp(&m.motion,&zero,sizeof zero));assert(md_load(&m));
 if(argc==3){FILE*f=fopen(argv[1],"rb");assert(f);assert(fread(old,1,2220,f)==2220);fclose(f);assert(md_decode(&m,old,2220));f=fopen(argv[2],"wb");assert(f);assert(fwrite(&m,1,sizeof m,f)==sizeof m);fclose(f);}
 puts("PASS full v8 prefix migration; all motion additions default neutral");
 for(int n: {1,4,32})for(int d=0;d<5;d++)for(int shift=0;shift<32;shift++){
  p=base();p.sequence[0].length=n;p.motion.direction[0]=d;p.motion.shift[0]=shift;assert(md_load(&p));
  for(unsigned k=0;k<256;k++){unsigned got=md_sequence_position(0,k);assert(got<(unsigned)n);if(!d)assert(got==(k+shift)%n);if(d==1)assert(got==(n-1-k%n+shift)%n);if(d==2){unsigned at=n==1?0:k%(2*n-2);if(at>=(unsigned)n)at=2*n-2-at;assert(got==(at+shift)%n);}if(d==3)assert(got==(k%n+k/n+shift)%n);if(d==4)assert(got==md_sequence_position(0,k+n));}
 }
 puts("PASS all five paths / lengths 1, 4, 32 / every offset / seeded repeatability");
 p=base();p.sequence[0].length=4;p.motion.direction[0]=1;p.motion.shift[0]=1;assert(md_load(&p));cmd(MD_RECORD,0,2);cmd(MD_PLAY,0,1);md_midi(0x90,60,100,0);unsigned at=md_sequence_position(0,(md_status()->step?md_status()->step-1:0));assert(md_state()->sequence[0].steps[at].note==60);cmd(MD_PLAY,0,0);puts("PASS live recording uses the sounding, remapped step");
 for(int sc=0;sc<8;sc++)for(int g=0;g<8;g++){
  p=base();p.scale=sc;assert(md_load(&p));cmd(MD_SEQ_GENERATE,0,g);auto q=md_state()->sequence[0];for(auto &s:q.steps)if(s.note)assert(md_quantize(s.note,p.root,p.scale)==s.note);assert(!memcmp(md_state()->sequence+1,p.sequence+1,5*sizeof(md_sequence)));cmd(MD_SEQ_GENERATE,0,g);assert(!memcmp(&q,&md_state()->sequence[0],sizeof q));cmd(MD_RETURN,0,0);assert(!memcmp(&q,&md_state()->sequence[0],sizeof q));
 }
 puts("PASS eight generators in all eight scales / deterministic / isolated / undo");
 for(int journey=0;journey<=8;journey++)for(int bars: {1,16})for(int compact=0;compact<2;compact++){
  p=base();p.chord_pattern=journey;p.chord_bars=bars;p.motion.compact=compact;p.chord_tracks=1;assert(md_load(&p));for(int pos=0;pos<8;pos++)assert(md_chord_degree(journey,pos)>=0&&md_chord_degree(journey,pos)<=6);cmd(MD_PLAY,0,1);run(1024);cmd(MD_PLAY,0,0);
 }
 puts("PASS eight journeys / 1–16 bars / compact roots / bounded output");
 p=base();p.ext[0].tides=0;auto neutral=audio(p);
 for(int target=0;target<7;target++)for(int wave=0;wave<4;wave++){p.motion.target[0]=target;p.motion.wave[0]=wave;assert(audio(p)==neutral);}
 puts("PASS depth zero is bit-identical across every routing and curve");
 p=base();p.ext[0].tides=950;p.ext[0].tides_rate=40;auto reference=audio(p,3);
 for(int target=0;target<7;target++)for(int wave=0;wave<4;wave++){
  p.motion.target[0]=target;p.motion.wave[0]=wave;auto a=audio(p,3);double energy=0,diff=0;for(size_t k=0;k<a.size();k++){energy+=a[k]*a[k];diff+=fabs(a[k]-reference[k]);}assert(energy>.001);if(target||wave)assert(diff>.01);
 }
 p=base();p.ext[0].tides=0;neutral=audio(p);p.motion.target[1]=6;p.motion.wave[1]=3;p.ext[1].tides=1000;assert(audio(p)==neutral);
 puts("PASS 28 LFO combinations audible and finite; changes isolated to target track");
 // The second LFO must be independently audible and stay neutral at zero depth.
 p=base();neutral=audio(p);p.motion.target2[0]=6;p.motion.wave2[0]=3;p.motion.rate2[0]=50;assert(audio(p)==neutral);
 for(int target=0;target<7;target++)for(int wave=0;wave<4;wave++){
  p.motion.target2[0]=target;p.motion.wave2[0]=wave;p.motion.depth2[0]=800;auto a=audio(p);assert(a!=neutral);
 }
 p=base();p.ext[0].tides=700;p.motion.target[0]=1;p.motion.wave[0]=2;p.motion.target2[0]=2;p.motion.wave2[0]=3;p.motion.depth2[0]=700;p.motion.rate2[0]=500;auto both=audio(p);p.motion.depth2[0]=0;assert(audio(p)!=both);
 puts("PASS both LFOs independent; second 28 routes/curves audible; disabled adds no sound change");
 p=base();p.motion.depth2[0]=1001;assert(!md_validate(&p));p=base();p.motion.rate2[0]=1001;assert(!md_validate(&p));p=base();p.motion.target2[0]=7;assert(!md_validate(&p));p=base();p.motion.wave2[0]=4;assert(!md_validate(&p));
 p=base();p.motion.reserved=1;assert(!md_validate(&p));p=base();p.motion.target[2]=7;assert(!md_validate(&p));p=base();p.motion.wave[2]=4;assert(!md_validate(&p));p=base();p.motion.direction[2]=5;assert(!md_validate(&p));p=base();p.motion.feel[2]=101;assert(!md_validate(&p));p=base();p.motion.shift[2]=32;assert(!md_validate(&p));puts("PASS invalid motion storage rejected");
 p=base();p.sequence[0].enabled=1;p.sequence[0].length=4;for(auto &s:p.sequence[0].steps)s={48,60,100,85};p.motion.feel[0]=100;md_init();assert(md_load(&p));cmd(MD_PLAY,0,1);run(MD_RATE*8);assert(!memcmp(md_state()->sequence,p.sequence,sizeof p.sequence));cmd(MD_PLAY,0,0);assert(md_import(&p,sizeof p));assert(!memcmp(md_state(),&p,sizeof p));puts("PASS feel preserves stored notes and velocity; patch round trip retains every new setting");
}
