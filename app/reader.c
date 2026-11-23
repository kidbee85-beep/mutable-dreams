/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Screen reader (GitHub #10): what the firmware tells fm1-reader besides its messages. Included by app.c.
 *
 * 2.2 spoke only app_say messages (PRESET's Biome name, HOME's view, SAVE) and the held buttons' help titles.
 * Now, while a reader listens (ann_on), after the frame that shows the result:
 *   a turned K1-K4      its card as drawn ("K1 DENSITY 51"), the newest of a turn once it settles
 *   SELECT / ALGO       the selected track and its voice ("TRACK 2 PLAITS VA")
 *   a new page or view  its title, the track (on track pages) and its four cards; also the reader's WHERE
 *                       and a new reader's HELLO (fm1-reader asks WHERE when it starts)
 *   a short button      when the app said nothing itself: what changed (play, mute, seed lock, freeze, a card),
 *                       OCT-/OCT+ the keys' octave
 *   PRESET              on MIDI the track's notes out; elsewhere the cards it changed, when the app names nothing
 * Nothing is collected or sent while no reader listens: no extra work in the frame, audio or mirror then.
 * The cards are taken where the screen draws them (md_named, md_value_span: labels "K1".."K4"), so every
 * page is covered without a list of its own. No settings, banks or patch data change. */
static char reader_cards[4][44],reader_prev[4][44];
static uint32_t reader_turned;                 /* EN_* bits turned since the last frame */
static uint32_t reader_state;                  /* play, seed lock and the mutes before the frame's input */
static int reader_tap=-1,reader_key=-1,reader_spoke;
static const char *const *reader_help_seen;
static uint32_t reader_tap_at;              /* a button's effect can follow a few frames later (the engine's queue) */
static int reader_fresh;                    /* the cards come from a draw made while this reader listened */

/* each detent read anywhere in the app (app.c maps plat_enc here) */
static int32_t reader_enc(int role){
 const int32_t d=(plat_enc)(role);
 if(d&&role>=0&&role<32)reader_turned|=1u<<role;
 return d;
}
/* a K1..K4 card as the screen draws it: its label, then its value */
static void reader_card(const char *label,const char *value){
 if(!ann_on()||!label||label[0]!='K'||label[1]<'1'||label[1]>'4')return;
 char *s=reader_cards[label[1]-'1'];unsigned n=0;
 while(*label&&n<sizeof reader_cards[0]-14)s[n++]=*label++;
 s[n++]=' ';
 while(value&&*value&&n<sizeof reader_cards[0]-1)s[n++]=*value++;
 s[n]=0;
}
/* the screen is drawn anew: the cards it draws replace the last ones (a page without cards has none) */
static void reader_cards_clear(void){reader_fresh=ann_on();if(reader_fresh)memset(reader_cards,0,sizeof reader_cards);}
/* draw_display may not keep a cached still screen (HOME's views): its cards were never taken, or are from an
 * earlier reader, or the reader asks for the whole screen (HELLO, WHERE). Only while a reader listens. */
static int reader_stale(void){return ann_on()&&(!reader_fresh||ann.where);}
/* (the firmware links no string library: the two small helpers the reader needs) */
static int reader_same(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static void reader_copy(char *d,const char *s){while((*d++=*s++)!=0){}}
/* s += ". " + t (no separator before the first part) */
static unsigned reader_add(char *s,unsigned n,const char *t){
 if(!t||!*t)return n;
 if(n&&n+2<ANN_MAX){s[n++]='.';s[n++]=' ';}
 while(*t&&n<ANN_MAX)s[n++]=*t++;
 s[n]=0;return n;
}
static const char *reader_title(void){
 static const char *const views[LIVE_VIEW_COUNT]={"LIVE ATLAS","LARGE TRACKS","BIOMES","LIVE LIST","BIOME STAGE"};
 return dream_ui.page==LIVE?views[dream_ui.live_view%LIVE_VIEW_COUNT]:dream_pages[dream_ui.page];
}
/* "TRACK 2 PLAITS VA", "TRACK 4 LO-FI KIT OFF" */
static void reader_track(char *s){
 const md_patch *p=md_state();const int t=dream_ui.track;unsigned n=6;
 memcpy(s,"TRACK ",6);md_num(t+1,s+n);while(s[n])n++;s[n++]=' ';
 for(const char *v=p->kit.enabled&&t>=3?"LO-FI KIT":md_model_name(p->track[t].model);*v&&n<36;)s[n++]=*v++;
 if(p->track[t].mute){memcpy(s+n," OFF",4);n+=4;}
 s[n]=0;
}
static uint32_t reader_now_state(void){
 const md_patch *p=md_state();uint32_t m=0;
 for(int i=0;i<6;i++)if(p->track[i].mute)m|=1u<<i;
 return m|(uint32_t)!!md_playing()<<6|(uint32_t)!!p->locked<<7|(uint32_t)!!p->freeze<<8;
}
/* "OCTAVE -1", "OCTAVE 0", "OCTAVE +2" (the keys' octave: lit OCT buttons only, no screen of its own) */
static void reader_octave(char *s){
 const int o=dream_ui.octave;reader_copy(s,"OCTAVE ");
 if(o)s[7]=o<0?'-':'+';md_num(o<0?-o:o,s+(o?8:7));
}
/* PRESET on MIDI: "TRACK 2 NOTES OUT CH 3" / "TRACK 2 NOTES OUT OFF" */
static void reader_notes_out(char *s){
 const int ch=md_midi_out_channel(dream_ui.track);unsigned n=6;
 memcpy(s,"TRACK ",6);md_num(dream_ui.track+1,s+n);while(s[n])n++;
 reader_copy(s+n,ch?" NOTES OUT CH ":" NOTES OUT OFF");
 if(ch){while(s[n])n++;md_num(ch,s+n);}
}
/* a short button press (md_tap; also from app_input_only between frames) */
static void reader_pressed(int b){reader_tap=b;reader_tap_at=plat_ms();}
/* after draw_display: say what the frame shows */
static void reader_after(void){
 if(!ann_on()){reader_key=-1;reader_turned=0;reader_fresh=0;return;}
 char s[ANN_MAX+1],t[48];unsigned n=0;s[0]=0;
 const int key=dream_ui.help>=0?-2-(dream_ui.help*DREAM_PAGE_COUNT+dream_ui.page)*LIVE_VIEW_COUNT-dream_ui.live_view:dream_ui.page*8+(dream_ui.page==LIVE?dream_ui.live_view:0);
 const int where=ann_where();
 if(dream_ui.help<0&&(where||key!=reader_key)){
  if(where||!reader_spoke)n=reader_add(s,n,reader_title());   /* (HOME names its views itself) */
  if(dream_ui.page!=LIVE&&dream_ui.page!=ITINERA){reader_track(t);n=reader_add(s,n,t);}
  for(int k=0;k<4;k++)n=reader_add(s,n,reader_cards[k]);
  if(where&&!reader_same(dream_ui.msg,reader_title()))n=reader_add(s,n,dream_ui.msg);
  if(n)ann_say(s,0,reader_spoke?0:ANN_NOW);
 }else if(dream_ui.help<0){
  for(int k=0;k<4;k++)
   if((reader_turned&(1u<<(EN_K1+k)))&&reader_cards[k][0])ann_later(reader_cards[k],ANN_NOW,250,plat_ms());
  if(reader_turned&((1u<<EN_SELECT)|(1u<<EN_ALGO))){reader_track(t);ann_later(t,ANN_NOW,250,plat_ms());}
  /* PRESET where it names nothing itself (Biomes and tones say their names): notes out on MIDI, else the
   * cards it changed (a learn or link slot, a sequence panel, a pad) */
  if((reader_turned&(1u<<EN_PRESET))&&!reader_spoke){
   if(dream_ui.page==MIDI){reader_notes_out(t);ann_later(t,ANN_NOW,250,plat_ms());}
   else{
    for(int k=0;k<4;k++)if(!reader_same(reader_cards[k],reader_prev[k]))n=reader_add(s,n,reader_cards[k]);
    if(n)ann_later(s,ANN_NOW,250,plat_ms());
    n=0;s[0]=0;
   }
  }
  if(reader_tap>=0&&!reader_spoke){
   const uint32_t now=reader_now_state(),was=reader_state;
   const int late=(uint32_t)(plat_ms()-reader_tap_at)>=300u;
   if((now^was)&(1u<<6))n=reader_add(s,n,now&(1u<<6)?"PLAYING":"STOPPED");
   if((now^was)&(1u<<7))n=reader_add(s,n,now&(1u<<7)?"SEED LOCKED":"SEED FREE");
   if((now^was)&(1u<<8))n=reader_add(s,n,now&(1u<<8)?"FREEZE ON":"FREEZE OFF");
   if(reader_tap==B_OCTDN||reader_tap==B_OCTUP){reader_octave(t);n=reader_add(s,n,t);}  /* also at its end */
   for(int i=0;i<6;i++)if((now^was)&(1u<<i)){
    memcpy(t,"TRACK ",6);md_num(i+1,t+6);unsigned k=6;while(t[k])k++;
    reader_copy(t+k,now&(1u<<i)?" OFF":" ON");n=reader_add(s,n,t);
   }
   for(int k=0;k<4;k++)if(!reader_same(reader_cards[k],reader_prev[k]))n=reader_add(s,n,reader_cards[k]);
   /* REC and ENV's own work (mutate, return; PANIC on SYSTEM and MIDI) shows nowhere else */
   if(!n&&late&&(reader_tap==B_REC||reader_tap==B_ENV))
    n=reader_add(s,n,reader_tap==B_ENV?"RETURN":dream_ui.page==SYSTEM||dream_ui.page==MIDI?"PANIC":"MUTATE");
   if(n)ann_say(s,0,ANN_NOW);
   if(n||late)reader_tap=-1;              /* else wait for the engine to show it */
  }
 }else{                                  /* Say the same concrete guide that is drawn. */
  reader_tap=-1;
  if((where||key!=reader_key||reader_help_seen!=reader_help_lines)&&reader_help_lines){
   n=reader_add(s,n,reader_help_title);
   for(int k=0;k<5;k++)n=reader_add(s,n,reader_help_lines[k]);
   if(n)ann_say(s,0,ANN_NOW);
   reader_help_seen=reader_help_lines;
  }
 }
 if(reader_spoke||key!=reader_key)reader_tap=-1;
 reader_key=key;reader_turned=0;reader_spoke=0;
 if(reader_tap<0)reader_state=reader_now_state();
 memcpy(reader_prev,reader_cards,sizeof reader_prev);
}
