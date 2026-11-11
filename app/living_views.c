/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
enum { LIVING_STANDARD, LIVING_FERN, LIVING_ROOTS, LIVING_CLOUD,
 LIVING_BOW, LIVING_DUB, LIVING_CLAY, LIVING_HORIZON,
 LIVING_LANTERNS, LIVING_KILN, LIVING_PORCELAIN, LIVING_ENSEMBLE };
static struct {
 uint8_t profile, level[6], muted, age[6], position[4], window[4], mix;
} living;

static void living_update(uint32_t now){
 const md_patch *p=md_state();
 const int id=p->biome_id;
 const int rule_offset=id-MD_RULE_FIRST_BIOME;
 uint8_t profile=id==213?LIVING_FERN:id==226?LIVING_ROOTS:
  id==217?LIVING_CLOUD:LIVING_STANDARD;
 // The twelve step-rule Biomes are arranged in four sets of three.
 if(rule_offset>=0&&id<MD_MALLET_FIRST_BIOME)profile=LIVING_BOW+rule_offset/3;
 if(id>=MD_MALLET_FIRST_BIOME&&id<MD_ENSEMBLE_FIRST_BIOME)profile=LIVING_LANTERNS+id-MD_MALLET_FIRST_BIOME;
 if(id>=MD_ENSEMBLE_FIRST_BIOME&&id<MD_BIOMES)profile=LIVING_ENSEMBLE+id-MD_ENSEMBLE_FIRST_BIOME;
 if(living.profile!=profile)memset(&living,0,sizeof living);
 living.profile=profile;
 const int tape=dream_ui.page==GRAIN;
 const int mixer=dream_ui.page==MIXER;
 if(!tape&&!mixer&&(dream_ui.page!=LIVE||dream_ui.live_view!=LIVE_ATLAS))return;
 const int moving=dream_prefs.motion;
 uint32_t lock=plat_lock();
 living.muted=0;
 for(unsigned i=0;i<6;i++){
  if(p->track[i].mute||(p->kit.enabled&&i>=4))living.muted|=1u<<i;
  int level=(moving||mixer)?clamp((int)(md_status()->track_peak[i]*800.f),0,100):0;
  if(living.muted&(1u<<i))level=0;
  int held=(int)living.level[i]-5;
  living.level[i]=(moving||mixer)&&!(living.muted&(1u<<i))&&held>level?held:level;
  uint32_t age=now-pulse_start[i];
  living.age[i]=moving&&!(living.muted&(1u<<i))&&(pulse_active&(1u<<i))&&age<160?age:160;
 }
 living.mix=p->macro[3]/10;
 if((tape||profile==LIVING_CLOUD||profile==LIVING_HORIZON||profile==LIVING_PORCELAIN||
     profile==LIVING_ENSEMBLE+1||profile==LIVING_ENSEMBLE+3||
     profile==LIVING_ENSEMBLE+6||profile==LIVING_ENSEMBLE+8||
     id==MD_VOWEL_FIRST_BIOME||id==MD_SOFT_FIRST_BIOME-1||
     id==MD_SOFT_FIRST_BIOME+2||id==MD_SOFT_FIRST_BIOME+4||
     id==MD_SOFT_FIRST_BIOME+7||id==MD_LAYERS_FIRST_BIOME+2)&&moving&&living.mix)
  md_grain_positions(living.position,living.window);
 else memset(living.window,0,sizeof living.window);
 plat_unlock(lock);
}

static void living_fern(void){
 const uint16_t fern=RGB(147,226,170);
 cv_line(49,128,48,53,md_tint(MD_BG,fern,16));
 for(int i=0;i<6;i++){
  int base=112-i*10,side=i&1?1:-1,level=living.level[i];
  int width=15+(5-i)+level/8;
  int tip_x=49+side*width,tip_y=base-12-level/14;
  uint16_t ink=md_tint(MD_BG,fern,(living.muted&(1u<<i))?6:15+level/7);
  md_fine_line(49*16,base*16,tip_x*16,tip_y*16,ink);
  md_fine_line(49*16,base*16,(49+side*(width*2/3))*16,(base+2)*16,ink);
  md_fine_line((49+side*(width*2/3))*16,(base+2)*16,tip_x*16,tip_y*16,ink);
  for(int j=1;j<=3;j++){
   int x=49+side*width*j/4,y=base+(tip_y-base)*j/4;
   md_fine_line(x*16,y*16,(x+side*3)*16,(y+4)*16,ink);
  }
  if(level>16)md_circle(tip_x,tip_y,1,fern,1);
 }
}

