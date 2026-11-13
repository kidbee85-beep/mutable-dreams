/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#define main platform_main
#include "../base/host/plat_host.c"
#undef main
#include <assert.h>
static unsigned packed(const uint8_t *src,unsigned n,uint8_t *dst){unsigned at=0;for(unsigned i=0;i<n;){unsigned k=n-i>7?7:n-i;uint8_t mask=0;for(unsigned j=0;j<k;j++)mask|=(src[i+j]>>7)<<j;dst[at++]=mask;for(unsigned j=0;j<k;j++)dst[at++]=src[i+j]&127;i+=k;}return at;}
static uint8_t response[224];static uint32_t response_n;
static int req(int cmd,const uint8_t *data,unsigned n){uint8_t p[300]={0x7d,0x4d,0x44,cmd,1};assert(n<=sizeof p-5);if(n)memcpy(p+5,data,n);response_n=0;int rc=editor_reply(p,n+5,response,&response_n);assert(response_n<=sizeof response);return rc;}
static int wire(int cmd,const uint8_t *data,unsigned n){uint8_t p[96]={0x7d,0x4d,0x44,cmd,1};assert(n<=sizeof p-5);if(n)memcpy(p+5,data,n);hsx_n=0;assert(app_sysex(p,n+5)&&hsx_n>=8);return hsx_out[6];}
static unsigned begin(const md_patch *p,uint32_t crc){uint32_t h[]={sizeof *p,crc};uint8_t b[16];unsigned n=packed((void*)h,sizeof h,b);assert(!req(11,b,n));return response[0];}
static void chunks(const md_patch *p,unsigned token){for(unsigned off=0;off<sizeof *p;off+=196){uint8_t b[240]={token,off&127,off>>7};unsigned n=sizeof *p-off;if(n>196)n=196;n=packed((uint8_t*)p+off,n,b+3);assert(!req(12,b,n+3));}}
static void test_staging_replay(const md_patch *patch){
 uint8_t token=begin(patch,dream_crc(patch,sizeof *patch));
 const uint8_t *bytes=(const uint8_t*)patch;
 uint8_t packet[240]={token,0,0};
 unsigned n=packed(bytes,196,packet+3)+3;
 assert(!req(12,packet,n)&&editor_received==196);
 assert(!req(12,packet,n)&&editor_received==196);
 packet[n-1]^=1;
 assert(req(12,packet,n)==1&&editor_received==196);
 packet[n-1]^=1;
 packet[1]=197&127;packet[2]=197>>7;
 assert(req(12,packet,n)==1&&editor_received==196);
 packet[1]=1;packet[2]=0;n=packed(bytes+1,196,packet+3)+3;
 assert(req(12,packet,n)==1&&editor_received==196);
 assert(req(13,&token,1)==6);
 chunks(patch,token);
 assert(!req(13,&token,1)&&!memcmp(md_state(),patch,sizeof *patch));
 assert(req(13,&token,1)==6);
 puts("PASS exact upload block replay; changed, overlapping and out-of-order blocks rejected; commit runs once");
}
static void test_display_polarity(void){
 static uint16_t original[240*240];
 md_patch patch=*md_state();
 unsigned writes=store_writes;
 dream_ui.page=APPEARANCE;
 dream_prefs.motion=0;
 assert(!dream_inverted());
 md_tap(B_SEL);
 assert(dream_inverted()&&display_repaint&&store_writes==writes);
 assert(!memcmp(md_state(),&patch,sizeof patch));
 uint8_t extended=1;
 assert(!req(9,NULL,0)&&response_n==224&&response[221]==0);
 assert(!req(9,&extended,1)&&response_n==224&&response[221]==1);
 extended=2;assert(req(9,&extended,1)==1);
 toggle_display_polarity();
 const int pages[]={LIVE,APPEARANCE,GRAIN,SEQ_RULES,JOURNEY,MIXER};
 for(unsigned page=0;page<sizeof pages/sizeof pages[0];page++){
  dream_ui.page=pages[page];dream_ui.help=page==0?B_HOME:-1;
  timeline_update();sequence_view_update();
  app_redraw();draw_display();memcpy(original,fb,sizeof original);
  toggle_display_polarity();draw_display();
  for(unsigned i=0;i<240*240;i++){
   // Look keeps its two preview swatches in their named colours.
   if(page==1&&i/240>=51&&i/240<154)continue;
   assert((uint16_t)(original[i]^fb[i])==0xffffu);
  }
  toggle_display_polarity();draw_display();
  assert(!memcmp(original,fb,sizeof original));
 }
 dream_intro();app_redraw();draw_display();memcpy(original,fb,sizeof original);
 toggle_display_polarity();draw_display();
 for(unsigned i=0;i<240*240;i++)assert((uint16_t)(original[i]^fb[i])==0xffffu);
 toggle_display_polarity();dream_dismiss_intro();
 dream_ui.page=APPEARANCE;dream_ui.help=-1;
 md_tap(B_SEL);
 assert(!dream_save_preferences());
 app_init();
 assert(dream_inverted()&&!dream_prefs.motion);
 const uint8_t *stored=store[OBJ_PREFS];
 md_preferences previous;
 assert(store_len[OBJ_PREFS]==sizeof(md_saved_preferences));
 assert(dream_prefs_decode(&previous,stored,sizeof previous)&&!previous.motion);
 toggle_display_polarity();
 assert(!dream_save_preferences());
 app_init();
 assert(!dream_inverted());
 puts("PASS full-pixel RGB565 inversion across Atlas, Help, Grain, Steps, Journey, Mixer and Splash; exact return, SAVE persistence and legacy prefix");
}
static void test_bank_workspace(const md_patch *project){
 const unsigned biome=OBJ_USER+15,tone=OBJ_TONES+3;
 UserBiome world;
 ToneBank sounds;
 assert(plat_store_load(biome,&world,sizeof world)==sizeof world);
 assert(plat_store_load(tone,&sounds,sizeof sounds)==sizeof sounds);
 const uint8_t biome_name[]={0,15,0,0},tone_name[]={1,63,0,0};
 const unsigned token=begin(project,dream_crc(project,sizeof *project));
 const uint8_t packet_token=token;
 chunks(project,token);
 for(int i=0;i<4;i++){
  assert(app_backup_validate(tone,&sounds,sizeof sounds));
  assert(!req(15,biome_name,sizeof biome_name)&&response_n==24&&!memcmp(response,world.name,24));
  assert(app_backup_validate(biome,&world,sizeof world));
  assert(!req(15,tone_name,sizeof tone_name)&&response_n==24&&!memcmp(response,sounds.tone[15].name,24));
 }
 assert(!req(13,&packet_token,1)&&md_state()->tempo==project->tempo);
 world.name[23]='x';assert(!app_backup_validate(biome,&world,sizeof world));
 ToneBank invalid=sounds;invalid.used=0x10000;assert(!app_backup_validate(tone,&invalid,sizeof invalid));
 invalid=sounds;invalid.tone[15].ext.notes[2]=99;assert(!app_backup_validate(tone,&invalid,sizeof invalid));
 assert(!req(15,tone_name,sizeof tone_name)&&!memcmp(response,sounds.tone[15].name,24));
 puts("PASS alternating Biome and tone bank validation/query while a separate editor upload is in progress.");
}
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
  now_ms=starts[k];dream_ui.page=BANKS;bank_kind=0;bank_slot=15;bank_armed=-1;assert(bank_action(1)==5);
  now_ms+=BANK_CONFIRM_MS-1;assert(!bank_action(1));assert(bank_action(1)==5);now_ms+=BANK_CONFIRM_MS+1;assert(bank_action(1)==5);
  now_ms+=BANK_CONFIRM_MS+1;app_input_only();assert(bank_armed==-1);
  now_ms=starts[k];pulse_active=1;pulse_start[0]=now_ms;now_ms+=159;app_input_only();assert(pulse_active&1);now_ms++;app_input_only();assert(!(pulse_active&1));
  now_ms=starts[k];dream_ui.dirty=0;dream_ui.frame_at=now_ms;dream_ui.frame_wait=66;uint32_t frames=ui_frames;
  now_ms+=65;app_frame();assert(ui_frames==frames);now_ms++;app_frame();assert(ui_frames==frames+1);
  /* Already expired actions stay inactive even after a full counter cycle. */
  now_ms=starts[k];app_input_only();assert(!intro_active&&!editor_length&&bank_armed==-1&&!dream_ui.msg[0]&&!pulse_active);
 }
 now_ms=original_ms;
 puts("PASS clock rollover: live uploads, timeout rejection, overwrite confirmation, intro, message, pulse and UI cadence across UINT32_MAX");
}
static void test_user_bank_workflow(void){
 dream_ui.page=BANKS;dream_ui.help=B_SAVE;bank_slot=13;bank_kind=0;bank_armed=-1;
 const int obj=bank_object();
 assert(!plat_store_save(obj,&bank_io.biome,0));
 memcpy(bank_name,"BANK TEST",sizeof "BANK TEST");
 memset(bank_name+sizeof "BANK TEST",0,sizeof bank_name-sizeof "BANK TEST");
 dream_cmd(MD_TEMPO,0,103);run_ms(20);
 held_btn=1u<<B_SAVE;app_input_only();now_ms+=80;held_btn=0;app_input_only();
 assert(plat_store_size(obj)==sizeof(UserBiome)&&!md_unsaved());
 UserBiome copy;
 assert(plat_store_load(obj,&copy,sizeof copy)==sizeof copy);
 assert(!strcmp(copy.name,"BANK TEST")&&copy.patch.tempo==103);
 dream_cmd(MD_TEMPO,0,107);run_ms(20);md_tap(B_SAVE);assert(bank_armed>=0);
 const int armed=bank_armed;
 const uint8_t query[]={0,13,1,0};
 assert(!wire(15,query,sizeof query)&&bank_armed==armed&&dream_ui.track==0);
 md_tap(B_SEL);assert(plat_store_load(obj,&copy,sizeof copy)==sizeof copy&&copy.patch.tempo==107);
 md_tap(B_REC);md_tap(B_REC);assert((bank_armed&BANK_CLEAR_ARM)&&plat_store_size(obj)>0);
 md_tap(B_HOME);assert(plat_store_size(obj)>0&&bank_armed==-1);
 dream_ui.page=BANKS;md_tap(B_REC);md_tap(B_SEL);assert(plat_store_size(obj)<0);
 dream_cmd(MD_TEMPO,0,110);run_ms(20);store_fail=1;
 assert(bank_action(BANK_SAVE_ASK)==7&&md_unsaved()&&plat_store_size(obj)<0);
 store_fail=0;
 bank_kind=1;bank_slot=62;dream_ui.track=0;
 assert(!bank_action(BANK_SAVE_REPLACE));
 const uint8_t clear_tone[]={1,62,0,4},neighbor[]={1,63,0,0};
 assert(!wire(15,clear_tone,sizeof clear_tone));
 assert(!wire(15,neighbor,sizeof neighbor));
 assert(wire(15,(uint8_t[]){1,62,0,0},4)==3);
 puts("PASS SAVE after help, named Biome, read-only polling during overwrite, explicit clear and retained neighboring tone");
}
int main(void){
 boot();run_ms(100);dream_dismiss_intro();md_patch initial=*md_state(),desired=initial;desired.tempo=92;desired.journey.length=8;desired.journey.section[7].biome=176;desired.journey.section[7].repeats=5;desired.journey.section[7].keep=19;
 /* Reserved UI-query IDs must reject a whole mixed DSP batch atomically. */
 uint8_t mixed[8]={MD_TEMPO,0,100,64,124,0,0,64};assert(wire(2,mixed,8)==1);run_ms(10);assert(md_state()->tempo==initial.tempo&&!md_unsaved());
 uint8_t extended[10]={2,1,7,48,65,124,0,0,0,64};assert(wire(16,extended,10)==1);run_ms(10);assert(md_state()->journey.section[7].biome==65535);
 assert(!wire(16,extended,5));run_ms(10);assert(md_state()->journey.section[7].biome==176);assert(md_load(&initial));
 assert(!req(7,0,0)&&response_n==32);assert(!req(9,0,0)&&response_n==224);
 assert(!wire(21,NULL,0)&&hsx_n==13);
 /* A helper alone is insufficient: exercise the real SysEx dispatcher too. */
 uint8_t midi_track=0;assert(!wire(18,&midi_track,1));assert(!req(18,&midi_track,1)&&response_n==28);
 int32_t midi_state[7];memcpy(midi_state,response,28);assert(midi_state[0]==-1&&midi_state[1]==0&&midi_state[2]==1000&&midi_state[3]==0);
 midi_track=6;assert(wire(18,&midi_track,1)==1);midi_track=0;assert(wire(18,&midi_track,0)==1);
 assert(!memcmp(md_state(),&initial,sizeof initial));puts("PASS MIDI diagnostics traverses real SysEx, is read-only and rejects missing/out-of-range tracks.");
 test_staging_replay(&desired);assert(md_load(&initial));


 dream_command(110,0,15);dream_command(117,0,3);assert(!req(17,0,0)&&response_n==28);int32_t creative_status[6];memcpy(creative_status,response,24);assert(creative_status[0]==15&&creative_status[1]==3&&creative_status[2]==-1);uint8_t extra_byte=0;assert(req(17,&extra_byte,1)==1);
 assert(!req(8,0,0));assert(md_stored_size==2604);assert(!memcmp(md_stored_snapshot,&initial,sizeof initial));
 dream_cmd(MD_TEMPO,0,110);run_ms(20);assert(!memcmp(md_stored_snapshot,&initial,sizeof initial));
 assert(app_backup_validate(0,&initial,sizeof initial));
 md_patch legacy=initial;legacy.version=4;assert(app_backup_validate(0,&legacy,652));
 legacy.version=5;assert(app_backup_validate(0,&legacy,1292));
 legacy.version=6;assert(app_backup_validate(0,&legacy,1292));
 UserBiome oldbiome={0};oldbiome.magic=0x4d445542;memcpy(oldbiome.name,"LEGACY WORLD",12);oldbiome.patch=legacy;assert(app_backup_validate(OBJ_USER,&oldbiome,1320));
 assert(!plat_store_save(OBJ_USER,&oldbiome,1320));bank_kind=0;bank_slot=0;md_mark_saved();assert(!bank_action(0));assert(md_state()->version==MD_PATCH_VERSION&&!md_state()->sequence[0].enabled);
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
 test_bank_workspace(&desired);
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
 /* 108 writes the UI preference; 124 is only the legacy value query. */
 dream_command(108,0,2);assert(dream_prefs.button_glow==2);assert(!req(9,0,0)&&response_n==224);
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


 /* An explicit successful live import replaces a pending bank request; invalid
  * imports preserve that request. SAVE must then be available on first press. */
 assert(md_load(&initial));dream_cmd(MD_CUTOFF,0,123);run_ms(20);bank_kind=0;bank_slot=0;assert(bank_action(0)==8&&bank_pending==0);md_patch invalid_live=initial;invalid_live.magic=0;assert(!dream_install_edit(&invalid_live,sizeof invalid_live)&&bank_pending==0);assert(dream_install_edit(&initial,sizeof initial)&&bank_pending==-1&&bank_armed==-1);
 puts("PASS invalid live import keeps the pending bank guard; successful explicit import clears it and restores SAVE availability.");
 _Static_assert(sizeof(md_preferences)==156,"Preference revision 2 retains old 28-byte prefix");
 md_preferences v1=dream_prefs;v1.revision=1;v1.midi_route[0]=9;assert(app_backup_validate(OBJ_PREFS,&v1,28));assert(!plat_store_save(OBJ_PREFS,&v1,28));app_init();assert(md_midi_route(0)==9&&dream_prefs.revision==2&&!md_mapping(15)->enabled);
 dream_command(MD_LEARN_TARGET,15,5);dream_command(MD_LEARN_TRACK,15,4);dream_command(MD_LEARN_ARM,15,1);run_ms(20);md_midi(0xb4,74,33,0);assert(md_mapping(15)->enabled&&md_mapping(15)->target==5);
 dream_ui.page=MIDI_LEARN;md_tap(B_SAVE);app_init();assert(md_mapping(15)->enabled&&md_mapping(15)->cc==74&&md_mapping(15)->track==4&&md_midi_route(0)==9&&!md_learn_picked(15));
 bad_pref=dream_prefs;bad_pref.mapping[15].cc=64;assert(!app_backup_validate(OBJ_PREFS,&bad_pref,sizeof bad_pref));bad_pref=dream_prefs;bad_pref.revision=1;assert(!app_backup_validate(OBJ_PREFS,&bad_pref,sizeof bad_pref));
 puts("PASS v1 preference migration and sixteen MIDI mappings saved/rebooted with routes intact; malformed mappings rejected.");
 md_saved_preferences extended_prefs={dream_prefs,{.version=1,.touch=65}};
 extended_prefs.performance.favorites[230/8]=1u<<(230%8);
 assert(app_backup_validate(OBJ_PREFS,&extended_prefs,sizeof extended_prefs));
 assert(app_backup_validate(OBJ_PREFS,&extended_prefs,sizeof extended_prefs.base));
 assert(!app_backup_validate(OBJ_PREFS,&extended_prefs,sizeof extended_prefs-1));
 assert(!plat_store_save(OBJ_PREFS,&extended_prefs,sizeof extended_prefs));
 app_backup_restored();
 assert(biome_favorite(230)&&performance_prefs.touch==65&&md_mapping(15)->enabled);
 extended_prefs.performance.touch=101;
 assert(!app_backup_validate(OBJ_PREFS,&extended_prefs,sizeof extended_prefs));
 extended_prefs.performance.touch=65;extended_prefs.performance.version=2;
 assert(!app_backup_validate(OBJ_PREFS,&extended_prefs,sizeof extended_prefs));
 puts("PASS extended preferences restore, stable old prefix, favorites and velocity touch; corrupt trailer rejected.");
 test_display_polarity();test_user_bank_workflow();
 {/* Editor names (command 116): 0 ends the name, control characters are spaces, earlier gaps spaces; a restore
   * still takes a backup whose older names carry control characters. */
  for(int i=0;i<23;i++){dream_command(115,0,i);dream_command(116,0,i<2?"AB"[i]:0);}
  assert(!memcmp(bank_name,"AB",3)&&dream_name_valid(bank_name));
  dream_command(115,0,1);dream_command(116,0,7);assert(bank_name[1]==' '&&dream_name_valid(bank_name));
  dream_command(115,0,5);dream_command(116,0,'Z');assert(!memcmp(bank_name,"A    Z",7)&&dream_name_valid(bank_name));
  dream_command(115,0,3);dream_command(116,0,0);assert(!memcmp(bank_name,"A  ",4)&&!bank_name[5]&&dream_name_valid(bank_name));
  dream_command(115,0,0);dream_command(116,0,300);assert(bank_name[0]=='~'&&dream_name_valid(bank_name));
  static UserBiome legacy_name;memset(&legacy_name,0,sizeof legacy_name);legacy_name.magic=0x4d445542;memcpy(legacy_name.name,"OLD\001NAME",8);legacy_name.patch=*md_state();
  assert(!dream_name_valid(legacy_name.name)&&app_backup_validate(OBJ_USER,&legacy_name,sizeof legacy_name));
  legacy_name.name[20]='x';assert(!app_backup_validate(OBJ_USER,&legacy_name,sizeof legacy_name));
  puts("PASS editor bank names stay printable and terminated; restore keeps older names, rejects unterminated ones.");}
 {/* app_overload runs in the audio interrupt: it queues ECO and nothing else, not even the main loop's
   * "STOP JOURNEY" message when the queue is full during a Journey. */
  dream_cmd(MD_JOURNEY_MODE,0,1);dream_cmd(MD_QUALITY,0,1);run_ms(20);dream_cmd(MD_PLAY,0,1);run_ms(20);assert(md_journey_active());
  dream_ui.msg[0]=0;uint32_t before=overloads,drops=md_status()->dropped_commands;
  while(md_command_space())assert(md_command(MD_LEVEL,0,500));
  app_overload();
  // F-3: ECO for the render at once, nothing queued, the saved quality kept
  assert(overloads==before+1&&!dream_ui.msg[0]&&md_status()->dropped_commands==drops&&!md_quality_active()&&md_state()->quality==1);
  run_ms(20);app_overload();run_ms(20);assert(!md_quality_active()&&md_state()->quality==1&&!dream_ui.msg[0]);
  dream_cmd(MD_PLAY,0,0);run_ms(20);
  puts("PASS overload from the audio interrupt: ECO for the render, no message or announcer, nothing queued, the saved quality kept.");}
 puts("PASS editor capabilities/state, atomic snapshot, bounded upload, CRC/version/range/timeout/busy rejection, all 16/64 slots, overwrite guard, backup save lock, saved-state reload, LED preference and legacy settings");return 0;
}
