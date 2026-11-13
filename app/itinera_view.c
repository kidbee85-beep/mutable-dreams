/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
static md_itinera_view radio_view;
static uint32_t radio_activity;
static uint8_t radio_preset,radio_saved[MD_ITINERA_PRESETS];

static unsigned radio_settings(void){
 return radio_saved[radio_preset]?radio_saved[radio_preset]-1:md_itinera_defaults(radio_preset);
}
static void radio_adjust(unsigned knob,int delta){
 unsigned settings=radio_settings();
 int duration=settings%4,pace=settings/4%3,hall=settings/12;
 if(knob==0)radio_preset=clamp(radio_preset+delta,0,MD_ITINERA_PRESETS-1);
 if(knob==1)duration=clamp(duration+delta,0,3);
 if(knob==2)pace=clamp(pace+delta,0,2);
 if(knob==3)hall=clamp(hall+delta,0,2);
 if(knob)radio_saved[radio_preset]=1+duration+pace*4+hall*12;
 dream_ui.dirty=1;
}
static void radio_center(int y,const felucca_font_t *font,const char *text,uint16_t color){
 cv_text((240-text_w(font,text))/2,y,font,text,color);
}
// Share the quarter-wave lookup across the many scene drawing sites.
static __attribute__((noinline)) int radio_wave(unsigned p){
 static const uint8_t quarter[]={0,6,12,19,24,30,36,41,45,49,53,56,59,61,63,64,64};
 p&=63u;
 const unsigned i=p&31u;
 const int y=quarter[i>16?32-i:i];
 return p&32u?-y:y;
}
/* G-1: the same wave between its 64 steps, for a phase that moves every frame
 * (the steps were 145 ms apart, slower than the 66 ms frames). p >= 0. */
static __attribute__((noinline)) float radio_wf(float p){
 const unsigned i=(unsigned)p;const float f=p-(float)i;
 const int a=radio_wave(i),b=radio_wave(i+1u);
 return (float)a+(float)(b-a)*f;
}
// md_sway's triangle (-16..16), continuous.
static float radio_swayf(float p){
 const unsigned i=(unsigned)p;const float f=p-(float)i;
 return (float)md_sway(i)+(float)(md_sway(i+1u)-md_sway(i))*f;
}
static float radio_absf(float x){return x<0?-x:x;}
// sin(pi x) for x in 0..1, from the wave table (G-3's day).
static float radio_sine_half(float x){return radio_wf(32.f*(x<0?0:x>1?1:x))/64.f;}
static float radio_smooth(float t){t=t<0?0:t>1?1:t;return t*t*(3-2*t);}
static int radio_round(float x){return (int)(x<0?x-.5f:x+.5f);}
/* G-6: the current layer's parallax offset (px), added to every stroke. */
static float radio_ox,radio_oy;
// Subpixel, softened strokes: one soft point a pixel along the line (as
// md_fine_line), but only the part of the line that crosses this LCD tile is
// walked. The art runs once per tile; a whole walk per tile cost 12 walks.
static __attribute__((noinline)) void radio_line(float x0,float y0,float x1,float y1,uint16_t ink){
 x0+=radio_ox;x1+=radio_ox;y0+=radio_oy;y1+=radio_oy;
 const float top=(float)cv_y0-cv_oy-2,bottom=(float)cv_y1-cv_oy+1;
 if((y0<top&&y1<top)||(y0>=bottom&&y1>=bottom))return;
 const float dx=x1-x0,dy=y1-y0,ax=radio_absf(dx),ay=radio_absf(dy);
 const int n=radio_round(ax>ay?ax:ay)+1;
 if(!(n>0&&n<=512))return;                  /* (no stroke is longer than the screen) */
 int k0=0,k1=n;
 if(ay>.001f){
  float t0=(top-y0)/dy,t1=(bottom-y0)/dy;
  if(t0>t1){const float t=t0;t0=t1;t1=t;}
  if(t0>0)k0=(int)(t0*n);
  if(t1<1)k1=(int)(t1*n)+1;
  if(k1>n)k1=n;
 }
 const float sx=dx/n,sy=dy/n;
 for(int k=k0;k<=k1;k++)md_aa_point(radio_round((x0+sx*k)*16),radio_round((y0+sy*k)*16),ink,32);
}
static void radio_dot(float x,float y,int wide,uint16_t ink){
 x+=radio_ox;y+=radio_oy;
 md_aa_point(radio_round(x*16),radio_round(y*16),ink,32);
 if(wide)md_aa_point(radio_round(x*16)+16,radio_round(y*16),ink,32);
}
/* Z-1: does the art's band top..bottom (with this layer's offset) reach the
 * LCD tile being drawn? A loop over a whole ridge, row or band asks first:
 * the art runs once per tile, and most of it lies in other tiles. */
