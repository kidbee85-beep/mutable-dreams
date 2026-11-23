/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Independent host acceptance for Grain Size/Position LFO routes.
 * No device timing or subjective listening claim. */
#ifdef NDEBUG
#error "Grain LFO acceptance requires active checks"
#endif
#include "dsp/dreams.cpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
static unsigned checks=0,zero_cases=0,spawn_cases=0,continuity_cases=0,bounds_frames=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static float clamp01(float x){return x<0?0:x>1?1:x;}
static float clamp11(float x){return x< -1?-1:x>1?1:x;}
static uint32_t next_random(uint32_t &v){v^=v<<13;v^=v>>17;v^=v<<5;return v;}
static float expected_noise(uint32_t &v){return (next_random(v)>>8)*(1.0f/8388608.0f)-1.0f;}
static void qrender(float *out,unsigned n){
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH==8
 quantum(out,n,{nullptr,nullptr,0},0);
#else
 quantum(out,n);
#endif
}
static void setup(int freeze=1,int quality=1,unsigned active=1){
 md_init();md_patch p=*md_state();p.kit.enabled=0;p.journey.mode=0;p.drift.mode=0;p.drift.depth=0;
 for(auto &l:p.links)l.depth=0;
 p.freeze=freeze;p.quality=quality;p.grain_size=500;p.grain_pos=500;p.feedback=0;p.grain_noise=0;p.fx_level=0;
 p.duck_amount=0;p.grain_pitch=0;p.reverse=0;p.macro[3]=1000;
 for(unsigned i=0;i<6;i++){
  p.track[i].model=MD_HARMONIC;p.track[i].mute=1;p.track[i].level=0;p.track[i].send=0;p.track[i].breathe=0;
  p.ext[i].tides=0;p.ext[i].tides_rate=500;p.ext[i].warps=0;p.ext[i].marbles=0;p.ext[i].delay_send=0;p.ext[i].reverb_send=0;
  p.motion.target[i]=0;p.motion.target2[i]=0;p.motion.wave[i]=0;p.motion.wave2[i]=0;p.motion.depth2[i]=0;p.motion.rate2[i]=500;
 }
 CHECK(md_validate(&p));CHECK(md_load(&p));
 for(unsigned i=0;i<6;i++){
  voices[i].active=(active>>i)&1;voices[i].trigger=0;voices[i].env=1;voices[i].attacking=0;voices[i].note=voices[i].target_note=60;
  voices[i].velocity=1;voices[i].lpg=1;voices[i].tide_phase=.173f+i*.071f;voices[i].tide2_phase=.713f-i*.053f;
 }
 for(unsigned i=0;i<GN;i++)tape[i]=(int16_t)(17000*std::sin(i*.01413)+11000*std::sin(i*.00219));
 tape_valid=GN;writehead=98304;grainclock=0;std::memset(grains,0,sizeof grains);
 rng=0x13579bdfu;seqrng=0x2468ace1u;stem_rng=0x23456789u;stmlib::Random::Seed(0x7654321u);
 for(unsigned j=0;j<4;j++)smooth[j]=unit(patch.macro[j]);md_mark_saved();
}
static void append(std::vector<unsigned char>&v,const void *p,size_t n){const auto b=(const unsigned char*)p;v.insert(v.end(),b,b+n);}
static std::vector<unsigned char> trace(unsigned frames){
 std::vector<unsigned char> v;float out[128];
 for(unsigned at=0;at<frames;){const unsigned n=std::min(64u,frames-at);qrender(out,n);append(v,out,n*2*sizeof(float));at+=n;}
 append(v,grains,sizeof grains);append(v,tape,sizeof tape);append(v,&stats,sizeof stats);
 const uint32_t state[]={rng,seqrng,stem_rng,stmlib::Random::state(),writehead,grainclock,tape_valid};append(v,state,sizeof state);
 for(unsigned i=0;i<6;i++){const Voice &a=voices[i];const float x[]={a.tide_phase,a.tide2_phase,a.amp,a.env,a.fade,a.pan_smooth,a.grain_send_smooth};append(v,x,sizeof x);append(v,&a.filter1,sizeof a.filter1);append(v,&a.filter2,sizeof a.filter2);}
 return v;
}
static void depth_zero(){
 const unsigned rates[]={0,499,1000};
 for(int freeze=0;freeze<2;freeze++)for(int quality=0;quality<2;quality++)for(unsigned which=0;which<2;which++)for(unsigned wave=0;wave<4;wave++)for(unsigned rate:rates){
  setup(freeze,quality);patch.motion.wave[0]=patch.motion.wave2[0]=wave;patch.ext[0].tides_rate=patch.motion.rate2[0]=rate;
  const auto base=trace(1024);
  for(unsigned target=7;target<=8;target++){
   setup(freeze,quality);patch.motion.wave[0]=patch.motion.wave2[0]=wave;patch.ext[0].tides_rate=patch.motion.rate2[0]=rate;
   (which?patch.motion.target2[0]:patch.motion.target[0])=target;
   CHECK(trace(1024)==base);zero_cases++;
  }
 }
}
static float sum_route(unsigned target){
 float m=0;
 for(unsigned i=0;i<6;i++)if(voices[i].active){
  if(patch.motion.target[i]==target)m+=tidal_wave(voices[i].tide_phase,patch.ext[i].tides?patch.motion.wave[i]:0)*unit(patch.ext[i].tides);
  if(patch.motion.target2[i]==target)m+=tidal_wave(voices[i].tide2_phase,patch.motion.wave2[i])*unit(patch.motion.depth2[i]);
 }
 return m;
}
static void verify_first_spawn(int freeze,int quality){
 const unsigned wh=writehead;uint32_t r=rng;const float jitter=expected_noise(r),pan=expected_noise(r);float out[2];
 qrender(out,1);CHECK(grains[0].age==1);CHECK(rng==r);
 const float sz=clamp01(unit(patch.grain_size)+clamp11(sum_route(7))*.40f);
 const float pos=clamp01(unit(patch.grain_pos)+clamp11(sum_route(8))*.30f);
 const unsigned life=2205+(unsigned)(sz*11025);CHECK(grains[0].life==life);
 const float travel=((freeze?0.f:1.f)-1.f)*life,lo=260+std::max(0.f,-travel),hi=(float)GN-260-std::max(0.f,travel);
 float delay=lo+pos*(hi-lo);delay=std::min(hi,std::max(lo,delay+jitter*250));float gp=(float)wh-delay;while(gp<0)gp+=GN;gp+=1;if(gp>=GN)gp-=GN;
 CHECK(grains[0].pos==gp);CHECK(grains[0].pan==.5f+pan*.4f);CHECK(patch.quality==quality);CHECK(grains[0].gain==2.f/(quality_on()?4:2));spawn_cases++;
}
static void spawn_matrix(){
 const unsigned rates[]={0,501,1000};
 for(int freeze=0;freeze<2;freeze++)for(int quality=0;quality<2;quality++)for(unsigned which=0;which<2;which++)for(unsigned target=7;target<=8;target++)for(unsigned wave=0;wave<4;wave++)for(unsigned rate:rates){
  setup(freeze,quality);(which?patch.motion.target2[0]:patch.motion.target[0])=target;
  patch.motion.wave[0]=patch.motion.wave2[0]=wave;patch.ext[0].tides_rate=patch.motion.rate2[0]=rate;
  if(which)patch.motion.depth2[0]=1000;else patch.ext[0].tides=1000;verify_first_spawn(freeze,quality);
 }
 // Full-track A-then-B order, shared destinations, positive/negative saturation,
 // and cancellation after a large prefix: distinguishes final from per-add clamp.
 for(int mode=0;mode<10;mode++)for(int freeze=0;freeze<2;freeze++){
  Grain reference={};uint32_t reference_rng=0;
  for(unsigned repeat=0;repeat<2;repeat++){
   setup(freeze,1,63);patch.grain_size=mode==3?970:mode==4?20:500;patch.grain_pos=patch.grain_size;
   for(unsigned i=0;i<6;i++){
    patch.motion.target[i]=(mode==2&&i%2)?8:7;patch.motion.target2[i]=(mode==2&&i%2)?7:8;
    if(mode>=5)patch.motion.target[i]=patch.motion.target2[i]=(mode==5||mode==7)?7:8;
    patch.ext[i].tides=patch.motion.depth2[i]=1000;
    patch.motion.wave[i]=patch.motion.wave2[i]=0;
    const bool cancelling=mode==2||mode==7||mode==8;
    voices[i].tide_phase=voices[i].tide2_phase=mode==1||mode==4||mode==9?.001f:cancelling?(i<3?.499f:.001f):.499f;
   }
   verify_first_spawn(freeze,1);
   if(!repeat){reference=grains[0];reference_rng=rng;}
   else{CHECK(!std::memcmp(&reference,&grains[0],sizeof reference));CHECK(reference_rng==rng);}
  }
 }
 // Inactive tracks, even with full LFO depth, do not move phases or contribute.
 setup(1,1,0);float phases[12];for(unsigned i=0;i<6;i++){patch.motion.target[i]=7;patch.motion.target2[i]=8;patch.ext[i].tides=patch.motion.depth2[i]=1000;phases[2*i]=voices[i].tide_phase;phases[2*i+1]=voices[i].tide2_phase;}
 verify_first_spawn(1,1);CHECK(grains[0].life==2205+(unsigned)(unit(500)*11025));for(unsigned i=0;i<6;i++){CHECK(voices[i].tide_phase==phases[2*i]);CHECK(voices[i].tide2_phase==phases[2*i+1]);}
}
static void existing_grains(){
 for(int freeze=0;freeze<2;freeze++)for(int quality=0;quality<2;quality++)for(unsigned target=7;target<=8;target++){
  setup(freeze,quality);patch.grain_size=1000;patch.grain_pos=0;patch.motion.target[0]=patch.motion.target2[0]=target;patch.ext[0].tides=patch.motion.depth2[0]=1000;
  grainclock=4096;
  for(unsigned i=0;i<4;i++){grains[i]={2000.25f+10000*i,.2f,1.f/(8000+i*1000),.1f+i*.2f,i==0?-2.f:i==1?.5f:i==2?1.f:2.f,.7f,8000+i*1000,123+i};}
  Grain before[4];std::memcpy(before,grains,sizeof before);float out[128];qrender(out,64);
  for(unsigned i=0;i<4;i++){const Grain &a=grains[i],&b=before[i];float pos=b.pos;for(unsigned k=0;k<64;k++){pos+=b.rate;if(pos>=GN)pos-=GN;if(pos<0)pos+=GN;}
   CHECK(a.life==b.life);CHECK(a.inc==b.inc);CHECK(a.rate==b.rate);CHECK(a.pan==b.pan);CHECK(a.gain==b.gain);CHECK(a.age==b.age+64);CHECK(a.pos==pos);CHECK(a.phase==a.age*a.inc);
  }continuity_cases++;
 }
}
static void bounds(){
 for(int freeze=0;freeze<2;freeze++)for(int quality=0;quality<2;quality++){
  setup(freeze,quality);patch.grain_size=0;patch.motion.target[0]=7;patch.motion.target2[0]=8;patch.ext[0].tides=patch.motion.depth2[0]=1000;patch.ext[0].tides_rate=patch.motion.rate2[0]=0;
  unsigned highest=0;float out[128];
  for(unsigned at=0;at<32768;at+=64){voices[0].active=1;qrender(out,64);unsigned active=0;
   for(float x:out)CHECK(std::isfinite(x));
   for(const Grain &g:grains)if(g.life){CHECK(g.life>=2205&&g.life<=13230);CHECK(g.age<=g.life);CHECK(g.pos>=0&&g.pos<GN);CHECK(std::isfinite(g.pos)&&std::isfinite(g.phase)&&std::isfinite(g.inc));if(g.age<g.life)active++;}
   CHECK(active<=(quality?4u:2u));highest=std::max(highest,active);CHECK(tape_valid==GN);CHECK(quality_on()==(quality!=0));bounds_frames+=64;
  }CHECK(highest==(quality?4u:2u));
 }
}
static void persistence(){
 static_assert(MD_PATCH_VERSION==12,"patch version changed");static_assert(sizeof(md_patch)==2604,"patch wire size changed");
 setup();patch.motion.target[0]=7;patch.motion.target2[0]=8;patch.ext[0].tides=600;patch.motion.depth2[0]=700;md_mark_saved();
 const md_patch saved=patch;const auto a=trace(2048);(void)a;CHECK(!std::memcmp(&saved,&patch,sizeof patch));CHECK(md_unsaved()==0);
 md_patch decoded;CHECK(md_decode(&decoded,&saved,sizeof saved));CHECK(!std::memcmp(&decoded,&saved,sizeof saved));CHECK(md_validate(&decoded));
 for(unsigned which=0;which<2;which++){md_patch bad=saved;(which?bad.motion.target2[0]:bad.motion.target[0])=9;CHECK(!md_validate(&bad));}
 float out[2];CHECK(md_command(MD_LFO_TARGET,0,8));md_render(out,1);CHECK(patch.motion.target[0]==8);CHECK(md_unsaved()!=0);md_mark_saved();CHECK(md_unsaved()==0);
 CHECK(md_command(MD_LFO2_TARGET,0,999));md_render(out,1);CHECK(patch.motion.target2[0]==8);CHECK(md_command(MD_LFO_TARGET,0,-1));md_render(out,1);CHECK(patch.motion.target[0]==0);
 CHECK(!std::strcmp(md_lfo_target_name(7),"GRAIN SIZE"));CHECK(!std::strcmp(md_lfo_target_name(8),"GRAIN POSITION"));
}
static std::vector<float> signal(unsigned target,unsigned which,unsigned depth){
 setup(1,1);patch.motion.target[0]=patch.motion.target2[0]=0;(which?patch.motion.target2[0]:patch.motion.target[0])=target;
 patch.motion.wave[0]=patch.motion.wave2[0]=1;patch.ext[0].tides_rate=patch.motion.rate2[0]=0;voices[0].tide_phase=voices[0].tide2_phase=.49f;
 if(which)patch.motion.depth2[0]=depth;else patch.ext[0].tides=depth;std::vector<float> x(8192*2);for(unsigned at=0;at<8192;at+=64)qrender(x.data()+2*at,64);return x;
}
static double audio_change(){
 double lowest=1e9;
 for(unsigned target=7;target<=8;target++)for(unsigned which=0;which<2;which++){
  const auto a=signal(target,which,0),b=signal(target,which,1000),again=signal(target,which,1000);CHECK(b.size()==again.size());CHECK(!std::memcmp(b.data(),again.data(),b.size()*sizeof(float)));
  double error=0,energy=0;for(unsigned i=0;i<a.size();i++){CHECK(std::isfinite(a[i])&&std::isfinite(b[i]));const double d=(double)a[i]-b[i];error+=d*d;energy+=(double)b[i]*b[i];}
  const double rms=std::sqrt(error/a.size());CHECK(rms>.001);CHECK(energy/a.size()>1e-5);lowest=std::min(lowest,rms);
 }return lowest;
}
int main(){depth_zero();spawn_matrix();existing_grains();bounds();persistence();const double difference=audio_change();
 std::printf("{\"pass\":true,\"checks\":%u,\"depth_zero_pairs\":%u,\"spawn_cases\":%u,\"continuity_cases\":%u,\"bounded_frames\":%u,\"minimum_audio_difference_rms\":%.9g,\"limits\":\"host sanitizer probe; no device timing or subjective listening; baseline1477 trace compared separately by root\"}\n",checks,zero_cases,spawn_cases,continuity_cases,bounds_frames,difference);
}
