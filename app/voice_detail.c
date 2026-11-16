/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * 2.2 development (#13): more room on the FM-1's 240 x 240 display.
 * VOICE_DETAIL: the selected track's voice in large figures. EDIT from Large Tracks opens it,
 * EDIT again continues to SOUND, HOME returns to Large Tracks. K1-K4 edit as on SOUND.
 * LIVE_STAGE: the live Biome as a full-screen scene. It reuses the Itinera scene renderer
 * (same family layers, track levels and grains, same frame pacing by audio load), so it adds
 * no new drawing cost class. The name fades in only after a change. */
static uint32_t stage_activity;
static int stage_biome=-2;

static void voice_cell(int x,int y,const char *label,const char *value,uint16_t accent){
 reader_card(label,value);   /* 2.2.1: K1-K4 for the screen reader */
 cv_text(x,y,&FONT_XS,label,MD_DIM);
 cv_text(x,y+14,text_w(&FONT_M,value)<=108?&FONT_M:&FONT_B,value,accent);
}
static void voice_number(int x,int y,const char *label,int value,uint16_t accent){
 char s[12];md_num(value,s);voice_cell(x,y,label,s,accent);
}
static void draw_voice_detail(uint16_t color){
 const md_patch *p=md_state();
 const int tr=dream_ui.track;const md_track *t=&p->track[tr];
 char s[28];
 md_num(tr+1,s);
 cv_rect(0,0,240,240,MD_BG);
 cv_text(11,6,&FONT_L,s,dream_colors[tr]);
 const int kit=p->kit.enabled&&tr==3;
 const char *engine=kit?"LO-FI KIT":md_model_name(t->model);
 cv_text(40,7,text_w(&FONT_B,engine)<=190?&FONT_B:&FONT_H,engine,MD_INK);
 const char *tone=kit?md_kit_name(p->kit.bank):md_tone_name(p->tone_preset[tr]);
 cv_text(40,27,text_w(&FONT_H,tone)<=190?&FONT_H:&FONT_XS,tone,color);
 cv_rect(10,47,220,1,color);
 if(t->mute){cv_text(186,7,&FONT_XS,"MUTED",RGB(231,174,154));}
 if(kit){
  voice_number(12,56,"PAD",p->kit.pad+1,MD_INK);
  voice_cell(128,56,"NAME",md_pad_name(p->kit.pad),MD_INK);
  cv_text(12,120,&FONT_H,"EDIT: Pad / Sends / Mixer",MD_DIM);
 }else{
  const int envelope=md_has_envelope(t->model)||t->model==MD_ELEMENTS;
  /* The first two rows follow K1-K4 exactly as on SOUND. */
  voice_number(12,52,"K1 TONE",t->tone/10,dream_colors[0]);
  voice_number(128,52,"K2 HARMONICS",t->harm/10,dream_colors[1]);
  voice_number(12,92,envelope?"K3 SHAPE":"K3 DECAY",(envelope?t->shape:t->decay)/10,dream_colors[2]);
  voice_number(128,92,"K4 LEVEL",t->level/10,dream_colors[3]);
  md_num(md_cutoff_hz(t->cutoff),s);{char *e=s;while(*e)e++;memcpy(e," HZ",4);}
  voice_cell(12,132,"CUTOFF",s,MD_INK);
  voice_number(128,132,"RESONANCE",t->resonance/10,MD_INK);
  voice_number(12,172,"ATTACK",t->attack/10,MD_INK);
  voice_number(128,172,envelope?"DECAY":"SEND",(envelope?t->decay:t->send)/10,MD_INK);
 }
 if(dream_ui.msg[0]){cv_rect(0,216,240,24,MD_CARD);cv_text(11,220,&FONT_XS,dream_ui.msg,color);}
 else cv_text(11,222,&FONT_XS,"PRESET Tone  EDIT Sound  HOME Tracks",MD_DIM);
}

static void stage_view(md_itinera_view *view){
 static const uint16_t minutes[4]={30,60,120,240};
 const md_patch *p=md_state();
 const unsigned total=minutes[radio_settings()%4]*60u;
 /* The middle of a dream: the clearest light; a slow breath from the motion clock. */
 const unsigned breath=(unsigned)((flower_time/48u)%1000u);
 memset(view,0,sizeof *view);
 view->active=1;view->biome=p->biome_id<0?0:p->biome_id;view->section=(unsigned)(p->seed%8u);
 view->remaining=total/2u;view->chapter=2;view->progress=breath;
 view->energy=(unsigned)clamp(p->macro[0]/10,0,100);
}
static void draw_stage(void){
 const md_patch *p=md_state();
 const uint16_t *pal=md_palette();
 if(stage_biome!=p->biome_id){stage_biome=p->biome_id;stage_activity=plat_ms();}
 md_itinera_view saved=radio_view;
 stage_view(&radio_view);
 radio_art(0,240,pal[0],pal[2]);
 radio_view=saved;
 const uint32_t age=plat_ms()-stage_activity;
 if(age<8000u||!dream_prefs.motion){
  const unsigned mix=dream_prefs.motion?radio_text_mix_from(age):32;
  if(mix){
   radio_center(13,&FONT_B,md_biome_name(p->biome_id),md_tint(MD_BG,MD_INK,mix));
   radio_center(215,&FONT_XS,md_biome_family(p->biome_id),md_tint(MD_BG,pal[1],mix));
  }
 }
 if(dream_ui.msg[0])radio_center(215,&FONT_XS,dream_ui.msg,MD_INK);
}
