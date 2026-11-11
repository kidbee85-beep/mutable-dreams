// Offline Journey qualification; host timing is not an FM-1 CPU measurement.
#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <vector>
static float lr[256];
static double run(unsigned n,FILE *f=nullptr){double e=0;while(n){unsigned z=n>128?128:n;md_render(lr,z);for(unsigned k=0;k<z*2;k++){assert(std::isfinite(lr[k])&&fabsf(lr[k])<1);e+=lr[k]*lr[k];}if(f)assert(fwrite(lr,8,z,f)==z);n-=z;}return e;}
static void cmd(int c,int t,int v){assert(md_command(c,t,v));run(1);}
static md_patch base(){md_init();cmd(MD_SCENE,0,169);md_patch p=*md_state();p.tempo=120;p.locked=1;p.journey.mode=1;p.journey.length=2;p.journey.section[0]={65535,1,1,0,0,{0,0}};p.journey.section[1]={171,1,1,2,0,{0,0}};return p;}
static void stop(){cmd(MD_PLAY,0,0);assert(!md_journey_active()&&!md_playing());}
static void step_rules(){
 md_init();cmd(MD_SCENE,0,MD_BLANK_BIOME);
 md_patch base=*md_state(),legacy=base,migrated;
 legacy.version=11;
 assert(md_decode(&migrated,&legacy,sizeof legacy));
 assert(!memcmp(&migrated,&base,sizeof base));
 legacy.sequence[0].steps[0].chance|=128u;
 assert(!md_decode(&migrated,&legacy,sizeof legacy));
 legacy.version=10;assert(!md_decode(&migrated,&legacy,2356));
 base.locked=1;
 base.track[0].speed=1;base.track[0].prob=1000;
 auto &seq=base.sequence[0];seq.enabled=1;seq.length=4;
 for(unsigned i=0;i<4;i++)seq.steps[i]={uint8_t(60+i),80,100,95};
 md_step_set_rule(&seq.steps[0],MD_RULE_FIRST);
 md_step_set_rule(&seq.steps[1],MD_RULE_SECOND);
 md_step_set_rule(&seq.steps[2],MD_RULE_FILL);
 assert(md_load(&base));
 cmd(MD_SEQ_STEP,0,2);cmd(MD_SEQ_CHANCE,0,83);cmd(MD_SEQ_GATE,0,91);
 auto step=md_state()->sequence[0].steps[2];
 assert(md_step_rule(&step)==MD_RULE_FILL&&md_step_chance(&step)==83&&md_step_gate(&step)==91);
 cmd(MD_SEQ_COPY,0,0);cmd(MD_SEQ_PASTE,1,0);
 assert(!memcmp(&md_state()->sequence[1].steps[0],&step,sizeof step));
 cmd(MD_SEQ_MIRROR,0,1);
 assert(md_step_rule(&md_state()->sequence[0].steps[1])==MD_RULE_FILL);
 cmd(MD_RETURN,0,0);
 assert(md_step_rule(&md_state()->sequence[0].steps[2])==MD_RULE_FILL);
 cmd(MD_SEQ_RULE,0,MD_RULE_FIRST);
 assert(md_step_rule(&md_state()->sequence[0].steps[2])==MD_RULE_FIRST);
 cmd(MD_RETURN,0,0);
 assert(md_step_rule(&md_state()->sequence[0].steps[2])==MD_RULE_FILL);
 assert(md_load(&base));
 cmd(MD_RECORD,0,1);cmd(MD_SEQ_STEP,0,2);cmd(MD_TRIGGER,0,72);cmd(MD_NOTE_OFF,0,72);
 assert(md_step_rule(&md_state()->sequence[0].steps[2])==MD_RULE_FILL);
 cmd(MD_RECORD,0,0);
 assert(md_load(&base));cmd(MD_SEQ_STEP,0,0);cmd(MD_SEQ_RULE,0,MD_RULE_FILL);
 cmd(MD_PLAY,0,1);cmd(MD_RECORD,0,2);cmd(MD_TRIGGER,0,69);run(1000);cmd(MD_NOTE_OFF,0,69);
 assert(md_step_rule(&md_state()->sequence[0].steps[0])==MD_RULE_FILL);
 assert(md_step_gate(&md_state()->sequence[0].steps[0])>=1&&md_step_gate(&md_state()->sequence[0].steps[0])<=100);
 cmd(MD_PLAY,0,0);
 assert(md_load(&base));
 cmd(MD_FILL,0,1);assert(md_fill_status()==0&&!md_unsaved());
 cmd(MD_CLOCK_SOURCE,0,1);md_mark_saved();md_midi(0xfa,0,0,0);
 for(unsigned i=0;i<37;i++){
  if(i==8){cmd(MD_FILL,0,-1);assert(md_fill_status()==2);cmd(MD_FILL,0,-1);assert(md_fill_status()==0);}
  if(i==12){cmd(MD_FILL,0,-1);assert(md_fill_status()==2);}
  md_midi(0xf8,0,0,0);
  int hit;
  assert(md_sequence_playhead(0,&hit)==int(i%4));
  bool expected=i%4==0?i==0:i%4==1?(i/4)%2!=0:i%4==2?i>=16&&i<32:true;
  assert(hit==expected);
  assert(md_fill_status()==(i>=16&&i<32?1:i>=12&&i<16?2:0));
  for(int pulse=0;pulse<5;pulse++)md_midi(0xf8,0,0,0);
 }
 assert(!md_unsaved());
 cmd(MD_FILL,0,1);md_midi(0xfc,0,0,0);
 assert(md_fill_status()==0);md_midi(0xfb,0,0,0);assert(md_fill_status()==0);md_midi(0xfc,0,0,0);
 base.sequence[1]=base.sequence[0];
 base.track[1].speed=1;base.track[1].prob=1000;base.track[1].mute=0;
 base.journey.mode=2;base.journey.length=2;
 base.journey.section[0]={65535,1,1,0,0,{0,0}};
 base.journey.section[1]={65535,1,1,1,0,{0,0}};
 assert(md_load(&base));cmd(MD_CLOCK_SOURCE,0,1);md_midi(0xfa,0,0,0);
 for(unsigned i=0;i<=16;i++){
  if(i==12)cmd(MD_FILL,0,1);
  md_midi(0xf8,0,0,0);
  for(int pulse=0;pulse<5;pulse++)md_midi(0xf8,0,0,0);
 }
 int carried,reset;
 md_sequence_playhead(0,&carried);md_sequence_playhead(1,&reset);
 assert(md_journey_position()==1&&!carried&&reset&&md_fill_status()==1);
 md_midi(0xfc,0,0,0);
 for(int id=MD_RULE_FIRST_BIOME;id<MD_BIOMES;id++){
  md_init();cmd(MD_SCENE,0,id);assert(md_validate(md_state())&&md_state()->quality==1&&!md_status()->quality_reductions);
  unsigned mask=0;for(const auto &q:md_state()->sequence){assert(q.enabled);for(const auto &s:q.steps)if(s.note)mask|=1u<<md_step_rule(&s);}
  assert(mask&~1u);
  if(id<MD_MALLET_FIRST_BIOME)assert((mask&13)==13);
 }
 puts("PASS v11 migration, expression, clipboard, reversal and undo; exact first/even-cycle and single-bar fill timing over MIDI clock; 12 valid condition Biomes.");
}
static void sequence_tools(){
 md_init();
 cmd(MD_SCENE,0,MD_BLANK_BIOME);
 md_patch p=*md_state();
 auto &phrase=p.sequence[0];
 phrase.enabled=1;phrase.length=5;phrase.edit=0;
 phrase.steps[0]={60,76,100,81};
 phrase.steps[1]={0,65,100,89};
 phrase.steps[2]={64,85,100,79};
 phrase.steps[3]={67,82,100,67};
 phrase.steps[4]={96,79,100,55};
 assert(md_load(&p));
 cmd(MD_SEQ_COPY,0,0);
 assert(md_sequence_clipboard()==1&&!md_unsaved());
 cmd(MD_SEQ_PASTE,1,1);
 assert((md_sequence_notice()&15)==MD_SEQ_WRONG_CLIP&&!md_unsaved());
 cmd(MD_SEQ_PASTE,1,0);
 assert(!memcmp(&md_state()->sequence[1].steps[0],&phrase.steps[0],sizeof(md_step)));
 assert(md_state()->track[1].model==p.track[1].model&&md_unsaved());
 cmd(MD_RETURN,0,0);
 assert(!memcmp(md_state(),&p,sizeof p));
 cmd(MD_SEQ_COPY,0,1);
 cmd(MD_SEQ_PASTE,1,1);
 assert(!memcmp(&md_state()->sequence[1],&phrase,sizeof phrase));
 cmd(MD_SEQ_TRANSPOSE,1,64+24+7);
 assert((md_sequence_notice()&15)==MD_SEQ_NOTE_LIMIT&&md_state()->sequence[1].steps[0].note==60);
 cmd(MD_SEQ_TRANSPOSE,1,64+24-12);
 assert((md_sequence_notice()&15)==MD_SEQ_TRANSPOSED);
 assert(md_state()->sequence[1].steps[0].note==48&&md_state()->sequence[1].steps[1].note==0);
 assert(md_state()->sequence[1].steps[0].velocity==76&&md_state()->sequence[1].steps[0].gate==81);
 cmd(MD_SEQ_MIRROR,1,1);
 assert(md_state()->sequence[1].steps[4].note==48&&md_state()->sequence[1].steps[3].note==0);
 cmd(MD_RETURN,0,0);
 assert(md_state()->sequence[1].steps[0].note==48);
 cmd(MD_RECORD,1,1);
 assert(!md_command(MD_SEQ_PASTE,1,1));
 assert(!md_command(MD_SEQ_EXPRESSION,1,50));
 cmd(MD_RECORD,1,0);
 puts("PASS step and phrase copy/paste, transpose, limit rejection, rest and expression preservation, reverse and undo.");

 md_step_set_rule(&phrase.steps[0],MD_RULE_FILL);
 assert(md_load(&p));
 md_patch expected=p;expected.edited_mask|=1;
 expected.sequence[0].steps[0].velocity=96;
 expected.sequence[0].steps[2].velocity=78;
 expected.sequence[0].steps[3].velocity=68;
 expected.sequence[0].steps[4].velocity=93;
 cmd(MD_SEQ_EXPRESSION,0,100);
 assert((md_sequence_notice()&15)==MD_SEQ_ACCENTED);
 assert(!memcmp(md_state(),&expected,sizeof expected)&&md_unsaved());
 cmd(MD_SEQ_EXPRESSION,0,0);
 assert((md_sequence_notice()&15)==MD_SEQ_NO_CHANGE);
 assert(!memcmp(md_state(),&expected,sizeof expected));
 cmd(MD_RETURN,0,0);assert(!memcmp(md_state(),&p,sizeof p));
 expected=p;expected.edited_mask|=1;
 expected.sequence[0].steps[0].gate=128+93;
 expected.sequence[0].steps[2].gate=69;
 expected.sequence[0].steps[3].gate=42;
 expected.sequence[0].steps[4].gate=63;
 cmd(MD_SEQ_EXPRESSION,0,MD_EXPRESSION_GATE_OFFSET+100);
 assert((md_sequence_notice()&15)==MD_SEQ_ARTICULATED);
 assert(!memcmp(md_state(),&expected,sizeof expected)&&md_validate(md_state()));
 cmd(MD_SEQ_EXPRESSION,1,80);
 assert((md_sequence_notice()&15)==MD_SEQ_UNAVAILABLE);
 cmd(MD_RETURN,0,0);assert(!memcmp(md_state(),&p,sizeof p));
 puts("PASS phrase expression preserves pitch, rests, chance and rule bits, and a no-op leaves Undo intact.");

 p.journey.mode=2;p.journey.length=2;
 p.journey.section[0]={65535,1,1,0,0,{0,0}};
 p.journey.section[1]={65535,1,1,1,0,{0,0}};
 // This section checks playhead carry without requiring a queued Fill.
 md_step_set_rule(&phrase.steps[0],MD_RULE_ALWAYS);
 p.sequence[1]=phrase;p.sequence[1].steps[0].note=0;
 p.track[0].speed=1;p.track[1].speed=1;p.track[0].prob=p.track[1].prob=1000;
 p.track[0].mute=p.track[1].mute=0;
 p.motion.direction[0]=1;p.motion.shift[0]=2;
 assert(md_load(&p));
 cmd(MD_CLOCK_SOURCE,0,1);
 md_midi(0xfa,0,0,0);
 int hit;
 assert(md_sequence_playhead(0,&hit)==-1&&!hit);
 md_midi(0xf8,0,0,0);
 assert(md_sequence_playhead(0,&hit)==1&&!hit);
 assert(md_sequence_playhead(1,&hit)==0&&!hit);
 for(unsigned pulse=0;pulse<96;pulse++)md_midi(0xf8,0,0,0);
 assert(md_journey_position()==1);
 assert(md_sequence_playhead(0,&hit)==0&&hit);
 assert(md_sequence_playhead(1,&hit)==0&&!hit);
 assert(!md_command(MD_SEQ_TRANSPOSE,0,0)&&!md_command(MD_SEQ_EXPRESSION,0,70)&&md_command(MD_SEQ_COPY,0,1));
 run(1);
 md_midi(0xfa,0,0,0);
 assert(md_sequence_playhead(0,&hit)==-1&&!hit);
 md_midi(0xfc,0,0,0);
 assert(md_sequence_playhead(1,&hit)==-1&&!hit);
 puts("PASS actual USB-clock step and hit state across Journey carries, phase resets and transport restart.");
}
static void factory_arrangements(){
 const int copper_step=252, chapel_of_fog=257, pearl_fern=269;
 md_init();
 for(int id=0;id<MD_BIOMES;id++){
  cmd(MD_SCENE,0,id);
  const md_patch preset=*md_state();
  const bool arranged=id==copper_step||id==chapel_of_fog||id==pearl_fern;
  assert(preset.biome_id==id&&md_validate(&preset)&&!md_unsaved());
  assert(preset.journey.mode==(arranged?2:0));
  cmd(MD_PLAY,0,1);
  assert(md_playing()&&md_journey_active()==arranged&&md_state()->biome_id==id);
  stop();
  if(!arranged)continue;

  const md_journey &journey=preset.journey;
  assert(journey.length==(id==chapel_of_fog?3:2)&&journey.section[0].biome==65535);
  cmd(MD_CLOCK_SOURCE,0,1);
  const md_patch before_start=*md_state();
  md_midi(0xfa,0,0,0);
  md_midi(0xf8,0,0,0);
  for(unsigned section=0;section<journey.length;section++){
   assert(md_journey_position()==int(section));
   const auto &current=journey.section[section];
   const int target=section==0?id:current.biome;
   assert(md_state()->biome_id==target&&md_state()->root==preset.root&&md_state()->scale==preset.scale);
   for(unsigned track=0;track<MD_TRACKS;track++)if(current.keep&(1u<<track)){
    assert(!memcmp(&md_state()->track[track],&preset.track[track],sizeof(md_track)));
   }
   for(unsigned pulse=0;pulse<96u*current.bars*current.repeats;pulse++)md_midi(0xf8,0,0,0);
  }
  assert(md_journey_position()==0&&md_playing());
  md_midi(0xfc,0,0,0);
  assert(!memcmp(md_state(),&before_start,sizeof before_start));
  cmd(MD_CLOCK_SOURCE,0,0);
  cmd(MD_JOURNEY_MODE,0,0);
  assert(!md_state()->journey.mode&&md_state()->journey.length==journey.length);
  assert(!memcmp(md_state()->journey.section,journey.section,sizeof journey.section));
  md_patch decoded;
  assert(md_decode(&decoded,md_state(),sizeof(md_patch))&&!decoded.journey.mode);
  assert(md_load(&decoded));
  cmd(MD_PLAY,0,1);
  assert(md_playing()&&!md_journey_active());
  stop();
 }
 puts("PASS all factory Journey defaults, three complete loops, anchor tracks, and persistence of an Off override.");
}
int main(int argc,char **argv){
 factory_arrangements();
 static_assert(sizeof(md_journey)==68&&sizeof(md_patch)==2604,"v10 tail");static_assert(offsetof(md_patch,journey)==2288,"whole v9 prefix retained");
 auto p=base();unsigned char v9[2288];memcpy(v9,&p,sizeof v9);uint32_t version=9;memcpy(v9+4,&version,4);md_patch migrated;
 assert(md_decode(&migrated,v9,sizeof v9)&&migrated.version==MD_PATCH_VERSION&&!migrated.journey.mode&&migrated.journey.length==1);assert(!memcmp((char*)&migrated+8,v9+8,sizeof v9-8));for(auto &s:migrated.journey.section)assert(s.biome==65535&&s.bars==4&&s.repeats==1&&!s.keep);
 for(int field=0;field<10;field++){auto bad=p;switch(field){case 0:bad.journey.mode=3;break;case 1:bad.journey.length=0;break;case 2:bad.journey.length=9;break;case 3:bad.journey.section[7].biome=MD_BIOMES;break;case 4:bad.journey.section[7].bars=0;break;case 5:bad.journey.section[7].bars=65;break;case 6:bad.journey.section[7].repeats=17;break;case 7:bad.journey.section[7].keep=64;break;case 8:bad.journey.section[7].transpose=25;break;case 9:bad.journey.section[7].reserved[1]=1;break;}assert(!md_validate(&bad));assert(!md_decode(&migrated,&bad,sizeof bad));}
 puts("PASS v9 migration preserves all 2288 prefix bytes except version; Journey off; invalid section data rejected.");
 assert(!md_command_valid(124)&&!md_command(124,0,0)&&!md_unsaved());for(int c=99;c<120;c++)assert(!md_command_valid(c));for(int c=128;c<MD_COMMAND_COUNT;c++)assert(md_command_valid(c));
 cmd(MD_JOURNEY_LENGTH,0,99);cmd(MD_JOURNEY_BARS,7,-9);cmd(MD_JOURNEY_TRANSPOSE,7,-99);cmd(MD_JOURNEY_KEEP_TRACK,7,11);assert(md_state()->journey.length==8&&md_state()->journey.section[7].bars==1&&md_state()->journey.section[7].transpose==-24&&md_state()->journey.section[7].keep==32);
 puts("PASS command namespace, reserved query rejection, bounded section 8 edits and independent carry bit.");
 md_init();assert(md_load(&p));md_patch original=*md_state();unsigned memory=md_memory_bytes();assert(!md_unsaved());cmd(MD_PLAY,0,1);assert(md_journey_position()==0&&!md_unsaved());run(88199);assert(md_journey_position()==0&&md_status()->step==15);run(1);assert(md_journey_position()==1&&md_status()->step==16&&md_state()->biome_id==171&&md_state()->tempo==120);assert(!memcmp(md_project_state(),&original,sizeof original));assert(!memcmp(&md_state()->track[1],&original.track[1],sizeof(md_track)));
 assert(!md_command(MD_MODEL,0,1)&&!md_command(MD_JOURNEY_BARS,0,2)&&!md_command(MD_RECORD,0,2));cmd(MD_MUTE,0,1);cmd(MD_MACRO,3,700);assert(!md_unsaved());run(88197);assert(md_playing());run(1);assert(!md_playing()&&!md_journey_active());assert(!memcmp(md_state(),&original,sizeof original)&&!md_unsaved());assert(md_memory_bytes()==memory);
 puts("PASS exact internal bar boundaries, Once completion, fixed tempo, carried track, protected edits, canonical export and unchanged saved baseline.");
 // A custom THIS BIOME remains an exact source even after visiting a factory scene.
 p=base();p.journey.mode=2;p.journey.length=3;p.journey.section[2]={65535,1,1,0,7,{0,0}};p.track[0].tone=123;p.sequence[0].steps[0].note=45;p.biome_id=-1;
 md_init();assert(md_load(&p));cmd(MD_PLAY,0,1);run(176400);assert(md_journey_position()==2&&md_state()->biome_id==-1&&md_state()->track[0].tone==123&&md_state()->sequence[0].steps[0].note==45&&md_state()->transpose==7);run(88200);assert(md_journey_position()==0&&md_playing());stop();assert(!memcmp(md_state(),&p,sizeof p));
 puts("PASS Loop, custom home return, transposition and lossless Stop restoration.");
 // USB MIDI clock, including longest durations, Continue, Stop and clock loss.
 p=base();p.journey.mode=2;p.journey.length=8;for(unsigned i=0;i<8;i++)p.journey.section[i]={uint16_t(169+i),64,16,63,0,{0,0}};
 md_init();assert(md_load(&p));cmd(MD_CLOCK_SOURCE,0,1);p=*md_state();md_midi(0xfa,0,0,0);for(unsigned t=0;t<8*16384;t++){for(unsigned pulse=0;pulse<6;pulse++)md_midi(0xf8,0,0,0);if(t%16384==0)assert(md_journey_position()==int(t/16384));}md_midi(0xf8,0,0,0);assert(md_journey_position()==0);md_midi(0xfc,0,0,0);assert(!md_journey_active()&&!memcmp(md_state(),&p,sizeof p));md_midi(0xfb,0,0,0);assert(md_journey_active());run(MD_RATE*2+256);assert(!md_playing()&&!md_journey_active()&&!memcmp(md_state(),&p,sizeof p));
 puts("PASS MIDI maximum 1024 bars per section / all 8 sections / loop / stop / clock loss restore.");
 // Rack switches cannot carry inactive synths into the kit. Shared tape keeps writing.
 p=base();p.journey.section[1]={174,1,1,63,0,{0,0}};md_init();assert(md_load(&p));cmd(MD_PLAY,0,1);run(88200);assert(md_journey_position()==1&&md_active_tracks()==4&&md_state()->kit.enabled);assert(!memcmp(&md_state()->track[0],&p.track[0],sizeof(md_track)));auto writes=md_status()->grain_writes;assert(writes>=88201);assert(run(44000)>.01);assert(md_status()->grain_writes>writes);stop();assert(!memcmp(md_state(),&p,sizeof p));
 puts("PASS six-synth to kit layout, legal carry tracks, continuous grain memory writes and audible output.");
 // Every factory source at highest master clock; no heap growth or non-finite audio.
 md_init();p=*md_state();p.journey.mode=2;p.journey.length=8;p.tempo=220;for(int id=0;id<MD_BIOMES;id+=8){for(int i=0;i<8;i++)p.journey.section[i]={uint16_t((id+i)%MD_BIOMES),1,1,uint8_t(i%2?0:63),int8_t(i-4),{0,0}};assert(md_load(&p));cmd(MD_PLAY,0,1);run(unsigned(MD_RATE*60.*4/220*8)+256);assert(!md_status()->nonfinite&&!md_status()->dropped_commands);stop();}
 puts("PASS all factory sources at 220 BPM, varied carries, transpositions, rack switches and stable bounded samples.");

 // A carried voice must remain sample-identical across section boundaries.
 md_init();cmd(MD_SCENE,0,64);p=*md_state();p.tempo=120;p.locked=1;p.macro[0]=500;p.macro[1]=0;p.macro[3]=0;p.fx_level=0;
 for(auto &t:p.track)t.mute=1;auto &tone=p.track[0];tone.mute=0;tone.model=MD_MELLOW;tone.note=48;tone.level=650;tone.attack=0;tone.decay=900;tone.hits=16;tone.length=16;tone.prob=1000;tone.send=0;tone.filter=1;tone.cutoff=600;
 auto capture=[&](const md_patch &q){md_init();assert(md_load(&q));cmd(MD_PLAY,0,1);std::vector<float> data;for(int i=0;i<1800;i++){run(128);data.insert(data.end(),lr,lr+256);}stop();return data;};
 auto reference=capture(p);p.journey.mode=2;p.journey.section[0]={65535,1,1,63,0,{0,0}};auto carried=capture(p);assert(reference==carried);
 puts("PASS carried warm voice remains sample-identical to ordinary playback through two section boundaries.");
 md_init();assert(md_load(&p));cmd(MD_PLAY,0,1);md_midi(0x90,48,100,0);run(MD_RATE);float last[2]={lr[0],lr[1]};
 // Render one frame to get an unambiguous final output sample, then stop.
 run(1);last[0]=lr[0];last[1]=lr[1];cmd(MD_PLAY,0,0);float jump=fmaxf(fabsf(lr[0]-last[0]),fabsf(lr[1]-last[1]));assert(jump<.001f);assert(!md_journey_active());cmd(MD_PANIC,0,0);assert(run(256)==0);
 printf("PASS manual Stop dry continuity (max edge %.9f), canonical restoration and exact Panic silence.\n",jump);
 if(argc>1){p=base();p.tempo=90;p.journey.mode=1;p.journey.length=4;for(int i=0;i<4;i++)p.journey.section[i]={uint16_t(i==0?169:i==1?171:i==2?174:176),4,1,uint8_t(i==1?2:0),0,{0,0}};md_init();assert(md_load(&p));FILE *f=fopen(argv[1],"wb");assert(f);cmd(MD_PLAY,0,1);run(MD_RATE*45,f);fclose(f);}
 printf("PASS Journey fixed memory accounting %u bytes; no hardware timing claim.\n",memory);
 sequence_tools();
 step_rules();
}
