/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
static int browse_filter;
static int browse_position;

static int biome_favorite(int id){
 return id>=0&&id<MD_BIOMES&&(performance_prefs.favorites[id/8]&(1u<<(id%8)));
}

static int browse_matches(int id){
 if(id<0)return 0;
 if(!browse_filter)return 1;
 if(browse_filter==(int)md_biome_family_count()+1)return biome_favorite(id);
 return md_biome_family_index(id)==browse_filter-1;
}

static int browse_biome(void){
 return browse_position<0?-1:md_biome_at((unsigned)browse_position);
}

static void browse_open(int current){
 browse_position=-1;
 for(unsigned position=0;position<MD_BIOMES;position++){
  const int id=md_biome_at(position);
  if(!browse_matches(id))continue;
  if(browse_position<0||id==current)browse_position=(int)position;
  if(id==current)break;
 }
 const int id=browse_biome();
 app_say(id<0?"NO BIOMES IN FILTER":md_biome_name(id),"");
}

static void browse_change_filter(int delta){
 const int current=browse_biome();
 browse_filter=clamp(browse_filter+delta,0,(int)md_biome_family_count()+1);
 browse_open(current);
}

static void browse_move(int delta){
 if(!delta)return;
 const int direction=delta>0?1:-1;
 int position=browse_position;
 if(position<0)position=direction>0?-1:MD_BIOMES;
 for(position+=direction;position>=0&&position<MD_BIOMES;position+=direction){
  if(!browse_matches(md_biome_at((unsigned)position)))continue;
  browse_position=position;
  delta-=direction;
  if(!delta)break;
 }
 const int id=browse_biome();
 if(id>=0)app_say(md_biome_name(id),"");
}

static void browse_toggle_favorite(void){
 const int id=browse_biome();
 if(id<0)return;
 performance_prefs.favorites[id/8]^=1u<<(id%8);
 if(!browse_matches(id))browse_open(-1);
 app_say(biome_favorite(id)?"FAVORITE ADDED":"FAVORITE REMOVED","");
}

static void draw_biome_browser(uint16_t color){
 const int id=browse_biome();
 const int live_id=display_static_frame[0]?(int)(display_static_frame[2]&511u)-1:md_state()->biome_id;
 const char *filter=browse_filter==(int)md_biome_family_count()+1?"FAVORITES":
  md_biome_family_name((unsigned)(browse_filter-1));
 cv_text(11,7,&FONT_M,"BIOMES",MD_INK);
 cv_rect(10,39,220,1,color);
 cv_text(11,49,&FONT_XS,"K1 FILTER",MD_DIM);
 atlas_text(88,46,143,&FONT_H,filter,color);
 md_biome_card card;
 if(md_biome_card_info(id,&card)){
  const char *name=md_biome_name(id);
  static const char *const notes[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  const uint32_t seed=md_scene_dna(card.seed,id);
  const uint16_t tint=atlas_colour(card.seed,id);
  char number[12],tempo[12];
  md_num(id+1,number);md_num(card.tempo,tempo);
  atlas_plate(8,70,224,105,tint);
  atlas_plate(153,79,70,86,tint);
  atlas_mark(188,121,23,atlas_kind(&card),seed,0,tint);
  cv_text(16,78,&FONT_XS,number,tint);
  cv_text(55,78,&FONT_XS,id==live_id?"LOADED":biome_favorite(id)?"FAVORITE":"",MD_DIM);
  atlas_title(16,95,130,name,MD_INK);
  atlas_text(16,141,130,&FONT_XS,md_biome_family(id),tint);
  cv_text(16,157,&FONT_T,tempo,MD_INK);
  cv_text(44,157,&FONT_T,"BPM",MD_DIM);
  cv_text(77,157,&FONT_T,"ROOT",MD_DIM);
  cv_text(109,157,&FONT_T,notes[card.root],MD_INK);
 }else{
  cv_text(11,104,&FONT_B,"No Biomes in this filter",MD_INK);
  cv_text(11,129,&FONT_H,"Turn K1 to choose a family",MD_DIM);
 }
 cv_text(11,183,&FONT_H,"K2 / PRESET Browse     SEL Load",MD_INK);
 cv_text(11,203,&FONT_XS,biome_favorite(id)?"REC Unstar  /  Stop + SAVE":"REC Star  /  Stop + SAVE",MD_DIM);
 cv_text(11,220,&FONT_XS,"HOME Atlas / GLO User Banks",MD_INK);
 if(dream_ui.msg[0]){
  cv_rect(0,216,240,24,MD_CARD);
  cv_text(11,220,&FONT_XS,dream_ui.msg,color);
 }
}
