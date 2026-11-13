/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Four bounded sample voices, no allocation or streaming in the audio callback.
 * Kit replaces slots 4-6 at runtime; their saved synth settings stay intact. */
#include "lofi_samples.inc"
struct KitVoice { float pos,rate,gain,lp,cut,env,decay,last,carry; unsigned end,age; uint8_t sample,pad; };
static KitVoice kit_voices[4];static unsigned kit_age;static float kit_hiss_lp,kit_amp,kit_pan=.5f;
static void kit_reset(){memset(kit_voices,0,sizeof kit_voices);kit_age=0;kit_hiss_lp=kit_amp=0;kit_pan=.5f;}
static unsigned kit_note(int n){switch(n){case 36:return 0;case 38:return 1;case 42:return 2;case 45:return 3;case 37:return 4;case 46:return 5;case 39:return 6;case 51:return 7;default:return (unsigned)(n>=53?n-53:n>=48?n-48:n)%8;}}
static void kit_pattern(md_kit &k,int g){
 k.generator=g;memset(k.gates,0,sizeof k.gates);
 for(unsigned n=0;n<32;n++){
  if(g==0){if(n%8==0)k.gates[0]|=1u<<n;if(n%8==4)k.gates[1]|=1u<<n;if(n%4==2)k.gates[2]|=1u<<n;if(n%16==13)k.gates[6]|=1u<<n;}
  if(g==1){if(n%8==0||n%16==11)k.gates[0]|=1u<<n;if(n%8==4)k.gates[1]|=1u<<n;if(n%2==0)k.gates[2]|=1u<<n;if(n%16==7)k.gates[3]|=1u<<n;}
  if(g==2){if(n%16==0)k.gates[0]|=1u<<n;if(n%16==7)k.gates[3]|=1u<<n;if(n%16==12)k.gates[7]|=1u<<n;if(n%8==3)k.gates[6]|=1u<<n;}
  if(g==3){if(n%16==0)k.gates[5]|=1u<<n;if(n%8==5)k.gates[6]|=1u<<n;if(n%16==11)k.gates[7]|=1u<<n;}
 }
}
static void kit_fire(unsigned pad,float velocity){
 if(!patch.kit.enabled||pad>=8)return;link_hit(3,velocity);
 unsigned slot=0;for(unsigned j=0;j<4;j++){if(!kit_voices[j].end){slot=j;break;}if(kit_voices[j].age<kit_voices[slot].age)slot=j;}
 KitVoice &v=kit_voices[slot];float carry=v.last;const md_pad &p=patch.kit.pads[pad];
 memset(&v,0,sizeof v);v.carry=carry;v.pad=pad;v.sample=patch.kit.bank*8+pad;v.end=kit_samples[v.sample].size-1;
 v.rate=(16000.f/MD_RATE)*stmlib::SemitonesToRatio((float)p.tune);v.gain=velocity*unit(p.level)*.25f;v.env=1;v.age=++kit_age;
 v.cut=.02f+.55f*unit(p.cutoff)*unit(p.cutoff);v.decay=1.f-1.f/(MD_RATE*(.008f+.9f*unit(p.decay)*unit(p.decay)));
}
static void kit_mix(float *lr,float *send,float *delay_send,float *room_send,unsigned n
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
 ,const md_usb_stems &stems,unsigned offset
#endif
){
 md_track live_track;md_extension live_ext;bool moving=creative_active();
 if(moving){live_track=patch.track[3];live_ext=patch.ext[3];creative_track(3,live_track,live_ext);}
 const md_track &t=moving?live_track:patch.track[3];const md_extension &e=moving?live_ext:patch.ext[3];
 float ds=unit(e.delay_send),rs=unit(e.reverb_send),master_send=unit(t.send),target=t.mute?0:unit(t.level)*expression[3];
 for(unsigned k=0;k<n;k++){
  float sum=0,gs=0;
  for(unsigned j=0;j<4;j++){
   KitVoice &v=kit_voices[j];float x=0;
   if(v.end&&v.pos<v.end){unsigned at=(unsigned)v.pos;float f=v.pos-at;const int16_t *pcm=kit_samples[v.sample].data;
    x=(pcm[at]+(pcm[at+1]-pcm[at])*f)*(1.f/32768.f);v.lp+=(x-v.lp)*v.cut;
    float attack=fminf(1.f,v.pos/16.f);x=v.lp*v.gain*v.env*attack;v.pos+=v.rate;v.env*=v.decay;
   }else v.end=0;
   // Steals and sample endings release smoothly, including sparse MIDI hits.
   v.carry*=.96f;if(fabsf(v.carry)<1e-12f)v.carry=0;x+=v.carry;if(!v.end){v.last*=.94f;if(fabsf(v.last)<1e-12f)v.last=0;x+=v.last;}else v.last=x;
   sum+=x;gs+=x*unit(patch.kit.pads[v.pad].send);
  }
  if(playing&&patch.kit.hiss){kit_hiss_lp+=(noise()-kit_hiss_lp)*.13f;float h=kit_hiss_lp*patch.kit.hiss*.0006f;sum+=h;gs+=h;}
  kit_amp+=(target-kit_amp)*.003f;kit_pan+=(unit(t.pan)-kit_pan)*.003f;
  float amp=kit_amp;if(journey_prepared&&(journey_fade_mask&8))amp*=journey_gain(3,k,n);
  sum*=amp;gs*=amp;stats.track_peak[3]=fmaxf(stats.track_peak[3],fabsf(sum));
  // K-1: the dry kit leaves; its echo and room sends stay and carry the tail.
  float dry=sum;if(kit_scene_step!=0.f||kit_scene_gain!=1.f){kit_scene_gain+=kit_scene_step;dry*=kit_scene_gain;}
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
  write_stem(stems,offset+k,3,dry);
#endif
  lr[k*2]+=dry*(1-kit_pan);lr[k*2+1]+=dry*kit_pan;send[k]+=gs*master_send;delay_send[k]+=sum*ds;room_send[k]+=sum*rs;
 }
}