static void living_roots(void){
 const uint16_t clay=RGB(246,174,96);
 md_circle(47,121,6,md_tint(MD_BG,clay,15),0);
 for(int i=0;i<6;i++){
  int x=15+i*13,y=58+(i%3)*10,knee=91+(i&1)*7;
  uint16_t ink=md_tint(MD_BG,clay,(living.muted&(1u<<i))?5:14);
  cv_line(47,116,x,knee,ink);cv_line(x,knee,x,y,ink);
  cv_line(x,knee,x+(i<3?-5:5),knee-8,ink);
  md_circle(x,y,3,ink,0);
  if(living.age[i]<160){
   int age=living.age[i],px=x,py=y;
   if(age<72){px=47+(x-47)*age/72;py=116+(knee-116)*age/72;}
   else py=knee+(y-knee)*(age-72)/88;
   md_circle(px,py,2,md_tint(clay,MD_INK,15-age/12),1);
  }
 }
}

static void living_cloud(void){
 const uint16_t violet=RGB(161,163,253),ice=RGB(150,227,246);
 for(int arc=0;arc<3;arc++){
  int py=0;
  for(int x=12;x<=85;x+=3){
   int n=x-48;
   int y=73+arc*15+n*n/102;
   if(x>12)cv_line(x-3,py,x,y,md_tint(MD_BG,violet,8+arc*3));
   py=y;
  }
 }
 md_circle(48,87,14,md_tint(MD_BG,violet,14),0);
 if(!living.mix)return;
 for(int i=0;i<4;i++)if(living.window[i]){
  int phase=living.window[i]-1,energy=phase<64?phase:127-phase;
  int x=13+living.position[i]*70/127,n=x-48,y=69+i*11+n*n/105;
  uint16_t ink=md_tint(MD_BG,i&1?violet:ice,8+energy*living.mix/300);
  md_circle(x,y,3+energy/24,ink,0);md_circle(x,y,1,ice,1);
 }
}

static void living_bow(void){
 const uint16_t gold=RGB(255,201,144),orchid=RGB(220,156,229);
 for(int i=0;i<6;i++){
  const int level=living.level[i],x=22+i*10,y=70+(i&1)*5;
  const int bend=11+(5-i)+level/8;
  const uint16_t ink=md_tint(MD_BG,i&1?orchid:gold,(living.muted&(1u<<i))?5:17+level/8);
  int px=x,py=y;
  for(int k=1;k<=6;k++){
   int nx=x+bend*k*(6-k)/9,ny=y+k*8;
   md_fine_line(px*16,py*16,nx*16,ny*16,ink);
   px=nx;py=ny;
  }
  cv_line(x,y,x,y+48,md_tint(MD_BG,ink,15));
  if(level>18)md_circle(x+bend,y+24,1,i&1?orchid:gold,1);
 }
}

static void living_dub(void){
 const uint16_t teal=RGB(118,232,196),blue=RGB(113,157,230);
 for(int i=0;i<6;i++){
  const int level=living.level[i],y=65+i*10,crest=8+level/8;
  const int hit=living.age[i],mid=39+(i&1)*13;
  const uint16_t colour=i&1?blue:teal;
  const uint16_t ink=md_tint(MD_BG,colour,(living.muted&(1u<<i))?5:13+level/7);
  cv_line(14,y,mid-12,y,ink);
  md_fine_line((mid-12)*16,y*16,mid*16,(y-crest)*16,ink);
  md_fine_line(mid*16,(y-crest)*16,(mid+15)*16,y*16,ink);
  cv_line(mid+15,y,82,y,ink);
  if(hit<160){
   const int x=mid+(82-mid)*hit/160;
   const int slope=crest*(mid+15-x)/15;
   md_circle(x,y-(slope>0?slope:0),1,md_tint(MD_BG,colour,31-hit/8),1);
  }
 }
}

static void living_clay(void){
 static const uint8_t x[]={27,61,43,70,23,54},y[]={80,72,104,101,116,120};
 const uint16_t clay=RGB(244,172,112),peach=RGB(255,218,171);
 cv_line(19,109,71,84,md_tint(MD_BG,clay,6));
 for(int i=0;i<6;i++){
  const int age=living.age[i],level=living.level[i],radius=4+(i%3)*2;
  const uint16_t ink=md_tint(MD_BG,clay,(living.muted&(1u<<i))?5:15+level/8);
  md_circle(x[i],y[i],radius,ink,0);
  md_circle(x[i]-1,y[i]-1,2,ink,1);
  if(age<160)md_circle(x[i],y[i],radius+1+age/18,md_tint(MD_BG,peach,29-age/7),0);
 }
}

static int living_tide_height(int x,int lane){
 const int n=x-47;
 return 70+lane*12+n*n/127;
}