static int radio_band(float top,float bottom){
 return bottom+radio_oy+cv_oy>=cv_y0-3&&top+radio_oy+cv_oy<cv_y1+3;
}
static void radio_circle(int x,int y,int r,uint16_t c,int fill){md_circle(x+radio_round(radio_ox),y+radio_round(radio_oy),r,c,fill);}
static void radio_orbit(float cx,float cy,float rx,float ry,float slant,uint16_t ink){
 if(cy+radio_oy+ry+radio_absf(rx*slant/8)+2+cv_oy<cv_y0||cy+radio_oy-ry-radio_absf(rx*slant/8)-2+cv_oy>=cv_y1)return;
 float x=cx+rx,y=cy+rx*slant/8;
 for(unsigned a=4;a<=64;a+=4){
  const float dx=radio_wave(a+16)*rx/64;
  const float nx=cx+dx,ny=cy+radio_wave(a)*ry/64+dx*slant/8;
  radio_line(x,y,nx,ny,ink);x=nx;y=ny;
 }
}
/* G-2: what the art follows, eased per frame (the art runs once per LCD tile,
 * so the state moves only when the frame's clock does). The phase's integer
 * wrap keeps float precision: 145 ms x 64 x 420, a multiple of every divisor
 * the scenes use (1..7), so no motion jumps when it wraps. */
static struct {
 uint32_t frame,seed_at;
 unsigned seed_now,seed_was;
 int8_t layer_now,layer_was;     /* G-4: the Biome family's layer, -1 none */
 float level[MD_TRACKS];         /* G-5: each track's eased loudness, 0..1 */
 uint8_t grain_pos[4],grain_win[4];   /* G-5: the grain cloud's grains, as the Atlas reads them */
 float partf,chapterf;
 uint8_t valid;
} radio_ease;
static float radio_phase(void){
 if(!dream_prefs.motion)return 0;
 return (float)(flower_time%(145u*64u*420u))*(1.0f/145.0f);
}
// G-3: how far the dream is (0..1), from the seconds it has left.
static float radio_dream(void){
 static const uint16_t minutes[4]={30,60,120,240};
 const unsigned total=minutes[radio_settings()%4]*60u;
 const unsigned remaining=radio_view.remaining<total?radio_view.remaining:total;
 return (float)(total-remaining)/total;
}
// The art's chapter (0..3.999). G-3: it grows with the dream itself, so sizes,
// heights and the sky move a little all the time (G-2 eased only the first
// 20 s of each chapter). Still pictures keep the whole chapter.
static float radio_chapter_target(void){
 const unsigned chapter=radio_view.chapter>3u?3u:radio_view.chapter;
 if(!dream_prefs.motion)return (float)chapter;
 const float c=radio_dream()*4.f;
 return c<3.999f?c:3.999f;
}
/* G-4: a second, quiet layer for each Biome family (0 fog, 1 water lines,
 * 2 ice glints, 3 rain, 4 pollen, 5 embers), so that the scenes of one dream
 * look different. -1: none (Blank). */
static int radio_layer_of(int biome){
 static const int8_t layer[]={0,0,2,3,3,4,5,4,-1,3,2,1,5,4,5,0,5,0,0,0};
 const int family=md_biome_family_index(biome);
 return family>=0&&family<(int)sizeof layer?layer[family]:-1;
}
static void radio_follow(unsigned seed,unsigned part){
 if(!dream_prefs.motion||!radio_ease.valid){         /* still pictures: no easing */
  radio_ease.valid=1;radio_ease.frame=flower_time;
  radio_ease.seed_now=radio_ease.seed_was=seed;radio_ease.seed_at=flower_time-7000u;
  radio_ease.layer_now=radio_ease.layer_was=(int8_t)radio_layer_of(radio_view.biome);
  radio_ease.partf=(float)part;radio_ease.chapterf=radio_chapter_target();return;
 }
 if(radio_ease.frame==flower_time)return;
 const uint32_t dt=flower_time-radio_ease.frame;
 radio_ease.frame=flower_time;
 if(seed!=radio_ease.seed_now){                      /* a new Biome: its stars travel from the last one's */
  radio_ease.seed_was=radio_ease.seed_now;
  radio_ease.seed_now=seed;radio_ease.seed_at=flower_time;
  radio_ease.layer_was=radio_ease.layer_now;radio_ease.layer_now=(int8_t)radio_layer_of(radio_view.biome);
 }
 const float k=dt>4000u?1.f:dt/4000.f;
 radio_ease.partf+=((float)part-radio_ease.partf)*k;
 // G-5: what the sound does now, read once a frame (the grains under the lock)
 {const float rise=dt>150u?1.f:dt/150.f,fall=dt>900u?1.f:dt/900.f;
  uint32_t lock=plat_lock();
  for(unsigned i=0;i<MD_TRACKS;i++){float v=md_status()->track_peak[i]*8.f;v=v>1?1:v;radio_ease.level[i]+=(v-radio_ease.level[i])*(v>radio_ease.level[i]?rise:fall);}
  md_grain_positions(radio_ease.grain_pos,radio_ease.grain_win);
  plat_unlock(lock);}
 radio_ease.chapterf=radio_chapter_target();
}
// One sky band of a chapter, as before; the art mixes two chapters' bands.
static uint16_t radio_sky(int band,int chapter,uint16_t color,uint16_t accent){
 const int height=band-(4+chapter*2);
 const unsigned light=10-(height<0?-height:height)/2;
 const uint16_t sky=md_tint(color,accent,clamp(band*3-chapter*2,0,32));
 return md_tint(MD_BG,sky,light);
}
static void radio_star(unsigned j,unsigned seed,int h,float *x,float *dy){
 const unsigned point=j*47+radio_preset*19+seed;
 *x=(float)(28+point%183);
 *dy=(float)(8+(j*19+radio_preset*11+seed)%(h-15));
}
/* G-4: one family layer at strength a (0..1), light enough to stay behind the
 * dream's own motif; t is the frame's phase. */
