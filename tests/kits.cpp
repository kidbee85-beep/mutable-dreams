#include "dsp/dreams.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstddef>
static float lr[256];
static double render(unsigned frames){double e=0;while(frames){unsigned n=frames>128?128:frames;md_render(lr,n);for(unsigned k=0;k<n*2;k++){assert(std::isfinite(lr[k])&&fabsf(lr[k])<1);e+=lr[k]*lr[k];}frames-=n;}return e;}
static void cmd(int c,int t,int v){assert(md_command(c,t,v));render(1);}
static void select(int id){cmd(MD_SCENE,0,id);if(md_pending_biome()>=0)cmd(MD_SCENE_CONFIRM,0,1);assert(md_state()->biome_id==id);}
int main(){
 static_assert(offsetof(md_patch,kit)==2100,"v7 prefix");static_assert(sizeof(md_patch)==2288,"v8 contract");static_assert(sizeof(md_patch)+28<=3840,"Biome sector capacity");static_assert(MD_COMMAND_COUNT<128,"UI command namespace");
 md_init();assert(!md_unsaved());cmd(MD_PLAY,0,1);render(44100);assert(!md_unsaved());cmd(MD_SEQ_STEP,0,3);assert(!md_unsaved());cmd(MD_KIT_PAD,3,4);assert(!md_unsaved());
 cmd(MD_MACRO,0,600);assert(md_unsaved());md_patch edited=*md_state();cmd(MD_SCENE,0,1);assert(md_pending_biome()==1&&!memcmp(&edited,md_state(),sizeof edited));assert(md_playing());cmd(MD_SCENE_CONFIRM,0,0);assert(md_pending_biome()==-1&&!memcmp(&edited,md_state(),sizeof edited));cmd(MD_MACRO,0,500);assert(!md_unsaved());cmd(MD_MACRO,0,620);md_mark_saved();assert(!md_unsaved());select(1);assert(!md_unsaved());cmd(MD_MACRO,0,777);select(2);assert(md_state()->biome_id==2&&!md_unsaved());cmd(MD_PLAY,0,0);md_patch edit=*md_state();edit.track[0].tone=333;assert(md_import_edit(&edit,sizeof edit));assert(md_unsaved());
 puts("PASS sound recall keeps unsaved Biome baseline");puts("PASS unsaved guard, cancellation, explicit discard, revert, saved baseline and cursor exclusions");
 cmd(MD_PLAY,0,0);unsigned char v7[2100];memcpy(v7,md_state(),2100);((uint32_t*)v7)[1]=7;md_patch migrated;assert(md_decode(&migrated,v7,sizeof v7));assert(migrated.version==9&&!migrated.kit.enabled);assert(!memcmp((char*)&migrated+8,v7+8,2092));assert(md_import(v7,sizeof v7));puts("PASS v7 migration preserves complete saved synth/sequence/FX prefix; kit disabled");
 select(64);md_patch base=*md_state();for(auto &t:base.track)t.mute=1;base.track[3].mute=0;base.track[3].level=900;base.track[3].speed=1;base.track[3].prob=1000;base.kit.enabled=1;base.macro[3]=0;base.fx_level=0;memset(base.kit.gates,0,sizeof base.kit.gates);
 for(int bank=0;bank<5;bank++)for(int pad=0;pad<8;pad++)for(int pitch=-12;pitch<=12;pitch+=12){md_init();base.kit.bank=bank;base.kit.pads[pad].tune=pitch;assert(md_load(&base));cmd(MD_KIT_TRIGGER,3,pad);double e=render(MD_RATE);printf("SAMPLE bank=%d pad=%d pitch=%d energy=%g\n",bank,pad,pitch,e);assert(e>.001);assert(render(MD_RATE)<1e-8);assert(!md_status()->nonfinite);}
 md_init();base.kit.bank=0;assert(md_load(&base));cmd(MD_KIT_TRIGGER,3,7);render(441);cmd(MD_KIT_BANK,3,4);assert(render(441)>.0001);puts("PASS kit bank switching preserves the previous sample tail");
 puts("PASS 40 original samples, all 3 pitches, finite bounded audio and silence after tails");
 md_init();base.kit.bank=0;assert(md_load(&base));md_track held[3];memcpy(held,&md_state()->track[3],sizeof held);for(int c=4;c<6;c++)md_midi(0x90+c,48,127,0);assert(!md_status()->events[4]&&!md_status()->events[5]);
 cmd(MD_KIT_GENERATE,3,1);cmd(MD_PLAY,0,1);assert(render(MD_RATE*4)>1);assert(md_status()->events[3]>10);assert(!md_status()->events[4]&&!md_status()->events[5]);cmd(MD_PLAY,0,0);cmd(MD_KIT_MODE,0,0);assert(md_active_tracks()==6&&!memcmp(&md_state()->track[3],held,sizeof held));puts("PASS four-track allocation, hidden MIDI ignored, all three dormant synth slots preserved");
 cmd(MD_KIT_MODE,0,1);cmd(MD_RECORD,3,1);cmd(MD_KIT_TRIGGER,3,6);assert(md_state()->kit.gates[6]&1);assert(md_state()->kit.step==1);assert(!md_state()->sequence[3].enabled);cmd(MD_RECORD,3,0);cmd(MD_KIT_STEP,3,31);cmd(MD_KIT_LENGTH,3,32);cmd(MD_KIT_STEP,3,31);cmd(MD_KIT_GATE,3,1);assert(md_state()->kit.gates[md_state()->kit.pad]&(1u<<31));puts("PASS kit step recording / simultaneous pad masks / step 32");
 auto saved=*md_state();cmd(MD_PANIC,0,0);assert(md_import(&saved,sizeof saved));assert(!memcmp(md_state(),&saved,sizeof saved));saved.kit.bank=5;assert(!md_load(&saved));saved=*md_state();saved.kit.length=0;assert(!md_load(&saved));saved=*md_state();saved.kit.pads[2].tune=13;assert(!md_load(&saved));puts("PASS kit persistence and malformed bank rejection");
 for(int b=0;b<5;b++){md_init();base.kit.bank=b;base.kit.hiss=100;base.track[3].send=1000;base.fx_level=900;base.macro[3]=1000;base.feedback=850;base.grain_noise=500;base.tempo=220;base.grain_size=1000;base.ext[3].delay_send=1000;base.ext[3].reverb_send=1000;for(auto &p:base.kit.pads)p.send=1000;assert(md_load(&base));cmd(MD_KIT_GENERATE,3,1);cmd(MD_PLAY,0,1);for(int j=0;j<800;j++){md_midi(0x93,36+(j%8),127,0);cmd(MD_KIT_TRIGGER,3,j%8);render(1102);}cmd(MD_PANIC,0,0);assert(render(44100)==0);assert(!md_status()->nonfinite&&!md_status()->dropped_commands);}
 puts("PASS dense kit/MIDI stealing + max grains/noise/delay/reverb, bounded audio, panic silence");
 for(int id=0;id<MD_BIOMES;id++){
  md_init();select(id);assert(md_validate(md_state()));
  if(id>=93){
   cmd(MD_PLAY,0,1);assert(render(MD_RATE*8)>.01);
   const bool kit=(id>=101&&id<MD_V13_BIOMES)||(id>=MD_V13_BIOMES&&(id-MD_V13_BIOMES)%8==4);
   assert(md_state()->kit.enabled==kit);
  }
 }
 puts("PASS all 157 factory patches validate; kit mode and synth allocation agree");
 bool seen[MD_BIOMES]={};int cursor=md_biome_step(-1,1);
 for(int i=0;i<MD_BIOMES;i++){
  assert(cursor>=0&&cursor<MD_BIOMES&&!seen[cursor]);seen[cursor]=true;
  if(i+1<MD_BIOMES){int next=md_biome_step(cursor,1);assert(md_biome_step(next,-1)==cursor);cursor=next;}
 }
 for(bool present:seen)assert(present);
 assert(md_biome_step(cursor,1)==cursor);
 puts("PASS curated wheel visits every original and new Biome exactly once, with stable inverse navigation");
}
