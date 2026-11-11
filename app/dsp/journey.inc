/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * One active rack, one canonical project, one staging patch (the undo slot).
 * No allocation, flash reads or additional synthesis/effect buffers at transitions.
 * The final sixteenth dissolves only changing voices; shared effect tails survive.
 */
static unsigned journey_next(){unsigned at=journey_at+1;return at<journey_home.journey.length?at:journey_home.journey.mode==2?0:8;}
static unsigned journey_duration(){const auto &s=journey_home.journey.section[journey_at];return 16u*s.bars*s.repeats;}
static void journey_copy_track(md_patch &dst,const md_patch &src,unsigned i){
 dst.track[i]=src.track[i];dst.ext[i]=src.ext[i];dst.sequence[i]=src.sequence[i];dst.tone_preset[i]=src.tone_preset[i];
 dst.motion.direction[i]=src.motion.direction[i];dst.motion.feel[i]=src.motion.feel[i];dst.motion.shift[i]=src.motion.shift[i];dst.motion.target[i]=src.motion.target[i];dst.motion.wave[i]=src.motion.wave[i];dst.motion.target2[i]=src.motion.target2[i];dst.motion.wave2[i]=src.motion.wave2[i];dst.motion.depth2[i]=src.motion.depth2[i];dst.motion.rate2[i]=src.motion.rate2[i];
}
static unsigned journey_stage(unsigned at,bool first){
 const auto &s=journey_home.journey.section[at];
 if(s.biome==65535)previous=journey_home;else factory_scene(previous,s.biome);
 previous.journey=journey_home.journey;memcpy(previous.links,journey_home.links,sizeof previous.links);previous.drift=journey_home.drift;previous.tempo=clock_source?patch.tempo:journey_home.tempo;
 previous.transpose=bound(previous.transpose+s.transpose,-24,24);
 unsigned keep=first?0:s.keep;
 // Slots 4..6 cannot carry a synth into a kit rack (or vice versa).
 if(previous.kit.enabled!=patch.kit.enabled)keep&=7;
 if(previous.kit.enabled)keep&=15;
 for(unsigned i=0;i<6;i++)if(keep&(1u<<i))journey_copy_track(previous,patch,i);
 if(previous.kit.enabled&&(keep&8))previous.kit=patch.kit;
 return keep;
}
static void journey_enter(unsigned at,unsigned keep){
 // Performance mutes/macros may have changed after staging the final step.
 for(unsigned i=0;i<6;i++)if(keep&(1u<<i))journey_copy_track(previous,patch,i);
 if(previous.kit.enabled&&(keep&8))previous.kit=patch.kit;
 learn_reset();patch=previous;journey_at=at;journey_ticks=0;journey_chord_origin=step;
 journey_prepared=0;journey_fade_mask=0;seqrng=patch.seed;
 for(unsigned i=0;i<6;i++)if(!(keep&(1u<<i))){
  reset_voice(i);repeats[i].remaining=0;seq_gate[i]=0;journey_origin[i]=step;
 }
 if(!(keep&8))kit_reset();
 // Do not clear the tape, grains, echo, room, DC state or macro slew.
}
static void journey_start(){
 if(!patch.journey.mode)return;
 journey_home=patch;journey_active=1;journey_at=0;journey_ticks=0;journey_prepared=0;
 step=0;memset(journey_origin,0,sizeof journey_origin);record_mode=0;record_note=record_step=-1;
 journey_enter(0,journey_stage(0,true));pending_biome=-1;
}
static void journey_stop(){
 if(!journey_active)return;
 journey_release[0]=journey_dry_last[0];journey_release[1]=journey_dry_last[1];
 journey_active=journey_prepared=0;journey_fade_mask=0;patch=journey_home;previous=patch;learn_reset();
 // Stop restores the editable project, not the last transient factory section.
 for(unsigned i=0;i<6;i++){reset_voice(i);repeats[i].remaining=0;seq_gate[i]=0;}
 kit_reset();record_mode=0;record_note=record_step=-1;
}
static bool journey_tick(){
 if(journey_ticks>=journey_duration()){
  unsigned next=journey_next();
  if(next==8){journey_stop();fill_state=0;playing=0;untilstep=0;return false;}
  unsigned keep=(~journey_fade_mask)&63u;journey_enter(next,keep);
 }
 return true;
}
static void journey_prepare(){
 if(++journey_ticks<journey_duration())return;
 unsigned next=journey_next();unsigned keep=next==8?0:journey_stage(next,false);
 journey_fade_mask=(~keep)&63u;journey_prepared=1;
 journey_fade_step=1.f/(MD_RATE*60.f/(patch.tempo*4.f)*(1+(step%2?-1:1)*unit(patch.swing)*.5f));
}
static float journey_gain(unsigned,unsigned k,unsigned n){
 return fminf(1.f,fmaxf(0.f,(untilstep+n-k)*journey_fade_step));
}
