/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
static int browse_filter;
static int browse_position;
/* 2.4: more ways in (all UI, nothing saved): NEW and RECENT filters, a letter jump (K3), A-Z order (K4) */
static int16_t browse_recent[8];
static int browse_recent_count;
static int browse_alpha;
static int browse_letter;
static int16_t browse_sorted[MD_BIOMES];
static int browse_sorted_ready;

static int browse_lower(int c){return c>='A'&&c<='Z'?c+32:c;}
static int browse_name_less(int a,int b){
 const char *x=md_biome_name(a),*y=md_biome_name(b);
 while(*x&&browse_lower((unsigned char)*x)==browse_lower((unsigned char)*y)){x++;y++;}
 const int c=browse_lower((unsigned char)*x)-browse_lower((unsigned char)*y);
 return c?c<0:a<b;
}
static int browse_at(int position){
 if(position<0||position>=MD_BIOMES)return -1;
 if(!browse_alpha)return md_biome_at((unsigned)position);
 if(!browse_sorted_ready){
  for(int i=0;i<MD_BIOMES;i++){
   int v=i,j=i;
   while(j>0&&browse_name_less(v,browse_sorted[j-1])){browse_sorted[j]=browse_sorted[j-1];j--;}
   browse_sorted[j]=(int16_t)v;
  }
  browse_sorted_ready=1;
 }
 return browse_sorted[position];
}
static void browse_note_loaded(int id){
 if(id<0||id>=MD_BIOMES)return;
 int at=browse_recent_count;
 for(int i=0;i<browse_recent_count;i++)if(browse_recent[i]==id){at=i;break;}
 if(at==browse_recent_count&&browse_recent_count<8)browse_recent_count++;
 if(at>=8)at=7;
 for(int i=at;i>0;i--)browse_recent[i]=browse_recent[i-1];
 browse_recent[0]=(int16_t)id;
}
static int browse_is_recent(int id){
 for(int i=0;i<browse_recent_count;i++)if(browse_recent[i]==id)return 1;
 return 0;
}

static int biome_favorite(int id){
 return id>=0&&id<MD_BIOMES&&(performance_prefs.favorites[id/8]&(1u<<(id%8)));
}

static int browse_matches(int id){
 if(id<0)return 0;
 if(!browse_filter)return 1;
 if(browse_filter==(int)md_biome_family_count()+1)return biome_favorite(id);
 if(browse_filter==(int)md_biome_family_count()+2)return id>=MD_VOICES_FIRST_BIOME;
 if(browse_filter==(int)md_biome_family_count()+3)return browse_is_recent(id);
 return md_biome_family_index(id)==browse_filter-1;
}

static int browse_biome(void){
 return browse_position<0?-1:browse_at(browse_position);
}

static void browse_open(int current){
 browse_position=-1;
 for(unsigned position=0;position<MD_BIOMES;position++){
  const int id=browse_at((int)position);
  if(!browse_matches(id))continue;
  if(browse_position<0||id==current)browse_position=(int)position;
  if(id==current)break;
 }
 const int id=browse_biome();
 app_say(id<0?"NO BIOMES IN FILTER":md_biome_name(id),"");
}

static void browse_change_filter(int delta){
 const int current=browse_biome();
 browse_filter=clamp(browse_filter+delta,0,(int)md_biome_family_count()+3);
 browse_open(current);
}

static void browse_move(int delta){
 if(!delta)return;
 const int direction=delta>0?1:-1;
 int position=browse_position;
 if(position<0)position=direction>0?-1:MD_BIOMES;
 for(position+=direction;position>=0&&position<MD_BIOMES;position+=direction){
  if(!browse_matches(browse_at(position)))continue;
  browse_position=position;
  delta-=direction;
  if(!delta)break;
 }
 const int id=browse_biome();
 if(id>=0)app_say(md_biome_name(id),"");
}

static void browse_jump_letter(int delta){
 browse_letter=((browse_letter+delta)%26+26)%26;
 const int want='a'+browse_letter;
 for(int position=0;position<MD_BIOMES;position++){
  const int id=browse_at(position);
  if(!browse_matches(id))continue;
  if(browse_lower((unsigned char)md_biome_name(id)[0])!=want)continue;
  browse_position=position;
  app_say(md_biome_name(id),"");
  return;
 }
 char note[8]={'N','O',' ',(char)('A'+browse_letter),0};
 app_say(note,"");
}

static void browse_toggle_order(void){
 const int current=browse_biome();
 browse_alpha^=1;
 browse_open(current);
 app_say(browse_alpha?"ORDER A TO Z":"ORDER BY FAMILY","");
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
 const char *filter=browse_filter==0?"ALL":
  browse_filter==(int)md_biome_family_count()+1?"FAVORITES":
  browse_filter==(int)md_biome_family_count()+2?"NEW IN 2.4":
  browse_filter==(int)md_biome_family_count()+3?"RECENT":
  md_biome_family_name((unsigned)(browse_filter-1));
 cv_text(11,7,&FONT_M,"BIOMES",MD_INK);
 {/* 2.4: the place in the list, and a star when the loaded Biome has been changed */
  int total=0,at=0;
  for(int position=0;position<MD_BIOMES;position++){const int b=browse_at(position);if(!browse_matches(b))continue;total++;if(position==browse_position)at=total;}
  char count[16];md_num(at,count);unsigned n=0;while(count[n])n++;count[n++]='/';md_num(total,count+n);
  cv_text(236-text_w(&FONT_XS,count),12,&FONT_XS,count,MD_DIM);
  if(md_unsaved())cv_text(236-text_w(&FONT_XS,count)-10,12,&FONT_XS,"*",color);
 }
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