static void radio_layer(int kind,float a,int y,int h,float t,uint16_t color,uint16_t accent){
 if(kind<0||a<.03f)return;
 const unsigned lift=(unsigned)radio_round(a*14);
 const uint16_t ink=md_tint(MD_BG,color,lift),warm=md_tint(MD_BG,accent,(unsigned)radio_round(a*18));
 if(kind==0){                                   /* fog: long, slow bands */
  for(unsigned b=0;b<4;b++){
   const float yy=y+h*(.3f+.16f*b)+radio_wf(t/3+b*17)*3/64;
   if(!radio_band(yy-4,yy+4))continue;
   float px=0,py=yy;
   for(int x=20;x<=240;x+=20){
    // each stretch of a band thickens and thins on its own: fog, not a rule
    const float thick=radio_wf(t/4+x/5+b*23)/64;
    const float ny=yy+radio_wf(t/2+x/8+b*9)*2/64;
    if(thick>-.2f)radio_line(px,py,x,ny,md_tint(MD_BG,color,(unsigned)radio_round(a*11*(.4f+.6f*thick))));
    px=x;py=ny;
   }
  }
 }else if(kind==1){                             /* water: low, wavering lines */
  for(unsigned b=0;b<3;b++){
   const float yy=y+h-12-b*7;
   float px=20,py=yy;
   for(int x=36;x<=220;x+=16){const float ny=yy+radio_wf(t+x/3+b*21)*2/64;radio_line(px,py,x,ny,b==0?warm:ink);px=x;py=ny;}
  }
 }else if(kind==2){                             /* ice: glints that come and go */
  for(unsigned j=0;j<9;j++){
   const float x=24+(j*61)%192,yy=y+14+(j*37)%(h-28);
   const float glint=radio_wf(t/(1+j%3)+j*13);
   if(glint>20){const float r=1+(glint-20)/22;radio_line(x-r,yy,x+r,yy,warm);radio_line(x,yy-r,x,yy+r,warm);}
  }
 }else if(kind==3){                             /* rain: fine, slanted streaks */
  for(unsigned j=0;j<12;j++){
   const float run=t*(3+j%3)+j*41;
   const float x=(float)((j*53)%236),yy=y+(run-(h-10)*(float)(unsigned)(run/(h-10)));
   radio_line(x,yy,x-2,yy+6,ink);
  }
 }else{                                         /* pollen (4) drift up and sway, embers (5) rise and flicker */
  const unsigned count=kind==4?12:10;
  for(unsigned j=0;j<count;j++){
   const float run=t*(kind==4?.6f:1.4f)*(1+j%3)+j*29;
   const float span=h-16,up=run-span*(float)(unsigned)(run/span);
   const float x=20+(j*47)%200+radio_wf(t/2+j*11)*(kind==4?6:3)/64,yy=y+h-8-up;
   const uint16_t c=kind==4?ink:md_tint(MD_BG,accent,clamp(radio_round(a*(10+radio_wf(t*2+j*7)/8)),0,32));
   radio_dot(x,yy,0,c);
  }
 }
}
static void radio_art(int y,int h,uint16_t color,uint16_t accent){
 if(-cv_oy>=y+h||-cv_oy+(int)cv_h<=y)return;
 const float phase=radio_phase();
 const unsigned part=radio_view.active?radio_view.section:0;
 const unsigned seed=radio_view.active?(unsigned)radio_view.biome*13+part*31:0;
 if(radio_view.active)radio_follow(seed,part);
 const float chapter=radio_view.active?radio_ease.chapterf:0;
 const float partf=radio_view.active?radio_ease.partf:0;
 const float cy=y+h/2.f;
 const int progress=radio_view.active?clamp(radio_view.progress,0,1000):0;
 const int swell=progress<500?progress:1000-progress;
 const float breath=dream_prefs.motion?swell/42.f+radio_view.energy/18.f:0;
 if(radio_view.active){
  // The sky's bands drift from one chapter's light to the next.
  // G-3: the whole dream is a day: dimmer at its start and end, clearest in
  // the middle, the accent's warmth gathering over its last third.
  const int c0=(int)chapter;const unsigned blend=(unsigned)radio_round((chapter-c0)*32);
  const float dream=dream_prefs.motion?radio_dream():.5f;
  const unsigned dim=(unsigned)radio_round(10*(1-radio_sine_half(dream)));
  const unsigned dusk=(unsigned)radio_round(6*radio_smooth((dream-.66f)/.34f));
  for(int band=-cv_oy/16;band<15&&band*16<-cv_oy+(int)cv_h;band++){
   uint16_t ink=radio_sky(band,c0,color,accent);
   if(blend)ink=md_tint(ink,radio_sky(band,c0+1,color,accent),blend);
   if(dusk)ink=md_tint(ink,accent,dusk);
   if(dim)ink=md_tint(ink,MD_BG,dim);
   cv_rect(0,band*16,240,16,ink);
  }
 }
 // G-6: a slow drift of the view; far layers move less (stars 1/4, the
 // family layer 1/2, the dream's motif fully).
 const int parallax=radio_view.active&&dream_prefs.motion;
 const float camx=parallax?radio_wf(phase/9)*6/64:0,camy=parallax?radio_wf(phase/13+16)*2/64:0;
 radio_ox=camx*.25f;radio_oy=camy*.25f;
 // A new constellation forms in each biome, then briefly catches the light.
 // G-2: its stars travel from the last Biome's places over 7 s.
 // G-3: more stars come out as the dream goes on (16 at its start, 30 at its
 // end), each new one brightening slowly.
 const float stars=radio_view.active?16.f+14.f*(dream_prefs.motion?radio_dream():.35f):13.f;
 const unsigned details=(unsigned)stars+1u;
 const float form=radio_view.active&&dream_prefs.motion?radio_smooth((flower_time-radio_ease.seed_at)/7000.f):1.f;
 for(unsigned j=0;j<details;j++){
  float x,dy,ox,ody;
  radio_star(j,seed,h,&x,&dy);
  if(form<1.f){radio_star(j,radio_ease.seed_was,h,&ox,&ody);x=ox+(x-ox)*form;dy=ody+(dy-ody)*form;}
  const unsigned point=j*47+radio_preset*19+seed;
  const float drift=phase/(1+j%3)+point%(64u*420u)+chapter*13;
  const float swing=radio_wf(drift);
  if(radio_view.active&&dream_prefs.motion){
   x+=swing*(5+chapter*2+j%5)*(1+.8f*radio_ease.level[j%6])/64; // G-5: louder tracks, wider stars
   dy+=radio_wf(drift+16)*(3+breath/3)/64;
   if((j&3u)==0&&swing>30){
    const float ray=2+(swing-30)/9;
    const uint16_t flare=md_tint(MD_BG,color,17+radio_round(chapter*3));
    radio_line(x-ray,y+dy,x+ray,y+dy,flare);
    radio_line(x,y+dy-ray/2,x,y+dy+ray/2,flare);
   }
  }
  const float shown=stars-j>1.f?1.f:stars-j;
  if(shown<=0)continue;
  const int glow=radio_round((13+(swing+64)/19+chapter)*shown);
  radio_dot(x,y+dy,j%6==0,md_tint(MD_BG,accent,glow));
 }
 if(radio_view.active){
  // G-5: the grain cloud's grains drift as small lights across the upper sky.
  for(unsigned g=0;g<4;g++)if(radio_ease.grain_win[g]){
   const float gx=20+radio_ease.grain_pos[g]*200/128.f,gy=y+h*.16f+g*9+radio_wf(phase/3+g*16)*3/64;
   const float life=radio_sine_half(radio_ease.grain_win[g]/128.f);
   const uint16_t c=md_tint(MD_BG,color,(unsigned)radio_round(6+18*life));
   radio_dot(gx,gy,1,c);radio_line(gx-5*life,gy,gx-1,gy,md_tint(MD_BG,color,(unsigned)radio_round(4+8*life)));
  }
 }
 radio_ox=camx*.5f;radio_oy=camy*.5f;
 if(radio_view.active){
  // G-4: the Biome family's layer, crossing over with the stars' 7 s.
  if(radio_ease.layer_was!=radio_ease.layer_now)radio_layer(radio_ease.layer_was,1.f-form,y,h,phase,color,accent);
  radio_layer(radio_ease.layer_now,radio_ease.layer_was!=radio_ease.layer_now?form:1.f,y,h,phase,color,accent);
 }
 radio_ox=camx;radio_oy=camy;
 const float *lv=radio_ease.level; // G-5: each track's loudness bends its part of the motif
 const uint16_t soft=md_tint(MD_BG,color,14),line=md_tint(MD_BG,color,25);
 if(radio_preset==2){
  // Nearby branches follow the wind. The woodland opens in the later chapters.
  for(int j=0;j<9;j++){
   const float x=42+j*19,base=y+h-8-(j%2)*5;
   const float size=25+(j*23+seed)%32+chapter*7+h/6;
   const float sway=radio_wf(phase+j*9)*(breath+chapter+6*lv[j%6])/64;
   radio_line(x,base,x+sway,base-size,soft);
   for(int b=1;b<=3;b++){
    const float root=x+sway*(6-b)/6,top=base-size+size*b/6;
    const float spread=4+b*(4+(j&1)-chapter/2);
    radio_line(root,top,x-spread,top+spread,line);
    radio_line(root,top,x+spread,top+spread,soft);
    if(radio_view.active&&((j+b)&1)==0){
     radio_line(x+spread/2,top+spread/2,x+spread/2+4,top+spread/2+6,line);
     const float twig=top+spread/2+radio_wf(phase+j*13+b*5)/23;
     radio_line(x-spread/2,twig,x-spread/2-4,twig-3,soft);
     radio_dot(x-spread/2-4,twig-3,0,color);
    }
   }
  }
  radio_line(30,y+h-7,210,y+h-7,soft);
  const float lantern=175-chapter*12+radio_wf(phase/2+seed%(64u*420u))/9;
  radio_circle(radio_round(lantern),y+18,4+radio_round(chapter),accent,0);
 }else if(radio_preset==4){
  // Dawn: the horizon first holds a sliver of the rising sun, then lets it go.
  const float horizon=cy+20,sun=cy+13-chapter*9-breath/2;
  if(radio_view.active)radio_circle(120,radio_round(sun),9+radio_round(chapter*3),md_tint(MD_BG,accent,24),1);
  for(int k=0;k<7;k++){
   const float x=34+k*29,yy=horizon+k%3*4;
   radio_line(x-14,yy,x+13,yy,soft);
  }
  for(int k=0;k<13;k++){
   const unsigned a=k*5;
   const float dx=radio_wave(a+16),dy=radio_wave(a);
   if(sun+dy*(25+breath)/64<horizon){
    radio_line(120+dx*16/64,sun+dy*16/64,120+dx*(25+breath)/64,sun+dy*(25+breath)/64,line);
   }
  }
  radio_orbit(120,sun,12,12,0,accent);
 }else if(radio_preset==5){
  // Drops move at different speeds and leave expanding rings on the river.
  const float water=y+h-17;
  const float travel=dream_prefs.motion?(float)(flower_time%3600000u)/36.f:0;
  for(unsigned j=0;j<16;j++){
   const float px=j*41+partf*19;
   const float x=43+(px-156*(int)(px/156));
   const unsigned span=h-22;
   const int speed=2+(j+radio_round(chapter))%4;
   const float run=travel*speed/4+j*17u;
   const float dy=run-span*(float)(unsigned)(run/span);
   const float trail=3+speed+breath/4;
   if(dy<(float)span-12)radio_line(x,y+dy,x-trail/3,y+dy+trail,j%4==0?color:line);
   else radio_orbit(x,water+j%4,4+dy-(span-12),2+j%2,0,j%3==0?accent:soft);
  }
  radio_line(34,water+8,206,water+8,soft);
 }else if(radio_preset==6){
  // Curtains sweep at separate speeds. Late chapters reveal their reflection.
  const float mirror=radio_smooth(chapter-1.f);
  for(int ribbon=0;ribbon<3;ribbon++){
   float px=30,py=cy+ribbon*11;
   for(int j=0;j<=22;j++){
    const float drift=phase/(ribbon+1)+j*3+ribbon*13+partf*5;
    const float x=30+j*8,ny=cy+ribbon*11+radio_wf(drift)*(11+breath+chapter*3+8*lv[ribbon*2])/64;
    radio_line(px,py,x,ny,ribbon==1?color:line);
    if(mirror>0&&ribbon!=0)radio_line(px,cy+74-(py-cy)/2,x,cy+74-(ny-cy)/2,md_tint(MD_BG,soft,radio_round(mirror*32)));
    if(j%2==0){
     const float curtain=ny-5-(radio_wf(phase+j*7)+64)*(5+chapter)/70;
     radio_line(x,ny,x-1,curtain,ribbon==1?line:soft);
    }
    px=x;py=ny;
   }
  }
 }else if(radio_preset==7){
  // Sleep: a crescent hangs above slowly expanding breath rings.
  const int moon=y+23;
  radio_circle(120,moon,10,accent,1);
  radio_circle(124,moon-3,10,MD_BG,1);
  for(unsigned j=0;j<4;j++){
   float drift=phase+j*16;drift-=64*(int)(drift/64);
   radio_orbit(120,y+h-23,15+drift*4/5,4+drift/8,0,
               md_tint(MD_BG,color,clamp(radio_round(24-drift/4),0,32)));
  }
 }else if(radio_preset==8){
  // Ridges at different depths move against one another in the wind.
  radio_circle(173,y+h/4,h/11,md_tint(MD_BG,accent,18+radio_round(chapter*6)),1);
  for(int j=0;j<6;j++){
   const float floor=y+h/2+j*h/14;
   if(!radio_band(floor-(h/12+j+5),floor+(h/12+j+5)+3))continue;
   float px=0,py=floor;
   const uint16_t ink=md_tint(MD_BG,j%2?accent:color,12+j*4);
   for(int x=0;x<240;x+=6){
    const float ny=floor+radio_wf(x/4+phase/(7-j)+j*9)*(h/12+j+5*lv[j])/64;
    radio_line(px,py,x,ny,ink);
    if(j%2)radio_line(px,py+3,x,ny+3,soft);
    px=x;py=ny;
   }
  }
 }else if(radio_preset==9){
  // Six growing arms with crystalline reflections behind the main flake.
  const float span=h/3+chapter*h/26+breath;
  radio_orbit(120,cy,span+11,span/2+9,(float)((int)(part%5)-2),soft);
  for(unsigned i=0;i<6;i++){
   const float a=i*64/6+phase/5;
   const float dx=radio_wf(a+16),dy=radio_wf(a);
   const float x=120+dx*span/64,yy=cy+dy*span/64;
   radio_line(120,cy,x,yy,line);
   radio_dot(x,yy,1,accent);radio_dot(x,yy+1,1,accent);
   for(int j=1;j<=3;j++){
    const float r=span*(j+1)/5;
    const float bx=120+dx*r/64,by=cy+dy*r/64;
    radio_line(bx,by,bx+(dx-dy)*r/220,by+(dy+dx)*r/220,color);
    radio_line(bx,by,bx+(dx+dy)*r/220,by+(dy-dx)*r/220,color);
   }
  }
 }else if(radio_preset==10){
  // Small lights have different flight paths, speeds and lengths of trail.
  for(unsigned j=0;j<18;j++){
   const float t=phase/(1+j%3)+j*19;
   const float cx=42+(j*67)%157,yy=y+11+(j*43)%(h-22);
   const float rx=7+j%17+breath/2,ry=4+j%13;
   if(!radio_band(yy-ry-3,yy+ry+3))continue;
   for(unsigned k=0;k<4;k++){
    const float a=t+64-k*3;
    const float x=cx+radio_wf(a+16)*rx/64;
    const float py=yy+radio_wf(2*a)*ry/64;
    const int glow=16+radio_round(chapter*4+(radio_wf(t)+64)/7)-(int)k*5;
    const uint16_t ink=md_tint(MD_BG,j%3?accent:color,clamp(glow,0,32));
    if(k==0)radio_circle(radio_round(x),radio_round(py),2,ink,1);
    else radio_dot(x,py,1,ink);
   }
  }
 }else if(radio_preset==11){
  // A colony grows under the canopy while its spores drift upward.
  for(unsigned j=0;j<9;j++){
   const float x=22+j*24,base=y+h-9-(j*29)%(h/4);
   const float tall=h/6+(j*17)%(h/3),wide=7+(j*11)%15;
   const float top=base-tall-chapter*2-radio_wf(phase+j*7)*breath/96;
   radio_line(x,base,x-2,top,soft);
   radio_line(x+2,base,x,top,line);
   radio_orbit(x-2,top,wide,3+wide/4,0,color);
   radio_orbit(x-2,top-2,wide-2,4+wide/4,1,soft);
   const float up=phase/(1<<(j&1))+j*13+seed%(64u*420u);
   const float drift=up-(h/2)*(float)(unsigned)(up/(h/2));
   const float spore=x-7+radio_wf(drift+j)*wide/90;
   radio_dot(spore,base-drift,1,accent);
  }
 }else if(radio_preset==12){
  // The clay bowls answer in rings. Each has a different decay and footprint.
  for(unsigned j=0;j<6;j++){
   const float x=44+j%3*76,top=cy+j/3*h/5;
   if(!radio_band(top-14,top+16+chapter+14))continue;
   float pulse=phase/(1<<(j&1))+j*11+seed%(64u*420u);pulse-=32*(int)(pulse/32);
   const float width=11+j%3+breath/2;
   radio_orbit(x,top,width,5,0,line);
   radio_orbit(x,top+7,width-3,4,0,soft);
   radio_orbit(x,top,width+pulse/2,5+pulse/3,0,md_tint(MD_BG,accent,clamp(radio_round(31-pulse/2),0,32)));
   radio_line(x,top+11,x,top+15+chapter,soft);
  }
 }else if(radio_preset==13){
  // Veils rise independently, their edges drawn into thin aerial ribbons.
  for(unsigned j=0;j<7;j++){
   const float t=phase/(1<<(j&1))+seed%(64u*420u)+j*11;
   const float x=42+j*26+radio_wf(t+16)*breath/65;
   const float dy=radio_wf(t)*h/275,top=cy-h/4+j%4*h/7+dy;
   const float spread=5+chapter*2+breath/3+j%3;
   radio_orbit(x,top,spread,2+spread/2,dy/4,j%2?soft:line);
   radio_line(x,top,x+dy,top-spread,color);
  }
 }else if(radio_preset==0){
  const float cx=120;float px=cx,py=y+h-7;
  const unsigned leaves=5+radio_round(chapter)/2;
  for(unsigned j=1;j<=leaves;j++){
   const float x=cx+radio_swayf(phase+j*5)/3,ny=y+h-7-(h-17)*(float)j/leaves;
   radio_line(px-1,py,x-1,ny,soft);
   radio_line(px,py,x,ny,line);
   if(j<leaves){
    const int sign=j%2?1:-1;
    const float reach=(float)(sign*(int)(14+((j*11+radio_round(breath))%19)+h/13)); /* (signed: j is unsigned) */
    const float tip=x+reach,ty=ny-12-radio_swayf(phase+j*8)/5;
    const float shoulder=x+reach*3/5;
    radio_line(x,ny+1,shoulder,ty,soft);
    radio_line(x,ny,x+reach/3,ty+3,color);radio_line(x+reach/3,ty+3,shoulder,ty-2,color);
    radio_line(shoulder,ty-2,tip,ty,accent);radio_line(tip,ty,shoulder,ny-1,line);
    radio_line(shoulder,ny-1,x+reach/3,ny+3,line);radio_line(x+reach/3,ny+3,x,ny,soft);
    radio_line(x,ny,tip,ty,soft);
    if(radio_view.active){
     for(int rib=1;rib<=3;rib++){
      const float bx=x+reach*rib/4,by=ny+(ty-ny)*rib/4;
      radio_line(bx,by,bx+reach/8,by-3,soft);
      radio_line(bx,by,bx+reach/8,by+3,line);
     }
     radio_circle(radio_round(tip),radio_round(ty),1+((part+j)&1u),accent,1);
    }
   }
   px=x;py=ny;
  }
  if(radio_view.active){
   const float reach=4+chapter+breath/3;
   for(unsigned a=0;a<64;a+=13){
    const float dx=radio_wf(a+phase/4+16)*reach/64,dy=radio_wf(a+phase/4)*reach/64;
    radio_orbit(px+dx,py+dy,reach/2,reach/3,dy/2,line);
   }
  }
  radio_circle(radio_round(px),radio_round(py),2,accent,1);
 }else if(radio_preset==1){
  for(unsigned ring=0;ring<3;ring++){
   const float rx=25+ring*15+breath+chapter*3+h/16+6*lv[ring*2],ry=h/6+ring*5;
   const float slant=(float)ring*2-2+radio_wf(phase/3+partf*7)/28;
   radio_orbit(120,cy,rx,ry,slant,ring==1?line:soft);
   const float a=phase/(ring+1)+ring*23;
   const float dx=radio_wf(a+16)*rx/64;
   radio_circle(radio_round(120+dx),radio_round(cy+radio_wf(a)*ry/64+dx*slant/8),ring==1?3:2,accent,1);
  }
  radio_circle(120,radio_round(cy),4,color,1);
 }else{
  // Sea: the visible shoal drifts over currents at different depths.
  for(unsigned row=0;row<5;row++){
   const float py=y+12+(h-24)*row/4.f;
   float px=33,last=py;
   const float depth=3+breath/2+chapter*2+h/40+4*lv[row];
   if(!radio_band(py-depth-6,py+depth+6))continue;
   const float flow=phase/(1<<(row&1))+row*11+seed%(64u*420u);
   for(unsigned j=1;j<=16;j++){
    const float nx=33+j*11;
    const float ny=py+radio_wf(flow+j*(4+row))*depth/64;
    radio_line(px,last,nx,ny,row==2?line:soft);px=nx;last=ny;
   }
   const float drift=dream_prefs.motion?(float)(flower_time%3600000u)/(47+row*9):0;
   const float run=drift+row*37+seed%145u;
   const float point=run-145*(float)(unsigned)(run/145);
   const float yy=py+radio_wf(flow+(point/11)*(4+row))*depth/64;
   const uint16_t ink=row==2?accent:line;
   radio_orbit(47+point,yy,8+row,3,radio_wf(flow+16)/19,ink);
   radio_line(40+point,yy,35+point,yy-3,soft);
   radio_line(40+point,yy,35+point,yy+3,soft);
   radio_dot(50+point+row,yy-1,0,accent);
   if(radio_view.active)radio_line(32+point,yy-1,25+point,yy+1,soft);
  }
 }
 if(radio_view.active&&dream_prefs.motion){
  // G-5: each track's note opens a small ring at its own place near the
  // ground, which widens and fades within 0.9 s.
  const uint32_t now=plat_ms();
  for(unsigned i=0;i<MD_TRACKS;i++){
   const uint32_t age=now-pulse_start[i];
   if(age>=900u)continue;
   const float t=age/900.f;
   radio_orbit(30+i*36,y+h-26-(i%2)*9,2+9*t,1+4*t,0,md_tint(MD_BG,i%2?accent:color,(unsigned)radio_round(20*(1-t))));
  }
 }
 radio_ox=radio_oy=0;
}
/* G-2: the title and the chapter's line come in over 0.6 s and leave over the
 * last 1.5 s of their 8 s, instead of appearing and vanishing at once. */
