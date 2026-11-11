/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
static struct {
 md_journey plan;
 int8_t section,next;
 uint8_t keep;
 uint16_t step;
} timeline;

static int timeline_kit(unsigned biome,int home,unsigned slot){
 static unsigned cached_id[2]={65535,65535};
 static uint8_t cached_kit[2];
 if(biome==65535)return home;
 if(cached_id[slot]!=biome){
  cached_kit[slot]=md_preview_biome_kit(&bank_patch_io,biome);
  cached_id[slot]=biome;
 }
 return cached_kit[slot];
}

static void timeline_update(void){
 if(dream_ui.page<JOURNEY||dream_ui.page>JOURNEY_KEEP)return;
 uint32_t lock=plat_lock();
 timeline.plan=md_project_state()->journey;
 const int home=md_project_state()->kit.enabled,live=md_state()->kit.enabled;
 const int section=md_journey_position();
 timeline.section=section;timeline.step=md_journey_step();
 plat_unlock(lock);
 if(section>=0)journey_edit=section;
 const md_journey *p=&timeline.plan;
 int next=section>=0?section+1:journey_edit;
 timeline.keep=0;
 if(next>=p->length)next=section>=0&&p->mode==2?0:-1;
 timeline.next=next;
 if(next<0||(section<0&&next==0&&p->mode!=2))return;
 unsigned before=(next+p->length-1)%p->length;
 int source=section>=0?live:timeline_kit(p->section[before].biome,home,0);
 int target=timeline_kit(p->section[next].biome,home,1);
 timeline.keep=p->section[next].keep&(source!=target?7:target?15:63);
}

static unsigned timeline_weight(const md_journey_section *s){return (unsigned)s->bars*s->repeats;}

static void draw_journey_timeline(uint16_t color){
 const md_journey *p=&timeline.plan;
 unsigned total=0,offset=0,space=216-4*p->length;
 int x=12;
 for(unsigned i=0;i<p->length;i++)total+=timeline_weight(&p->section[i]);
 if(!total)return;
 for(unsigned i=0;i<p->length;i++){
  offset+=timeline_weight(&p->section[i]);
  int end=12+4*(i+1)+space*offset/total;
  int width=end-x-1,active=i==timeline.section,selected=i==journey_edit;
  md_biome_card card;
  uint16_t shade=md_biome_card_info(p->section[i].biome,&card)?atlas_colour(card.seed,p->section[i].biome):color;
  uint16_t ink=active?MD_INK:md_tint(MD_BG,shade,selected?28:19);
  cv_rect(x,93,width,6,md_tint(MD_BG,shade,active?17:9));
  cv_rect(x,99,width,7,md_tint(MD_BG,shade,active?10:5));
  cv_rect(x,93,width,2,ink);
  if(selected)cv_rect(x,104,width,2,color);
  unsigned repeats=p->section[i].repeats;
  if(repeats>1&&width>=6*(int)repeats)
   for(unsigned repeat=1;repeat<repeats;repeat++)
    cv_rect(x+width*repeat/repeats,96,1,7,md_tint(MD_BG,ink,13));
  if(width>=12){
   char label[2]={'1'+i,0};
   cv_text(x+(width-6)/2,106,&FONT_T,label,active?MD_INK:ink);
  }
  if(active){
   unsigned steps=16*timeline_weight(&p->section[i]);
   int play=x+(width-1)*timeline.step/steps;
   cv_rect(x,103,play-x+1,2,shade);
   cv_rect(play,89,1,17,MD_INK);cv_rect(play-1,89,3,2,MD_INK);
  }
  x=end;
 }
}

static void draw_journey_anchors(uint16_t color){
 char next[12];
 if(timeline.next<0){md_text(12,131,timeline.section<0?"OUTSIDE ARRANGEMENT":"END OF JOURNEY",MD_DIM);return;}
 md_text(12,131,timeline.section<0?"ANCHORS INTO":"KEEP INTO",MD_INK);
 md_num(timeline.next+1,next);md_text(103,131,next,color);
 for(int i=0;i<6;i++){
  char label[2]={'1'+i,0};
  int x=125+17*i,keep=timeline.keep&(1u<<i);
  uint16_t ink=keep?dream_colors[i]:MD_EDGE;
  cv_rect(x,129,15,15,keep?md_tint(MD_BG,ink,10):MD_CARD);
  cv_text(x+4,130,&FONT_T,label,ink);
 }
}
