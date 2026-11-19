/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Original compact fractional-delay string. A bounded half-rate Q15 loop,
 * excitation spread over time, and full-rate reconstruction: no trigger fill. */
struct CedarState {float delay,previous,quant_error;unsigned head,phase,left,span;uint32_t rng;};
static int16_t cedar_line[6][512];
static CedarState cedar_state[6];
static void cedar_reset(unsigned i){
 memset(cedar_line[i],0,sizeof cedar_line[i]);memset(&cedar_state[i],0,sizeof cedar_state[i]);
 cedar_state[i].rng=0x9e3779b9u^(patch.seed+i*7919u);
}
static void cedar_render(unsigned i,Voice &v,const md_track &t,float f,float tone,float harm,float shape,bool trigger,float *out,unsigned n,bool bowed=false){
 CedarState &s=cedar_state[i];
 float fundamental=f,loopf=f;
 // Low notes keep their true fundamental while the finite string models an
 // octave partial. Every accepted MIDI note retains its concert-pitch anchor.
 while(loopf<1.f/1018.f)loopf*=2;
 float bright=bowed?.24f+.35f*tone:.52f+.45f*tone;
 float target=fmaxf(2.f,fminf(510.f,.5f/loopf-(1-bright)));
 if(!s.delay)s.delay=target;
 if(trigger)s.left=s.span=(unsigned)fmaxf(12.f,fminf(320.f,target));
 float seconds=.45f+12.f*unit(t.decay)*unit(t.decay);
 // [1/1] exponential approximation for the loop loss: bounded and monotonic,
 // without thousands of software powf calls per second on the target CPU.
 float loss=6.9077553f*target/(22050.f*seconds);
 float feedback=(1-.5f*loss)/(1+.5f*loss);
 float anchor=bowed?.12f+.24f*harm:loopf>fundamental*1.1f?.24f:.08f;
 const float pluck_pole=.14f+.24f*tone;
 const float pluck_gain=.21f+.31f*(1-shape);
 const float pluck_edge=fminf(16.f,s.span*.25f);
 const float pluck_ramp=pluck_edge>0?1.f/pluck_edge:0;
 for(unsigned k=0;k<n;k++){
  if(!s.phase){
   s.delay+=(target-s.delay)*.006f;
   float pos=s.head-s.delay;if(pos<0)pos+=512;
   unsigned a=(unsigned)pos;float frac=pos-a;
   float x=(cedar_line[i][a]+(cedar_line[i][(a+1)&511]-cedar_line[i][a])*frac)*(1.f/32768.f);
   float filtered=bright*x+(1-bright)*s.previous;s.previous=x;
   float excitation=0;
   if(s.left||bowed){float noise=(random_word(s.rng)>>8)*(1.f/8388608.f)-1.f;
    if(bowed){v.noise_lp+=(noise-v.noise_lp)*(.008f+.055f*tone);excitation=v.noise_lp*(.11f+.23f*shape);}
    else{
     // Round the edges and band-limit the contact before it enters the
     // resonator. Tone still opens the attack, and loop brightness is unchanged.
     v.noise_lp+=(noise-v.noise_lp)*pluck_pole;
     float edge=fminf((float)(s.span-s.left),(float)s.left);
     excitation=v.noise_lp*pluck_gain*fminf(1.f,edge*pluck_ramp);
    }
    if(s.left)s.left--;}
   float value=filtered*feedback+excitation;
   if(bowed)value-=value*value*value*.035f;
   value=fmaxf(-.97f,fminf(.97f,value));
   // Error feedback avoids a coarse quantisation tail in the compact loop.
   float q=value*32768+s.quant_error;int sample=(int)(q+(q>=0?.5f:-.5f));
   sample=bound(sample,-32768,32767);s.quant_error=(q-sample)*.75f;
   cedar_line[i][s.head]=(int16_t)sample;s.head=(s.head+1)&511;
   v.eco_previous=v.eco_next;v.eco_next=filtered;
  }
  float x=s.phase?v.eco_next:.5f*(v.eco_previous+v.eco_next);s.phase^=1;
  v.sub_phase+=fundamental;if(v.sub_phase>=1)v.sub_phase-=1;
  out[k]=x*(3.6f+1.2f*harm)+anchor*plaits::Sine(v.sub_phase);
 }
}