static void living_horizon(void){
 const uint16_t lilac=RGB(181,165,252),foam=RGB(134,218,230);
 for(int lane=0;lane<4;lane++){
  const uint16_t ink=md_tint(MD_BG,lane&1?foam:lilac,10+lane*3);
  for(int x=14;x<80;x+=6)cv_line(x,living_tide_height(x,lane),x+6,living_tide_height(x+6,lane),ink);
  if(!living.window[lane])continue;
  const int phase=living.window[lane]-1,energy=phase<64?phase:127-phase;
  const int x=15+living.position[lane]*64/127,y=living_tide_height(x,lane);
  const uint16_t light=md_tint(MD_BG,lane&1?foam:lilac,10+energy*living.mix/300);
  md_circle(x,y,2+energy/20,light,0);
  md_circle(x,y,1,light,1);
 }
}

static void living_lanterns(void){
 const uint16_t amber=RGB(255,199,108),moss=RGB(153,220,165);
 for(int i=0;i<6;i++){
  const int x=23+i*10,y=64+(i%3)*18,level=living.level[i];
  const int radius=4+level/17;
  const uint16_t colour=i&1?moss:amber;
  const uint16_t ink=md_tint(MD_BG,colour,(living.muted&(1u<<i))?5:16+level/8);
  cv_line(x,54+(i&1)*8,x,y,md_tint(MD_BG,colour,10));
  md_circle(x,y+radius,radius,ink,0);
  cv_line(x-2,y+2*radius+2,x+2,y+2*radius+2,ink);
  if(level>15)md_circle(x,y+radius,1+level/38,ink,1);
 }
}

static void living_kiln(void){
 const uint16_t clay=RGB(251,160,127),gold=RGB(243,210,135);
 for(int i=0;i<6;i++){
  const int y=69+i*9,level=living.level[i],side=9+i*4+level/10;
  const int age=living.age[i];
  const uint16_t ink=md_tint(MD_BG,clay,(living.muted&(1u<<i))?5:15+level/8);
  cv_line(48-side,y-6,48-side+5,y,ink);
  cv_line(48-side+5,y,48+side-5,y,ink);
  cv_line(48+side-5,y,48+side,y-6,ink);
  if(age<160)md_circle(48,y-3-age/14,1,md_tint(MD_BG,gold,31-age/8),1);
 }
}

static void living_porcelain(void){
 const uint16_t ice=RGB(143,223,239),lilac=RGB(196,170,254);
 for(int i=0;i<4;i++){
  const int y=76+i*13;
  const int x=28+i*14,level=living.level[i],span=7+level/15;
  const uint16_t colour=i&1?ice:lilac;
  const uint16_t ink=md_tint(MD_BG,colour,(living.muted&(1u<<i))?5:16+level/8);
  cv_line(x-span,y+9,x,y-span,ink);
  cv_line(x,y-span,x+span,y+9,ink);
  cv_line(x+span,y+9,x-span,y+9,ink);
  if(living.window[i]){
   const int bead=16+living.position[i]*65/127;
   md_circle(bead,y+13,1,colour,1);
  }
 }
}

static void living_ensemble(void){
 const unsigned scene=living.profile-LIVING_ENSEMBLE;
 static const uint8_t shapes[]={ART_FROND,ART_MEDUSA,ART_CURRENT,ART_WIRE,ART_SHELL,ART_CELLS,
  ART_FROND,ART_SHELL,ART_CURRENT,ART_MEDUSA,ART_CELLS,ART_WIRE,ART_CORAL,
  ART_SHELL,ART_CURRENT,ART_MEDUSA,ART_FROND,ART_CELLS,ART_CORAL,ART_WIRE,ART_FROND,
  ART_FROND,ART_CORAL,ART_MEDUSA};
 static const uint16_t colours[]={RGB(171,233,180),RGB(138,218,245),RGB(247,208,131),
  RGB(182,169,249),RGB(245,164,124),RGB(164,190,249),RGB(161,230,213),
  RGB(231,175,118),RGB(151,207,248),RGB(198,185,238),RGB(241,183,112),
  RGB(140,218,202),RGB(244,151,187),RGB(226,177,123),RGB(131,192,171),
  RGB(143,227,232),RGB(166,214,153),RGB(243,160,136),RGB(242,199,111),
  RGB(210,182,226),RGB(152,185,220),
  RGB(175,237,181),RGB(238,179,118),RGB(159,222,245)};
 _Static_assert(sizeof shapes==MD_BIOMES-MD_ENSEMBLE_FIRST_BIOME,"one shape per Biome");
 _Static_assert(sizeof colours/sizeof colours[0]==sizeof shapes,"one colour per Biome");
 int energy=0;
 for(int i=0;i<6;i++)if(living.level[i]>energy)energy=living.level[i];
 uint16_t colour=colours[scene];
 if(scene>=6&&scene<9){
  const uint16_t ink=md_tint(MD_BG,colour,10);
  for(int i=0;i<6;i++){
   int offset=9+i*12,height=8+living.level[i]/10;
   cv_line(offset,111,offset+4,111-height,ink);
   cv_line(offset+4,111-height,offset+8,111,ink);
  }
 }
 atlas_mark(49,90,30,shapes[scene],md_world_dna(),clamp(energy*2,0,100),colour);
 for(int i=0;i<6;i++){
  int x=17+i*12,length=living.level[i]/18;
  uint16_t ink=md_tint(MD_BG,dream_colors[i],(living.muted&(1u<<i))?6:15+length*2);
  cv_rect(x,126-length,5,1+length,ink);
  if(living.age[i]<160)cv_pset(x+2,123-length,MD_INK);
 }
 for(int i=0;i<4;i++)if(living.window[i]){
  int x=14+living.position[i]*67/127,y=110+i*3;
  md_circle(x,y,1,md_tint(colour,MD_INK,11),1);
 }
}

