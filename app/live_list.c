/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * 2.2 development (#4, #8, #18): a numbered Live List from HOME and a starred tone list.
 * Row 00 loads the Blank Biome. Rows 01-16 each hold a factory Biome or one of the 16
 * User Biome slots. Every Biome load goes through the existing unsaved-edits guard
 * (Biome browser: md_pending_biome; User Banks: bank_pending). The list and the stars are
 * preferences: SAVE keeps them, nothing else is written. */
enum { LIST_BIOMES, LIST_TONES, LIST_STARRED, LIST_MODES };
enum { LIST_ROWS=LIVE_SLOTS+1, LIST_CHOICES=1+16+MD_BIOMES };
static int list_mode,list_row,tone_row,list_dirty;
static int live_user_slot=-1,live_user_biome;static uint32_t live_user_seed;
static uint32_t list_serial;
static char list_names[16][24];   /* User Biome names, read from each slot's header */
static uint8_t list_names_valid;

static void list_invalidate_names(void){list_names_valid=0;}
static void live_list_reset(void){
 list_mode=LIST_BIOMES;list_row=1;tone_row=0;list_dirty=0;live_user_slot=-1;list_names_valid=0;list_serial++;
}
static int list_factory(unsigned e){return e>=1&&e<=MD_BIOMES?(int)e-1:-1;}
static int list_user(unsigned e){return (e&LIVE_USER)&&(e&0x7fffu)<16?(int)(e&0x7fffu):-1;}
static int tone_starred(int id){return id>=0&&id<MD_TONE_PRESETS&&(live_prefs.tone_stars[id/8]>>(id%8)&1);}
static void list_changed(void){list_dirty=1;list_serial++;dream_ui.dirty=1;}

/* Main loop only, never while drawing: one 28-byte header for each listed User Biome. */
static void list_refresh_names(void){
 for(int slot=0;slot<16;slot++){
  struct { uint32_t magic; char name[24]; } head;
  char *out=list_names[slot];
  if(plat_store_head(OBJ_USER+slot,&head,sizeof head)==(int)sizeof head&&head.magic==0x4d445542){
   int i=0;for(;i<23&&head.name[i];i++)out[i]=head.name[i]>=32&&head.name[i]<=126?head.name[i]:' ';
   out[i]=0;if(!i)memcpy(out,"USER BIOME",11);
  }else memcpy(out,"EMPTY SLOT",11);
 }
 list_names_valid=1;list_serial++;
}

/* A User Biome load remembers its slot while the project still is that slot's Biome. */
static void list_note_user_load(int slot){
 const md_patch *p=md_project_state();
 live_user_slot=slot;live_user_seed=p->seed;live_user_biome=p->biome_id;
}
static int list_live_user_slot(void){
 const md_patch *p=md_project_state();
 return live_user_slot>=0&&p->seed==live_user_seed&&p->biome_id==live_user_biome?live_user_slot:-1;
}

static void list_entry_name(unsigned e,char out[28]){
 const int f=list_factory(e),u=list_user(e);
 const char *name=f>=0?md_biome_name(f):u>=0?list_names[u]:"-";
 int i=0;
 if(u>=0){out[i++]='U';md_num(u+1,out+i);while(out[i])i++;out[i++]=' ';}
 while(*name&&i<27)out[i++]=*name++;
 out[i]=0;
}

static int list_choice_of(unsigned e){
 const int f=list_factory(e),u=list_user(e);
 if(u>=0)return 1+u;
 if(f>=0)for(unsigned pos=0;pos<MD_BIOMES;pos++)if(md_biome_at(pos)==f)return 17+(int)pos;
 return 0;
}
static unsigned list_choice(int index){
 if(index<=0)return 0;
 if(index<=16)return LIVE_USER|(unsigned)(index-1);
 return (unsigned)md_biome_at((unsigned)(index-17))+1u;
}

static int list_next_star(int from,int direction){
 for(int id=from;id>=0&&id<MD_TONE_PRESETS;id+=direction)if(tone_starred(id))return id;
 return -1;
}
static void list_say_tone(void){
 if(list_mode==LIST_STARRED&&!tone_starred(tone_row)){app_say("NO STARRED TONES","");return;}
 app_say(md_tone_name(tone_row),"");
}
static void list_open(void){
 list_names_valid=0;
 app_say(list_mode==LIST_BIOMES?"LIVE LIST":list_mode==LIST_TONES?"TONE LIST":"STARRED TONES","");
}
static void list_change_mode(int delta){
 const int mode=clamp(list_mode+delta,0,LIST_MODES-1);
 if(mode==list_mode)return;
 if(list_mode==LIST_BIOMES){
  const md_patch *p=md_state();const int id=p->tone_preset[dream_ui.track];
  tone_row=id>=0?id:md_model_first_tone(p->track[dream_ui.track].model);
 }
 list_mode=mode;
 if(list_mode==LIST_STARRED&&!tone_starred(tone_row)){
  int id=list_next_star(tone_row,1);if(id<0)id=list_next_star(tone_row,-1);if(id>=0)tone_row=id;
 }
 list_serial++;list_open();
}
static void list_move(int delta){
 if(!delta)return;
 if(list_mode==LIST_BIOMES){
  list_row=clamp(list_row+delta,0,LIST_ROWS-1);
  char name[28];
  if(!list_row)app_say("00 BLANK BIOME","");
  else if(!live_prefs.entry[list_row-1])app_say("EMPTY ROW","");
  else {list_entry_name(live_prefs.entry[list_row-1],name);app_say(name,"");}
 }else if(list_mode==LIST_TONES){
  tone_row=clamp(tone_row+delta,0,MD_TONE_PRESETS-1);list_say_tone();
 }else{
  const int direction=delta>0?1:-1;
  for(int n=delta*direction;n>0;n--){const int id=list_next_star(tone_row+direction,direction);if(id<0)break;tone_row=id;}
  list_say_tone();
 }
 list_serial++;
}
/* K3: choose a row's content: empty, User Biome 1-16, then every factory Biome in wheel order. */
static void list_assign(int delta){
 if(list_mode!=LIST_BIOMES||!delta)return;
 if(!list_row){app_say("ROW 00 IS ALWAYS BLANK","");return;}
 uint16_t *e=&live_prefs.entry[list_row-1];
 *e=(uint16_t)list_choice(clamp(list_choice_of(*e)+delta,0,LIST_CHOICES-1));
 char name[28];
 if(*e){list_entry_name(*e,name);app_say(name,"");}else app_say("EMPTY ROW","");
 list_changed();
}
/* REC: take the live Biome into an empty row, or empty a filled row; in the tone list, star. */
static void list_record(void){
 if(list_mode!=LIST_BIOMES){
  if(list_mode==LIST_STARRED&&!tone_starred(tone_row)){app_say("NO STARRED TONES","");return;}
  live_prefs.tone_stars[tone_row/8]^=(uint8_t)(1u<<(tone_row%8));
  app_say(tone_starred(tone_row)?"TONE STARRED":"STAR REMOVED","");
  if(list_mode==LIST_STARRED&&!tone_starred(tone_row)){
   int id=list_next_star(tone_row,1);if(id<0)id=list_next_star(tone_row,-1);if(id>=0)tone_row=id;
  }
  list_changed();return;
 }
 if(!list_row){app_say("ROW 00 IS ALWAYS BLANK","");return;}
 uint16_t *e=&live_prefs.entry[list_row-1];
 if(*e){*e=0;app_say("ROW CLEARED","");list_changed();return;}
 const int user=list_live_user_slot(),biome=md_project_state()->biome_id;
 if(user>=0)*e=(uint16_t)(LIVE_USER|(unsigned)user);
 else if(biome>=0&&biome<MD_BIOMES)*e=(uint16_t)(biome+1);
 else {app_say("SAVE IT IN USER BANKS FIRST","");return;}
 char name[28];list_entry_name(*e,name);app_say(name,"");
 list_changed();
}
/* SEL: load the row (behind the unsaved-edits guard) or apply the tone to the selected track. */
static void list_select(void){
 const md_patch *p=md_state();
 if(list_mode!=LIST_BIOMES){
  if(list_mode==LIST_STARRED&&!tone_starred(tone_row)){app_say("NO STARRED TONES","");return;}
  if(p->kit.enabled&&dream_ui.track==3){app_say("SELECT A SYNTH TRACK","");return;}
  if(dream_cmd(MD_TONE_PRESET,dream_ui.track,tone_row))app_say(md_tone_name(tone_row),"");
  return;
 }
 if(!list_row){if(dream_cmd(MD_SCENE,0,MD_BLANK_BIOME))app_say("BLANK BIOME","");return;}
 const unsigned e=live_prefs.entry[list_row-1];
 const int f=list_factory(e),u=list_user(e);
 if(u>=0){
  bank_kind=0;bank_slot=u;bank_armed=-1;
  bank_action(BANK_LOAD);   /* unsaved edits: bank_pending opens the guard; SEL there loads */
  return;
 }
 if(f<0){app_say("EMPTY ROW: K3 OR REC","");return;}
 uint32_t lock=plat_lock();const int unsaved=md_unsaved();plat_unlock(lock);
 if(f==md_project_state()->biome_id&&list_live_user_slot()<0&&!unsaved){app_say("ALREADY LOADED","");return;}
 if(dream_cmd(MD_SCENE,0,f))app_say(md_biome_name(f),"");
}

static void draw_live_list(uint16_t color){
 static const char *const titles[]={"LIVE LIST","TONE LIST","STARRED"};
 const md_patch *p=md_state();
 char s[28];
 cv_text(11,7,&FONT_M,titles[list_mode],MD_INK);
 cv_text(150,8,&FONT_T,"K1 LIST / TONES",MD_DIM);
 cv_text(150,21,&FONT_T,list_dirty?"SAVE KEEPS IT":"SAVED",list_dirty?color:MD_DIM);
 cv_rect(10,39,220,1,color);
 if(list_mode==LIST_BIOMES){
  const int first=clamp(list_row-2,0,LIST_ROWS-6),live=list_live_user_slot();
  for(int r=first;r<first+6;r++){
   const int y=45+(r-first)*26;
   const unsigned e=r?live_prefs.entry[r-1]:(unsigned)MD_BLANK_BIOME+1u;
   const int f=list_factory(e),u=list_user(e);
   const int loaded=u>=0?u==live:f>=0&&f==p->biome_id&&live<0;
   if(r==list_row){cv_rect(8,y,224,24,MD_CARD);cv_rect(8,y,3,24,color);}
   s[0]='0'+r/10;s[1]='0'+r%10;s[2]=0;
   cv_text(16,y+3,&FONT_B,s,r==list_row?color:MD_DIM);
   if(!r)memcpy(s,"BLANK BIOME",12);else if(e)list_entry_name(e,s);else memcpy(s,"- empty -",10);
   cv_text(46,y+3,text_w(&FONT_B,s)>(loaded?132:178)?&FONT_H:&FONT_B,s,e?MD_INK:MD_EDGE);
   if(loaded)cv_text(186,y+7,&FONT_T,"LIVE",color);
  }
  cv_text(11,203,&FONT_XS,"K2 Move  K3 Choose  SEL Load",MD_INK);
  cv_text(11,220,&FONT_XS,"REC Take live / Clear  SAVE Keep",MD_DIM);
 }else{
  int rows[6],n=0,at=0;
  if(list_mode==LIST_TONES){
   const int first=clamp(tone_row-2,0,MD_TONE_PRESETS-6);
   for(int i=0;i<6;i++)rows[n++]=first+i;
   at=tone_row-first;
  }else if(tone_starred(tone_row)){
   int before[2],nb=0;
   for(int id=list_next_star(tone_row-1,-1);id>=0&&nb<2;id=list_next_star(id-1,-1))before[nb++]=id;
   while(nb)rows[n++]=before[--nb];
   at=n;
   for(int id=tone_row;id>=0&&n<6;id=list_next_star(id+1,1))rows[n++]=id;
  }
  if(!n){
   cv_text(11,92,&FONT_B,"No starred tones yet",MD_INK);
   cv_text(11,118,&FONT_H,"K1 left: all tones, REC: star",MD_DIM);
  }
  for(int i=0;i<n;i++){
   const int y=45+i*26,id=rows[i];
   if(i==at){cv_rect(8,y,224,24,MD_CARD);cv_rect(8,y,3,24,color);}
   cv_text(13,y+3,&FONT_B,tone_starred(id)?"*":"",color);
   const char *name=md_tone_name(id);
   cv_text(28,y+3,text_w(&FONT_B,name)>150?&FONT_H:&FONT_B,name,MD_INK);
   md_num(id+1,s);cv_text(196,y+7,&FONT_T,s,id==p->tone_preset[dream_ui.track]?color:MD_DIM);
  }
  s[0]='T';s[1]='1'+dream_ui.track;s[2]=0;
  cv_text(11,203,&FONT_XS,"K2 Move  SEL Apply to",MD_INK);cv_text(150,203,&FONT_XS,s,color);
  cv_text(11,220,&FONT_XS,"REC Star  SAVE Keep stars",MD_DIM);
 }
 if(dream_ui.msg[0]){
  cv_rect(0,216,240,24,MD_CARD);
  cv_text(11,220,&FONT_XS,dream_ui.msg,color);
 }
}
