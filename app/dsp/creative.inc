/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Control-rate motion only. No audio buffers, allocation, recursive modulation,
 * engine switches or feedback gain destinations. Defaults are bit-transparent. */
static float link_envelope[4],link_mod[6][6],link_readout[4],link_phase[4];
static float drift_phase,drift_live;static int drift_direction=1;
static bool creative_active(){
 if(patch.drift.mode&&patch.drift.depth&&patch.drift.captured==3)return true;
 for(auto &l:patch.links)if(l.depth)return true;for(auto &row:link_mod)for(float v:row)if(fabsf(v)>.01f)return true;return false;
}
static bool drift_active(){return patch.drift.mode&&patch.drift.depth&&patch.drift.captured==3;}
static void creative_reset(){memset(link_envelope,0,sizeof link_envelope);memset(link_mod,0,sizeof link_mod);memset(link_readout,0,sizeof link_readout);memset(link_phase,0,sizeof link_phase);drift_phase=drift_live=unit(patch.drift.position);drift_direction=1;}
static void link_hit(unsigned track,float velocity){for(unsigned j=0;j<4;j++)if(patch.links[j].depth&&patch.links[j].source==track)link_envelope[j]=fmaxf(link_envelope[j],velocity);}
static void drift_capture(int endpoint){
 auto &s=patch.drift.scene[endpoint];for(int j=0;j<4;j++)s.macro[j]=patch.macro[j];
 for(int i=0;i<6;i++){auto &t=patch.track[i];auto &e=patch.ext[i];uint16_t values[8]={(uint16_t)t.tone,(uint16_t)t.cutoff,(uint16_t)t.decay,(uint16_t)t.level,(uint16_t)t.pan,(uint16_t)t.send,(uint16_t)e.delay_send,(uint16_t)e.reverb_send};memcpy(s.track[i],values,sizeof values);}
 patch.drift.captured|=1u<<endpoint;
}
static int drift_value(int base,int a,int b){return bound((int)(base+unit(patch.drift.depth)*(a+(b-a)*drift_live-base)+.5f),0,1000);}
static int drift_macro(unsigned k){return drift_active()?drift_value(patch.macro[k],patch.drift.scene[0].macro[k],patch.drift.scene[1].macro[k]):patch.macro[k];}
static int creative_probability(unsigned i){return bound(patch.track[i].prob+(int)link_mod[i][5],0,1000);}
static void creative_track(unsigned i,md_track &t,md_extension &e){
 if(drift_active()){
  auto &a=patch.drift.scene[0];auto &b=patch.drift.scene[1];int32_t *fields[8]={&t.tone,&t.cutoff,&t.decay,&t.level,&t.pan,&t.send,&e.delay_send,&e.reverb_send};
  for(int k=0;k<8;k++)*fields[k]=drift_value(*fields[k],a.track[i][k],b.track[i][k]);
 }
 t.cutoff=bound(t.cutoff+(int)link_mod[i][0],0,1000);t.tone=bound(t.tone+(int)link_mod[i][1],0,1000);t.pan=bound(t.pan+(int)link_mod[i][2],0,1000);t.send=bound(t.send+(int)link_mod[i][3],0,1000);
 e.delay_send=bound(e.delay_send+(int)link_mod[i][4],0,1000);e.reverb_send=bound(e.reverb_send+(int)link_mod[i][4],0,1000);
}
static void creative_tick(unsigned n){
 if(!patch.drift.mode&&!creative_active())return;
 if(patch.drift.mode&&patch.drift.captured==3){
  if(patch.drift.mode==1)drift_phase=unit(patch.drift.position);
  else {float distance=n/(MD_RATE*(10.f+unit(patch.drift.rate)*290.f)*.5f);drift_phase+=drift_direction*distance;if(drift_phase>1){drift_phase=2-drift_phase;drift_direction=-1;}else if(drift_phase<0){drift_phase=-drift_phase;drift_direction=1;}}
  drift_live+=(drift_phase-drift_live)*(n/(MD_RATE*.04f+n));
 }
 float target[6][6]={};
 for(unsigned j=0;j<4;j++){
  const auto &l=patch.links[j];float x=0;if(l.depth&&l.signal>=2){int rate=l.signal==2?patch.ext[l.source].tides_rate:patch.motion.rate2[l.source];link_phase[j]+=n/(MD_RATE*(.5f+unit(rate)*19.5f));if(link_phase[j]>=1)link_phase[j]-=1;}
  if(l.depth&&!patch.track[l.source].mute&&!(patch.kit.enabled&&l.source>3)){
   if(l.signal==0)x=link_envelope[j];
   else if(l.signal==1){if(patch.kit.enabled&&l.source==3){for(auto &v:kit_voices)x=fmaxf(x,v.env);}else x=voices[l.source].env;x=fminf(1.f,fmaxf(0.f,x));}
   else {int shape=l.signal==2?patch.motion.wave[l.source]:patch.motion.wave2[l.source];x=tidal_wave(link_phase[j],shape);}
   target[l.destination][l.target]+=x*l.depth;
  }
  link_readout[j]=x;link_envelope[j]/=1.f+n/(MD_RATE*(.05f+unit(l.release)*7.95f));
 }
 float a=n/(MD_RATE*.025f+n);for(int i=0;i<6;i++)for(int k=0;k<6;k++)link_mod[i][k]+=(fminf(1000.f,fmaxf(-1000.f,target[i][k]))-link_mod[i][k])*a;
}
