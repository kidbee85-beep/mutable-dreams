/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
static void draw_mixer_field(const md_patch *p,uint16_t colour){
 if(!atlas_visible(44,108))return;
 static const uint16_t buses[]={RGB(139,222,192),RGB(176,173,244),RGB(246,203,138)};
 atlas_plate(8,44,224,108,colour);
 atlas_filaments(13,68,215,62,7);
 cv_text(14,48,&FONT_T,"LEVEL / PEAK",MD_INK);
 for(int bus=0;bus<3;bus++){
  static const char *const legend[]={"GRAIN","DELAY","ROOM"};
  cv_text(95+45*bus,48,&FONT_T,legend[bus],buses[bus]);
 }
 const int tracks=p->kit.enabled?4:6;
 for(int i=0;i<tracks;i++){
  const int left=16+i*(tracks==4?52:36);
  const int selected=i==dream_ui.track,muted=p->track[i].mute;
  const uint16_t tone=muted?MD_DIM:dream_colors[i];
  const uint16_t quiet=md_tint(MD_BG,tone,8);
  const int level=p->track[i].level, stem=left+8, peak=40*living.level[i]/100;
  const int top=116-40*level/1000;
  char number[8]={'1'+i,0};
  if(selected){
   cv_rect(left-3,63,30,81,MD_CARD);
   cv_line(left-2,64,left+25,64,colour);
   cv_line(left-2,145,left+25,145,colour);
  }
  cv_text(left,65,&FONT_T,number,selected?MD_INK:tone);
  md_num(level/10,number);
  cv_text(left+8,65,&FONT_T,muted?"OFF":number,muted?MD_DIM:tone);
  cv_line(stem,77,stem,116,quiet);
  if(level)cv_line(stem,top,stem,116,tone);
  // The five veins trace the set level. The adjacent narrow meter shows audio.
  for(int leaf=0;leaf<5;leaf++){
   const int y=114-7*leaf, side=leaf&1?1:-1;
   const uint16_t vein=y>=top&&level?tone:quiet;
   cv_line(stem,y,stem+6*side,y-4,vein);
   cv_line(stem+6*side,y-4,stem+2*side,y-4,vein);
  }
  if(level)cv_rect(stem-1,top,3,2,tone);
  cv_rect(left+20,76,2,41,quiet);
  if(peak){cv_rect(left+20,117-peak,2,peak,tone);cv_rect(left+19,117-peak,4,1,MD_INK);}
  const int sends[]={p->track[i].send,p->ext[i].delay_send,p->ext[i].reverb_send};
  for(int bus=0;bus<3;bus++){
   int y=123+bus*6;
   const int fill=23*sends[bus]/1000;
   cv_rect(left,y,23,1,md_tint(MD_BG,buses[bus],8));
   if(fill){
    uint16_t ink=md_tint(MD_BG,buses[bus],muted?12:27);
    cv_rect(left,y,fill,1,ink);cv_rect(left+fill-1,y-1,2,3,ink);
   }
  }
 }
}

static void draw_space_field(const md_patch *p){
 if(!atlas_visible(44,108))return;
 const uint16_t echo=RGB(169,185,248),room=RGB(240,202,142);
 const int return_level=p->fx_level;
 atlas_plate(8,44,108,108,echo);atlas_plate(124,44,108,108,room);
 atlas_filaments(13,67,96,61,6);
 atlas_filaments(129,67,96,61,9);
 cv_text(15,50,&FONT_XS,"DELAY",echo);cv_text(131,50,&FONT_XS,"ROOM",room);
 const int gap=6+p->delay_ms*13/350;
 int height=48;
 cv_rect(16,126,91,1,md_tint(MD_BG,echo,12));
 for(int tap=0;tap<5;tap++){
  int x=21+tap*gap;
  uint16_t ink=md_tint(MD_BG,echo,6+return_level/85+(tap==0?8:0));
  if(height>0){
   cv_rect(x,126-height,tap?2:3,height,ink);
   cv_pset(x,126-height,echo);
  }
  height=height*p->delay_fb/1000;
 }
 const int length=24+p->reverb_time*68/850;
 cv_rect(132,126,92,1,md_tint(MD_BG,room,12));
 for(int at=0;at<length;at+=4){
  int rest=length-at;
  int y=125-rest*rest*46/(length*length);
  int x=132+at;
  uint16_t ink=md_tint(MD_BG,room,6+return_level/100+rest*10/length);
  cv_rect(x,y,1,125-y,ink);
  cv_pset(x,y,room);
 }
 cv_text(15,135,&FONT_T,"ECHO SPACING",MD_DIM);
 cv_text(131,135,&FONT_T,"TAIL LENGTH",MD_DIM);
}
