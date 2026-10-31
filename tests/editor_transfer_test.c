/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#define main platform_main
#include "../base/host/plat_host.c"
#undef main
#include <assert.h>
static unsigned packed(const uint8_t *src,unsigned n,uint8_t *dst){unsigned at=0;for(unsigned i=0;i<n;){unsigned k=n-i>7?7:n-i;uint8_t mask=0;for(unsigned j=0;j<k;j++)mask|=(src[i+j]>>7)<<j;dst[at++]=mask;for(unsigned j=0;j<k;j++)dst[at++]=src[i+j]&127;i+=k;}return at;}
static uint8_t response[224];static uint32_t response_n;
static int req(int cmd,const uint8_t *data,unsigned n){uint8_t p[300]={0x7d,0x4d,0x44,cmd,1};assert(n<=sizeof p-5);if(n)memcpy(p+5,data,n);response_n=0;int rc=editor_reply(p,n+5,response,&response_n);assert(response_n<=sizeof response);return rc;}
static unsigned begin(const md_patch *p,uint32_t crc){uint32_t h[]={sizeof *p,crc};uint8_t b[16];unsigned n=packed((void*)h,sizeof h,b);assert(!req(11,b,n));return response[0];}
static void chunks(const md_patch *p,unsigned token){for(unsigned off=0;off<sizeof *p;off+=196){uint8_t b[240]={token,off&127,off>>7};unsigned n=sizeof *p-off;if(n>196)n=196;n=packed((uint8_t*)p+off,n,b+3);assert(!req(12,b,n+3));}}
static void test_clock_rollover(void){
 uint32_t original_ms=now_ms;
 const uint32_t starts[]={0,0x7ffffff0u,0xfffff000u,0xfffffc17u,0xffffffffu};
 for(unsigned k=0;k<sizeof starts/sizeof starts[0];k++){
  now_ms=starts[k];md_patch desired=*md_state();desired.tempo=87+k;
  unsigned token=begin(&desired,dream_crc(&desired,sizeof desired));uint8_t t=token;
  now_ms+=4000;chunks(&desired,token);now_ms+=4999;assert(!req(13,&t,1));assert(md_state()->tempo==87+(int)k);
  token=begin(&desired,dream_crc(&desired,sizeof desired));t=token;chunks(&desired,token);now_ms+=5001;
  assert(req(13,&t,1)==6);app_input_only();assert(!editor_length);
  now_ms=starts[k];dream_intro();int got=-1;assert(app_expect("intro",&got)&&got);
  now_ms+=3199;assert(app_expect("intro",&got)&&got);now_ms++;app_input_only();assert(app_expect("intro",&got)&&!got);
  now_ms=starts[k];app_say("ROLLOVER","");now_ms+=1399;app_input_only();assert(dream_ui.msg[0]);now_ms++;app_input_only();assert(!dream_ui.msg[0]);
  now_ms=starts[k];bank_kind=0;bank_slot=15;bank_armed=-1;assert(bank_action(1)==5);
  now_ms+=2999;assert(!bank_action(1));assert(bank_action(1)==5);now_ms+=3001;assert(bank_action(1)==5);
  now_ms+=3001;app_input_only();assert(bank_armed==-1);
  now_ms=starts[k];pulse_active=1;pulse_start[0]=now_ms;now_ms+=159;app_input_only();assert(pulse_active&1);now_ms++;app_input_only();assert(!(pulse_active&1));
  now_ms=starts[k];dream_ui.dirty=0;dream_ui.frame_at=now_ms;dream_ui.frame_wait=66;uint32_t frames=ui_frames;
  now_ms+=65;app_frame();assert(ui_frames==frames);now_ms++;app_frame();assert(ui_frames==frames+1);
  /* Already expired actions stay inactive even after a full counter cycle. */
  now_ms=starts[k];app_input_only();assert(!intro_active&&!editor_length&&bank_armed==-1&&!dream_ui.msg[0]&&!pulse_active);
 }
 now_ms=original_ms;
 puts("PASS clock rollover: live uploads, timeout rejection, overwrite confirmation, intro, message, pulse and UI cadence across UINT32_MAX");
}
int main(void){
 boot();run_ms(100);dream_dismiss_intro();md_patch initial=*md_state(),desired=initial;desired.tempo=92;
 assert(!req(7,0,0)&&response_n==32);assert(!req(9,0,0)&&response_n==224);
 assert(!req(8,0,0));assert(md_stored_size==2288);assert(!memcmp(md_stored_snapshot,&initial,sizeof initial));
 dream_cmd(MD_TEMPO,0,110);run_ms(20);assert(!memcmp(md_stored_snapshot,&initial,sizeof initial));
 assert(app_backup_validate(0,&initial,sizeof initial));
 md_patch legacy=initial;legacy.version=4;assert(app_backup_validate(0,&legacy,652));
 legacy.version=5;assert(app_backup_validate(0,&legacy,1292));
 legacy.version=6;assert(app_backup_validate(0,&legacy,1292));
 UserBiome oldbiome={0};oldbiome.magic=0x4d445542;memcpy(oldbiome.name,"LEGACY WORLD",12);oldbiome.patch=legacy;assert(app_backup_validate(OBJ_USER,&oldbiome,1320));
 assert(!plat_store_save(OBJ_USER,&oldbiome,1320));bank_kind=0;bank_slot=0;md_mark_saved();assert(!bank_action(0));assert(md_state()->version==9&&!md_state()->sequence[0].enabled);
 assert(md_state()->tempo==initial.tempo); /* validation never loads the old project */assert(!app_backup_validate(1,&initial,sizeof initial));assert(!app_backup_validate(0,&initial,20));
 md_patch bad=initial;bad.version=77;assert(!app_backup_validate(0,&bad,sizeof bad));bad=initial;bad.track[2].model=99;assert(!app_backup_validate(0,&bad,sizeof bad));
 for(unsigned obj=0;obj<23;obj++)if(obj!=1)assert(app_backup_validate(obj,0,0));
 unsigned token=begin(&desired,dream_crc(&desired,sizeof desired));uint8_t t=token;
 assert(req(13,&t,1)==6);chunks(&desired,token);assert(!req(13,&t,1));assert(md_state()->tempo==92);
 token=begin(&initial,123);t=token;chunks(&initial,token);assert(req(13,&t,1)==6);assert(md_state()->tempo==92);
 token=begin(&initial,dream_crc(&initial,sizeof initial));t=token;now_ms+=5001;assert(req(13,&t,1)==6);
 token=begin(&initial,dream_crc(&initial,sizeof initial));t=token;chunks(&initial,token);dream_cmd(MD_PLAY,0,1);run_ms(20);assert(req(13,&t,1)==4);dream_cmd(MD_PLAY,0,0);run_ms(20);
 uint32_t writes=store_writes;app_backup_hold(1);bank_slot=15;bank_kind=0;assert(bank_action(1)==4);md_tap(B_SAVE);assert(store_writes==writes);app_backup_flush();assert(store_writes==writes);app_backup_hold(0);
 for(int kind=0;kind<2;kind++)for(int slot=0;slot<(kind?64:16);slot++){bank_kind=kind;bank_slot=slot;assert(!bank_action(2));int obj=bank_object();assert(app_backup_validate(obj,store[obj],store_len[obj]));}
 bank_kind=0;bank_slot=15;writes=store_writes;bank_armed=-1;assert(bank_action(1)==5&&store_writes==writes);assert(!bank_action(1));
 UserBiome u=user_biome_io;u.name[23]='x';assert(!app_backup_validate(18,&u,sizeof u));
 ToneBank tones=tone_bank_io;tones.used=0x10000;assert(!app_backup_validate(22,&tones,sizeof tones));
 tones=tone_bank_io;tones.tone[15].ext.notes[2]=99;assert(!app_backup_validate(22,&tones,sizeof tones));
 assert(!plat_store_save(OBJ_PATCH,&desired,sizeof desired));app_backup_restored();assert(md_state()->tempo==92&&!md_playing()&&!md_clock_source()&&!dream_reloading);

 /* Sound recall is an edit, not a saved whole-Biome baseline. */
 bank_kind=1;bank_slot=0;dream_ui.track=0;assert(!bank_action(2));
 dream_cmd(MD_TONE,0,333);run_ms(20);md_mark_saved();assert(!md_unsaved());
 assert(!bank_action(0));assert(md_unsaved());
 dream_cmd(MD_SCENE,0,2);run_ms(20);assert(md_pending_biome()==2);
 md_tap(B_HOME);run_ms(20);assert(md_pending_biome()==-1);
 bank_kind=0;bank_slot=15;assert(bank_action(0)==8&&bank_pending==15);
 md_tap(B_HOME);run_ms(20);assert(bank_pending==-1&&md_unsaved());
 assert(bank_action(0)==8);md_tap(B_SEL);run_ms(20);assert(bank_pending==-1&&!md_unsaved());
 dream_cmd(MD_KIT_MODE,0,1);run_ms(20);dream_ui.track=3;bank_kind=1;
 assert(bank_action(2)==6&&bank_action(0)==6);
 dream_cmd(MD_KIT_MODE,0,0);run_ms(20);bank_kind=0;dream_ui.track=0;
 puts("PASS tone recall keeps Biome dirty; user Biome replacement guarded; kits use complete Biome banks");
 test_clock_rollover();
 dream_cmd(MD_CLOCK_SOURCE,0,1);run_ms(20);assert(app_backup_playing());assert(!dream_install(&initial,sizeof initial));
 /* The new LED preference is persistent and accepts saved 1.2 preferences. */
 dream_command(124,0,2);assert(dream_prefs.button_glow==2);assert(!req(9,0,0)&&response_n==224);
 uint32_t glow_state;memcpy(&glow_state,response+220,4);assert(glow_state==2);
 uint32_t old_pref[3]={0x4d445046,1,0};
 assert(app_backup_validate(OBJ_PREFS,old_pref,sizeof old_pref));
 dream_command(MD_MIDI_ROUTE,2,11);dream_command(MD_MIDI_ROUTE,3,10);dream_command(MD_MIDI_MACRO,0,0);
 dream_command(MD_MIDI_MACRO,0,-1);assert(!dream_prefs.midi_macro);
 dream_command(MD_MIDI_MACRO,0,2);assert(dream_prefs.midi_macro);
 dream_command(MD_MIDI_MACRO,0,0);
 assert(dream_prefs.midi_route[2]==11&&dream_prefs.midi_route[3]==10&&!dream_prefs.midi_macro);
 assert(app_backup_validate(OBJ_PREFS,&dream_prefs,sizeof dream_prefs));
 md_preferences bad_pref=dream_prefs;bad_pref.midi_route[2]=17;assert(!app_backup_validate(OBJ_PREFS,&bad_pref,sizeof bad_pref));
 assert(!plat_store_save(OBJ_PREFS,&dream_prefs,sizeof dream_prefs));app_init();assert(dream_prefs.button_glow==2&&md_midi_route(2)==11&&md_midi_route(3)==10&&!md_midi_macro_enabled());
 assert(!plat_store_save(OBJ_PREFS,old_pref,sizeof old_pref));app_init();assert(dream_prefs.button_glow==1&&dream_prefs.boot_sound==0&&md_midi_route(2)==3&&md_midi_macro_enabled());
 puts("PASS editor capabilities/state, atomic snapshot, bounded upload, CRC/version/range/timeout/busy rejection, all 16/64 slots, overwrite guard, backup save lock, saved-state reload, LED preference and legacy settings");return 0;
}
