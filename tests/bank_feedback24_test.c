/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Actual app bank/guard/MIDI acceptance on the serialized host platform.
 * MIDI enters the host queue and is consumed by app_render, as on the device.
 * No concurrent md_midi thread, hardware, user bank image or disk flash file. */
#ifdef NDEBUG
#error "Bank acceptance requires active assertions"
#endif
#define main platform_main
#include "../base/host/plat_host.c"
#undef main
#include <assert.h>

typedef struct {
 int clock,quantize,macro,input[6],output[6];
 unsigned generation;md_midi_mapping map[16];
} MidiSession;
static const int inputs[6]={9,4,12,1,7,15};
static const int outputs[6]={16,2,11,5,0,13};
static const int ports[3]={PORT_USB,PORT_TRS,PORT_BLE};
static UserBiome saved_biome;
static UserTone saved_tone;
static unsigned recalls,note_checks;

static void command(int cmd,int track,int value){assert(dream_cmd(cmd,track,value));run_ms(10);assert(md_command_space()==64);}
static void midi(int port,unsigned status,unsigned a,unsigned b){
 assert(mi_w-mi_r<MQ);unsigned at=mi_w++%MQ;
 min_port[at]=(uint32_t)port;
 min_q[at]=(status>=0xf8?0x0fu:status>>4)|(status<<8)|(a<<16)|(b<<24);
 run_ms(10);assert(mi_r==mi_w);
}
static void tap(int b){held_btn=1u<<b;run_ms(35);held_btn=0;run_ms(35);}
static void capture(MidiSession *s){
 uint32_t f=plat_lock();s->clock=md_clock_source();s->quantize=md_midi_quantize();s->macro=md_midi_macro_enabled();s->generation=md_mapping_generation();
 for(unsigned i=0;i<6;i++){s->input[i]=md_midi_route(i);s->output[i]=md_midi_out_channel(i);}
 for(unsigned i=0;i<16;i++)s->map[i]=*md_mapping(i);
 plat_unlock(f);
}
static void same_session(const MidiSession *s){
 assert(md_clock_source()==s->clock&&md_midi_quantize()==s->quantize&&md_midi_macro_enabled()==s->macro);
 assert(md_mapping_generation()==s->generation);
 for(unsigned i=0;i<6;i++){assert(md_midi_route(i)==s->input[i]);assert(md_midi_out_channel(i)==s->output[i]);}
 for(unsigned i=0;i<16;i++)assert(!memcmp(md_mapping(i),&s->map[i],sizeof s->map[i]));
}
static void set_session(int clock,int alternate){
 command(MD_CLOCK_SOURCE,0,clock);command(MD_MIDI_QUANTIZE,0,alternate);command(MD_MIDI_MACRO,0,!alternate);
 for(unsigned i=0;i<6;i++){command(MD_MIDI_ROUTE,i,inputs[i]);uint32_t f=plat_lock();md_set_midi_out(i,outputs[i]);plat_unlock(f);}
 for(unsigned i=0;i<16;i++){
  md_midi_mapping m={(uint8_t)(i%5!=0),(uint8_t)(i%4),(uint8_t)(i%17),(uint8_t)(70+i),(uint8_t)(i%12),(uint8_t)((i+2)%6),{0,0}};
  assert(md_mapping_valid(&m));uint32_t f=plat_lock();assert(md_set_mapping(i,&m));plat_unlock(f);
 }
 assert(md_mapping_generation()>=16);md_mark_saved();
}
static md_patch live_patch(int tempo){
 md_patch p=*md_state();p.tempo=tempo;p.biome_id=-1;p.root=0;p.scale=0;p.transpose=0;p.journey.mode=0;p.kit.enabled=0;p.fx_level=0;p.macro[1]=p.macro[3]=0;
 for(unsigned i=0;i<6;i++){
  md_track *t=&p.track[i];t->model=MD_MELLOW;t->mute=0;t->level=600;t->note=48+i;t->speed=1;t->ratchet=1;t->prob=1000;t->tone=281;
  p.sequence[i].enabled=1;p.sequence[i].length=1;p.sequence[i].edit=0;
  p.sequence[i].steps[0]=(md_step){(uint8_t)(48+i),90,100,100};
 }
 assert(md_validate(&p));return p;
}
static void fixtures(void){
 md_patch p=live_patch(97);assert(md_load(&p));bank_kind=0;bank_slot=12;dream_ui.page=BANKS;bank_armed=-1;
 memset(bank_name,0,sizeof bank_name);memcpy(bank_name,"CLOCK BIOME",11);assert(!bank_action(BANK_SAVE_REPLACE));
 assert(plat_store_load(OBJ_USER+12,&saved_biome,sizeof saved_biome)==sizeof saved_biome);
 p=live_patch(113);p.track[4].model=MD_FM;p.track[4].tone=719;p.track[4].harm=417;p.ext[4].glide=231;
 assert(md_load(&p));bank_kind=1;bank_slot=35;dream_ui.track=4;memcpy(bank_name,"CLOCK TONE",11);assert(!bank_action(BANK_SAVE_REPLACE));
 ToneBank tb;assert(plat_store_load(OBJ_TONES+2,&tb,sizeof tb)==sizeof tb);saved_tone=tb.tone[3];
}
static void incoming_routes(const MidiSession *s){
 for(unsigned p=0;p<3;p++)for(unsigned t=0;t<6;t++){
  uint32_t before[6];for(unsigned j=0;j<6;j++)before[j]=md_status()->events[j];
  unsigned note=49+t*2;int expected=s->quantize?md_quantize(note,md_state()->root,md_state()->scale):(int)note;
  midi(ports[p],0x90u|(s->input[t]-1u),note,101);
  for(unsigned j=0;j<6;j++)assert(md_status()->events[j]==before[j]+(j==t));
  assert(md_status()->last_note[t]==expected&&md_midi_held_port(t)==ports[p]);
  midi(ports[p],0x80u|(s->input[t]-1u),note,0);assert(md_midi_held_port(t)==-1);note_checks++;
 }
 same_session(s);
}
static unsigned pack(const void *data,unsigned n,uint8_t *out){
 const uint8_t *p=data;unsigned at=0;
 for(unsigned i=0;i<n;){unsigned k=n-i>7?7:n-i;uint8_t mask=0;for(unsigned j=0;j<k;j++)mask|=(p[i+j]>>7)<<j;out[at++]=mask;for(unsigned j=0;j<k;j++)out[at++]=p[i+j]&127;i+=k;}
 return at;
}
static int upload_begin(const md_patch *p){
 uint32_t head[2]={sizeof *p,dream_crc(p,sizeof *p)};uint8_t message[32]={0x7d,0x4d,0x44,11,1};unsigned n=pack(head,sizeof head,message+5);
 hsx_n=0;assert(app_sysex(message,n+5)&&hsx_n>=8);return hsx_out[6];
}
static void stopped_busy_writes(const MidiSession *s){
 const unsigned writes=store_writes,tries=store_tries;md_patch before=*md_state();
 for(int action=BANK_SAVE_ASK;action<=BANK_CLEAR;action++)assert(bank_action(action)==4);
 dream_ui.page=LIVE;md_tap(B_SAVE);dream_ui.page=BANKS;
 assert(!dream_install(&before,sizeof before)&&!dream_install_edit(&before,sizeof before));
 assert(upload_begin(&before)==4&&app_backup_playing());
 size_t bytes=sim_backup_image();assert(sim_rd32(sim_img+8)==1&&sim_restore_image(bytes)==10);
 assert(!memcmp(md_state(),&before,sizeof before)&&store_writes==writes&&store_tries==tries);same_session(s);
 app_backup_hold(1);assert(bank_action(BANK_LOAD)==4);app_backup_hold(0);
 assert(store_writes==writes&&store_tries==tries);
}
static void generated_output_and_playing_lock(const MidiSession *s){
 const int port=s->clock==1?PORT_USB:s->clock==2?PORT_TRS:PORT_BLE;
 /* The other two physical input transports must not start this clock. */
 for(unsigned p=0;p<3;p++)if(ports[p]!=port){midi(ports[p],0xfa,0,0);midi(ports[p],0xf8,0,0);assert(!md_playing());}
 n_out=0;midi(port,0xfa,0,0);midi(port,0xf8,0,0);assert(md_playing());
 /* The audio producer queues output; exercise its real main-loop consumer
  * explicitly instead of assuming a display redraw fell in these20ms. */
 midi_out_drain();
 for(unsigned t=0;t<6;t++){
  unsigned found=0;for(unsigned j=0;j<n_out;j++)if(((out_pkt[j]>>8)&0xf0u)==0x90u&&((out_pkt[j]>>16)&127u)==48+t){
   assert(s->output[t]&&((out_pkt[j]>>8)&15u)==(unsigned)s->output[t]-1u);assert(out_to[j]&TO_USB);found++;
  }
  if(found!=(unsigned)(s->output[t]!=0)){
   fprintf(stderr,"OUT mismatch track=%u expected_channel=%d expected_note=%u found=%u packets=%u clock=%d quantize=%d\n",t,s->output[t],48+t,found,n_out,s->clock,s->quantize);
   for(unsigned j=0;j<n_out;j++)fprintf(stderr,"OUT %08x to=%x\n",out_pkt[j],out_to[j]);
  }
  assert(found==(unsigned)(s->output[t]!=0));
 }
 const unsigned writes=store_writes,tries=store_tries;md_patch before=*md_state();
 assert(bank_action(BANK_LOAD)==4&&!memcmp(md_state(),&before,sizeof before));
 assert(store_writes==writes&&store_tries==tries);
 midi(port,0xfc,0,0);assert(!md_playing());same_session(s);
}
static void recall_matrix(void){
 for(int kind=0;kind<2;kind++)for(int clock=1;clock<=3;clock++)for(int alternate=0;alternate<2;alternate++){
  command(MD_CLOCK_SOURCE,0,0);md_patch p=live_patch(121);assert(md_load(&p));set_session(clock,alternate);
  /* Legacy performance reset is intentionally distinct from the preserved
   * session settings. No assumption about RPN range/selection retention. */
  midi(PORT_TRS,0xb0u|(inputs[0]-1u),101,0);midi(PORT_TRS,0xb0u|(inputs[0]-1u),100,0);midi(PORT_TRS,0xb0u|(inputs[0]-1u),6,12);
  midi(PORT_TRS,0xe0u|(inputs[0]-1u),127,127);midi(PORT_TRS,0xb0u|(inputs[0]-1u),11,33);midi(PORT_TRS,0xb0u|(inputs[0]-1u),64,127);midi(PORT_TRS,0x90u|(inputs[0]-1u),61,100);
  assert(md_midi_held_port(0)==PORT_TRS&&md_midi_bend(0)>11.9f);md_mark_saved();
  MidiSession session;capture(&session);bank_kind=kind;bank_slot=kind?35:12;dream_ui.track=4;dream_ui.page=BANKS;bank_armed=-1;
  const unsigned writes=store_writes,tries=store_tries;md_patch before=*md_state();
  assert(!bank_action(BANK_LOAD));assert(!md_playing());same_session(&session);
  assert(store_writes==writes&&store_tries==tries);
  if(!kind){assert(!memcmp(md_state(),&saved_biome.patch,sizeof saved_biome.patch)&&!md_unsaved());}
  else{
   const md_track *now=&md_state()->track[4];assert(now->model==saved_tone.track.model&&now->tone==saved_tone.track.tone&&now->harm==saved_tone.track.harm);
   assert(now->note==before.track[4].note&&now->level==before.track[4].level&&now->speed==before.track[4].speed&&now->pan==before.track[4].pan&&now->send==before.track[4].send);
   assert(md_state()->ext[4].delay_send==before.ext[4].delay_send&&md_state()->ext[4].reverb_send==before.ext[4].reverb_send&&md_unsaved());
  }
  int32_t perf[7];md_midi_performance_state(0,perf);assert(perf[0]==-1&&perf[1]==0&&perf[2]==1000&&perf[3]==0&&perf[4]==-1&&perf[5]==-1&&perf[6]==-1);
  printf("RECALL kind=%d clock=%d macro=%d quantize=%d mapping_generation=%u legacy_bend_range=%.2f\n",kind,clock,session.macro,session.quantize,session.generation,(double)md_midi_bend_range(0));
  incoming_routes(&session);stopped_busy_writes(&session);generated_output_and_playing_lock(&session);recalls++;
 }
 puts("PASS 12 stopped User Biome/tone recalls under USB/TRS/BLE clock: full MIDI session and generation, 216 real routed NoteOns, six output routes, no flash attempts; playing/writes/editor/restore remain guarded");
}
static void navigation_chord(int octave,int sel_first,int release_sel_first){
 held_btn=1u<<(sel_first?B_SEL:octave);run_ms(15);held_btn=(1u<<B_SEL)|(1u<<octave);run_ms(25);
 held_btn=1u<<(release_sel_first?octave:B_SEL);run_ms(15);held_btn=0;run_ms(25);
}
static void unsaved_guards(void){
 command(MD_CLOCK_SOURCE,0,0);md_patch p=live_patch(117);assert(md_load(&p));dream_ui.page=BANKS;dream_ui.help=-1;dream_dismiss_intro();bank_kind=0;bank_slot=12;
 command(MD_TEMPO,0,133);assert(md_unsaved());const unsigned writes=store_writes,tries=store_tries;
 for(int which=0;which<2;which++)for(int first=0;first<2;first++)for(int release=0;release<2;release++){
  assert(bank_action(BANK_LOAD)==8&&bank_pending==12);const int page=dream_ui.page,octave=dream_ui.octave;md_patch before=*md_state();
  navigation_chord(which?B_OCTUP:B_OCTDN,first,release);
  assert(bank_pending==12&&dream_ui.page==page&&dream_ui.octave==octave&&!memcmp(md_state(),&before,sizeof before));
  tap(B_HOME);assert(bank_pending==-1&&md_unsaved()&&md_state()->tempo==133);dream_ui.page=BANKS;
 }
 assert(bank_action(BANK_LOAD)==8);tap(B_SEL);assert(bank_pending==-1&&!md_unsaved()&&md_state()->tempo==saved_biome.patch.tempo);
 assert(store_writes==writes&&store_tries==tries);
 command(MD_TEMPO,0,136);assert(bank_action(BANK_LOAD)==8);tap(B_SAVE);
 assert(bank_pending==-1&&bank_armed>=0&&md_unsaved()&&md_state()->tempo==136&&store_writes==writes);
 tap(B_HOME);assert(bank_armed==-1&&md_unsaved());dream_ui.page=BANKS;assert(bank_action(BANK_LOAD)==8);tap(B_SAVE);tap(B_SEL);
 assert(bank_pending==-1&&!md_unsaved()&&store_writes==writes+1);UserBiome kept;assert(plat_store_load(OBJ_USER+12,&kept,sizeof kept)==sizeof kept&&kept.patch.tempo==136);
 /* External SAVE cancels the load question but cannot write or confirm it. */
 for(int clock=1;clock<=3;clock++){
  command(MD_CLOCK_SOURCE,0,clock);command(MD_TEMPO,0,140+clock);assert(bank_action(BANK_LOAD)==8);unsigned w=store_writes,t=store_tries;tap(B_SAVE);
  assert(bank_pending==-1&&md_unsaved()&&md_state()->tempo==140+clock&&store_writes==w&&store_tries==t&&bank_armed==-1);
 }
 command(MD_CLOCK_SOURCE,0,0);command(MD_SCENE,0,2);assert(md_pending_biome()==2);const int octave=dream_ui.octave;
 navigation_chord(B_OCTUP,1,1);assert(md_pending_biome()==2&&dream_ui.octave==octave);tap(B_HOME);assert(md_pending_biome()==-1);
 puts("PASS unsaved User Biome confirm/HOME cancel/SAVE and overwrite cancel/confirm; SEL+OCT in both press/release orders never confirms, changes page or shifts keys; external SAVE stays read-only");
}
int main(void){
 boot();run_ms(100);dream_dismiss_intro();fixtures();recall_matrix();unsaved_guards();
 assert(recalls==12&&note_checks==216&&!md_status()->nonfinite&&!md_status()->dropped_commands);
 puts("PASS bank_feedback24_test: real app bank_action and serialized host MIDI/IRQ ownership; legacy RPN/performance observations are not a session-preservation promise");return 0;
}