static unsigned radio_text_mix(uint32_t now){
 const uint32_t age=now-radio_activity;
 if(age>=8000u)return 0;
 if(!dream_prefs.motion)return 32;
 const float in=radio_smooth(age/600.f),out=radio_smooth((8000u-age)/1500.f);
 return (unsigned)radio_round(32*(in<out?in:out));
}
static void radio_draw(void){
 static const char *const durations[]={"30 MIN","1 HOUR","2 HOURS","4 HOURS"};
 static const char *const paces[]={"CALM","FLOW","VARIED"};
 static const char *const halls[]={"CLOSE","OPEN","VAST"};
 static const uint8_t palettes[MD_ITINERA_PRESETS]={10,8,0,7,6,4,9,3,4,8,9,10,6,2};
 static const char *const story[MD_ITINERA_PRESETS]={
  "Rain on seeds.\0Roots drink.\0A bud opens.\0Petals fold.",
  "Earth quiets.\0Moonward.\0Stars draw near.\0A light marks home.",
  "Into the wood.\0Branches close.\0Leaves stir.\0A path opens.",
  "The shore slips.\0Current turns.\0Breathe.\0Still water.",
  "Thin horizon.\0Light spills.\0Life wakes.\0Warmth lingers.",
  "A drop lands.\0Leaves answer.\0River rises.\0Rain drifts.",
  "Sky colours.\0Bright wind.\0Light folds.\0Blue on snow.",
  "The room quiets.\0Let the day go.\0Small moon.\0Sleep arrives.",
  "Dunes shift.\0A ridge dims.\0Tracks fade.\0Sand cools.",
  "Crystal grows.\0Ice catches light.\0Frost spreads.\0Edges melt.",
  "A light lifts.\0Another answers.\0Paths cross.\0Dark returns.",
  "Spores rise.\0Caps unfurl.\0The colony glows.\0Spores drift.",
  "Clay warms.\0Rings answer.\0The ground hums.\0Dust settles.",
  "Air gathers.\0A veil lifts.\0Clouds open.\0Sky lets go."
 };
 const uint16_t *pal=dream_palettes[palettes[radio_preset]];
 const char *name=md_itinera_name(radio_preset);
 if(radio_view.active){
  radio_art(0,240,pal[0],pal[2]);
  const unsigned mix=radio_text_mix(plat_ms());
  if(mix){
   const unsigned chapter=clamp(radio_view.chapter,0,3);
   radio_center(13,&FONT_B,name,md_tint(MD_BG,MD_INK,mix));
   const char *line=story[radio_preset];
   for(unsigned i=0;i<chapter;i++)while(*line++);
   radio_center(215,&FONT_XS,line,md_tint(MD_BG,pal[1],mix));
  }
  return;
 }
 radio_ease.valid=0;memset(radio_ease.level,0,sizeof radio_ease.level);memset(radio_ease.grain_win,0,sizeof radio_ease.grain_win);
 const unsigned settings=radio_settings();
 radio_center(7,&FONT_H,"ITINERA / DREAM RADIO",pal[0]);
 cv_rect(19,29,202,1,md_tint(MD_BG,pal[0],16));
 radio_center(36,&FONT_B,name,MD_INK);
 radio_center(63,&FONT_XS,"UNLIMITED DREAMS",pal[1]);
 radio_art(76,65,pal[0],pal[2]);
 md_named(8,150,"K1 / DREAM","SELECT",pal[0]);
 md_named(124,150,"K2 / LENGTH",durations[settings%4],pal[1]);
 md_named(8,186,"K3 / CHANGES",paces[settings/4%3],pal[2]);
 md_named(124,186,"K4 / ROOM",halls[settings/12],pal[3]);
 radio_center(225,&FONT_XS,md_playing()?"PLAY STOPS BIOME":"PLAY BEGINS / REC RESET",MD_DIM);
}