static void draw_living_view(void){
 if(-cv_oy>=138||-cv_oy+(int)cv_h<=44)return;
 switch(living.profile){
  case LIVING_FERN:living_fern();break;
  case LIVING_ROOTS:living_roots();break;
  case LIVING_CLOUD:living_cloud();break;
  case LIVING_BOW:living_bow();break;
  case LIVING_DUB:living_dub();break;
  case LIVING_CLAY:living_clay();break;
  case LIVING_HORIZON:living_horizon();break;
  case LIVING_LANTERNS:living_lanterns();break;
  case LIVING_KILN:living_kiln();break;
  case LIVING_PORCELAIN:living_porcelain();break;
  default:if(living.profile>=LIVING_ENSEMBLE)living_ensemble();break;
 }
}

/* A small bed of seeds under the Home illustration. Position and colour
 * follow the live macros. Hits and peaks are read once per display frame. */
static void draw_live_seeds(const md_patch *p){
 if(!atlas_visible(127,11))return;
 const int density=p->macro[0]/334;
 const int motion=p->macro[1]/200;
 const int colour=p->macro[2]/40;
 const int space=p->macro[3]/250;
 const int count=md_active_tracks();
 for(int i=0;i<count;i++){
  const int x=14+69*(2*i+1)/(2*count),y=134;
  if(living.muted&(1u<<i)){cv_pset(x,y,MD_EDGE);continue;}
  const int hit=160-living.age[i],rise=hit*motion/200;
  const int height=1+density+living.level[i]/37;
  const int drift=(i&1?1:-1)*rise,top=y-height-rise;
  const uint16_t ink=md_tint(MD_BG,dream_colors[i],7+colour);
  cv_line(x,y,x+drift,top,ink);
  cv_line(x+drift-1,top+1,x+drift+1,top+1,ink);
  if(space)cv_line(x-space,y+1,x+space,y+1,md_tint(MD_BG,dream_colors[i],9));
  if(hit)cv_pset(x+drift,top,MD_INK);
 }
}

static void draw_grain_tape(const md_patch *p,uint16_t colour){
 if(!atlas_visible(44,108))return;
 atlas_plate(8,44,224,107,colour);
 cv_text(16,49,&FONT_XS,"TRACK SENDS",MD_INK);
 cv_text(158,49,&FONT_XS,p->freeze?"FROZEN":"LIVE TAPE",colour);
 const int tracks=p->kit.enabled?4:6;
 for(int i=0;i<tracks;i++){
  int x=24+i*(tracks==4?51:35);
  const unsigned send=p->track[i].send;
  const int off=(living.muted>>i)&1;
  const uint16_t ink=off?MD_EDGE:dream_colors[i];
  char number[2]={'1'+i,0};
  cv_text(x,70,&FONT_T,number,off?MD_DIM:ink);
  cv_rect(x-6,85,22,3,md_tint(MD_BG,colour,8));
  cv_rect(x-6,85,22*send/1000,3,ink);
  int peak=send*living.level[i]/4500;
  if(peak)cv_rect(x-6,82,peak,1,MD_INK);
 }
 cv_rect(17,100,206,34,MD_BG);
 atlas_filaments(20,103,201,25,4);
 for(int j=0;j<4;j++){
  int y=105+j*7;
  cv_rect(21,y,198,1,md_tint(MD_BG,colour,8+(j&1)*2));
  if(living.window[j]){
   int phase=living.window[j]-1,energy=phase<64?phase:127-phase;
   int x=22+living.position[j]*194/127;
   uint16_t ink=md_tint(MD_BG,dream_colors[j],14+energy/4);
   int radius=1+(p->grain_size>520)+(energy>36);
   md_circle(x,y,radius,ink,0);cv_pset(x,y,MD_INK);
  }
 }
 for(int x=21;x<220;x+=33)cv_rect(x,100,1,3,md_tint(MD_BG,colour,14));
 cv_text(16,137,&FONT_T,living.mix?"GRAIN POSITIONS":"SPACE IS AT ZERO",MD_DIM);
}
