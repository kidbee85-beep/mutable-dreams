/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * One captured gesture never changes a sound. Pick up or cross the current
 * value before applying; reserved MIDI messages keep their protocol meaning. */
static md_midi_mapping mappings[16];static int16_t learn_last[16];static uint8_t learn_picked[16];
static int8_t learn_sender[16];
static unsigned mapping_generation,learn_deadline;static int learn_arm=-1;static int learn_applying=-1;
static bool learn_safe(int cc){return cc>=1&&cc<120&&cc!=6&&cc!=64&&!(cc>=32&&cc<=63)&&!(cc>=96&&cc<=101);}
static void learn_cancel(){learn_arm=-1;}
static void learn_reset(){memset(learn_last,0xff,sizeof learn_last);memset(learn_picked,0,sizeof learn_picked);memset(learn_sender,-1,sizeof learn_sender);}
static void learn_reset_port(int port){for(int j=0;j<16;j++)if(learn_sender[j]==port){learn_picked[j]=0;learn_last[j]=learn_sender[j]=-1;}}
static void learn_init(){memset(mappings,0,sizeof mappings);learn_arm=-1;mapping_generation=0;learn_applying=-1;learn_reset();}
static int learn_current(const md_midi_mapping &m){
 unsigned i=m.track;switch(m.target){case 0:case 1:case 2:case 3:return patch.macro[m.target];case 4:return patch.track[i].level;case 5:return patch.track[i].cutoff;case 6:return patch.track[i].pan;case 7:return patch.track[i].send;case 8:return patch.ext[i].delay_send;case 9:return patch.ext[i].reverb_send;case 10:return patch.drift.position;default:return patch.drift.depth;}
}
static Command learn_command(const md_midi_mapping &m,int value){
 static const int cmds[12]={MD_MACRO,MD_MACRO,MD_MACRO,MD_MACRO,MD_LEVEL,MD_CUTOFF,MD_PAN,MD_SEND,MD_DELAY_SEND,MD_REVERB_SEND,MD_DRIFT_POSITION,MD_DRIFT_DEPTH};return {cmds[m.target],m.target<4?m.target:m.track,value};
}
static void learn_parameter_changed(Command e){
 if(e.c==MD_RETURN||e.c==MD_SCENE||e.c==MD_SCENE_CONFIRM||e.c==MD_TONE_PRESET){learn_reset();return;}
 for(int j=0;j<16;j++){auto c=learn_command(mappings[j],0);if(j!=learn_applying&&c.c==e.c&&(c.t==e.t||e.c>=MD_DRIFT_MODE)){learn_picked[j]=0;learn_last[j]=-1;}}
}
static bool learn_matches(const md_midi_mapping &m,int port,unsigned channel){return m.enabled&&(m.port==3||m.port==port)&&(!m.channel||m.channel==channel);}
static bool learn_cc(int port,unsigned channel,unsigned cc,unsigned value){
 if(cc==121){for(int j=0;j<16;j++)if(learn_sender[j]==port&&learn_matches(mappings[j],port,channel)){learn_picked[j]=0;learn_last[j]=learn_sender[j]=-1;}return false;}
 if(learn_arm>=0&&(int32_t)(stats.frames-learn_deadline)>=0)learn_arm=-1;
 if(learn_arm>=0&&learn_safe(cc)){
  auto &m=mappings[learn_arm];m.enabled=1;m.port=port;m.channel=channel;m.cc=cc;m.reserved[0]=m.reserved[1]=0;
  // The most recent assignment wins over an exact duplicate.
  for(int j=0;j<16;j++)if(j!=learn_arm&&learn_matches(mappings[j],port,channel)&&mappings[j].cc==cc)mappings[j].enabled=0;
  mapping_generation++;learn_reset();learn_arm=-1;return true;
 }
 bool consumed=false;
 for(int j=0;j<16;j++){auto &m=mappings[j];if(!learn_matches(m,port,channel))continue;
  if(cc>=32&&cc<=63&&m.cc==cc-32){consumed=true;continue;}
  if(m.cc!=cc)continue;consumed=true;
  if(learn_sender[j]!=port){learn_picked[j]=0;learn_last[j]=-1;learn_sender[j]=port;}
  int v=value*1000/127,current=learn_current(m),last=learn_last[j];
  if((v>current?v-current:current-v)<=16||(last>=0&&((last<=current&&v>=current)||(last>=current&&v<=current))))learn_picked[j]=1;
  learn_last[j]=v;if(learn_picked[j]){learn_applying=j;apply(learn_command(m,v));learn_applying=-1;}
 }return consumed;
}
static void learn_edit(Command e){
 int j=bound(e.t,0,15);auto &m=mappings[j];int v=e.v;
 if(e.c==MD_LEARN_ARM){learn_arm=v?j:-1;learn_deadline=stats.frames+MD_RATE*20;learn_reset();return;}
 if(e.c==MD_LEARN_CLEAR){memset(&m,0,sizeof m);if(learn_arm==j)learn_arm=-1;}
 else if(e.c==MD_LEARN_TARGET)m.target=bound(v,0,11);
 else if(e.c==MD_LEARN_TRACK)m.track=bound(v,0,5);
 else if(e.c==MD_LEARN_PORT)m.port=bound(v,0,3);
 else if(e.c==MD_LEARN_CHANNEL)m.channel=bound(v,0,16);
 else if(e.c==MD_LEARN_CC){if(!learn_safe(v))return;m.cc=v;}
 else if(e.c==MD_LEARN_ENABLE){if(v&&!learn_safe(m.cc))return;m.enabled=!!v;}
 mapping_generation++;learn_reset();
}
