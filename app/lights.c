/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Main-loop light policy. Status lights stay bright; animation only changes
 * the low-power dim layer. No allocation, timer hook or audio-side work. */
static const uint8_t dream_page_button[]={
 B_HOME,B_EDIT,B_SEQ,B_FX,B_EDIT,B_GLO,B_EDIT,B_SEQ,B_FX,B_GLO,
 B_EDIT,B_GLO,B_EDIT,B_SEQ,B_FX,B_FX,B_SEQ,B_GLO,B_GLO,B_SEQ,
 B_SEQ,B_GLO,B_FX,B_GLO,B_EDIT,B_EDIT,B_SEQ,B_SEQ,B_FX,B_FX,
 B_GLO,B_SEQ,B_SEQ,B_SEQ,B_GLO,B_GLO,B_FX,B_FX,B_FX,B_FX,B_SEQ,B_SEQ,B_SEQ,B_GLO
};
_Static_assert(sizeof dream_page_button==sizeof dream_pages/sizeof *dream_pages,"Every page has one navigation light");
static uint32_t dream_light_at, dream_light_ns=1600;

static uint32_t dream_light_buttons(void){
 const md_patch *p=md_state();uint32_t b=1u<<dream_page_button[dream_ui.page];
 /* Holding PLAY highlights its green LED, never a false red recording light. */
 b|=dream_ui.btn&~(1u<<B_PLAY);
 if(dream_ui.help>=0)b|=1u<<dream_ui.help;
 if(dream_ui.page==SEQ_RULES?md_fill_status()!=0:
    p->track[dream_ui.track].mute||dream_ui.page==SEQ_GRID||dream_ui.page==SEQ_TOOLS)b|=1u<<B_SEL;
 if(p->locked)b|=1u<<B_ARP;
 if(p->freeze)b|=1u<<B_LFO;
 if(dream_ui.octave<0)b|=1u<<B_OCTDN;
 if(dream_ui.octave>0)b|=1u<<B_OCTUP;
 if(md_record_mode())b|=1u<<B_REC;
 if(md_playing()||(dream_ui.btn&(1u<<B_PLAY))||dream_ui.help==B_PLAY){
  b&=~(1u<<B_PLAY);b|=md_playing()&&md_record_mode()?(1u<<B_PLAY):LED_PLAY_GREEN;
 }
 if(md_pending_biome()>=0||bank_pending>=0)b|=(1u<<B_SEL)|(1u<<B_HOME)|(1u<<B_SAVE);
 return b;
}

static uint32_t dream_light_tail(unsigned track,uint32_t now){
 if(track>=(unsigned)md_active_tracks()||md_state()->track[track].mute||!last_events[track])return 0;
 uint32_t age=now-pulse_start[track];
 if(age>=1800u)return 0;
 uint32_t left=(1800u-age)*1024u/1800u;
 return left*left/1024u; /* A rounded musical decay, including while stopped. */
}

static uint32_t dream_light_target(uint32_t now){
 if(dream_prefs.button_glow!=2||!dream_prefs.motion)return 0;
 uint32_t energy=0;
 for(unsigned i=0;i<6;i++){uint32_t e=dream_light_tail(i,now);if(e>energy)energy=e;}
 /* At least four seconds per breath. Faster tempi use whole-bar multiples. */
 uint32_t period=240000u/(unsigned)clamp(md_state()->tempo,20,300);
 while(period<4000u)period*=2u;
 uint32_t x=(now%period)*1024u/period;x=x<512u?2u*x:2u*(1024u-x);
 uint32_t smooth=((x*x/1024u)*(3072u-2u*x))/1024u;
 uint32_t activity=md_playing()?1024u:energy;
 return 1600u+((smooth*1200u+energy*600u)/1024u)*activity/1024u;
}

static uint32_t dream_light_note(uint32_t now){
 unsigned tr=(unsigned)dream_ui.track;
 if(dream_prefs.button_glow!=2||!dream_prefs.motion||(md_state()->kit.enabled&&tr==3)||!dream_light_tail(tr,now))return 0;
 int key=md_status()->last_note[tr]-(53+dream_ui.octave*12);
 return key>=0&&key<27?1u<<key:0;
}

static void dream_lights(uint32_t now){
 if((uint32_t)(now-dream_light_at)<20u)return;
 dream_light_at=now;
 uint32_t target=dream_light_target(now),b=dream_light_buttons();
 if(target){
  int d=(int)target-(int)dream_light_ns;
  dream_light_ns=(uint32_t)((int)dream_light_ns+(d?d/8+(d>0?1:-1):0));
 }else dream_light_ns=1600;
 /* Green is a separate physical PLAY LED; never leave its red LED glowing
  * behind a green transport light. Unused labels keep the chosen dim floor. */
 uint32_t dim=(((1u<<NB)-1u)&~(1u<<B_PLAY)&~b)|LED_PLAY_GREEN;
 plat_lights(b,dream_ui.keys,dim,dream_light_note(now),dream_prefs.button_glow);
 plat_dim_ns(target?dream_light_ns:0);
}
