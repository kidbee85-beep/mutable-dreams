/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
/* Small, deterministic vector engravings shared by Home and the Biome browser. */
enum { ART_FROND, ART_MEDUSA, ART_CURRENT, ART_WIRE, ART_SHELL, ART_CELLS, ART_CORAL };

static int atlas_visible(int y,int height){return y+cv_oy<(int)cv_h&&y+height+cv_oy>0;}

static uint16_t atlas_colour(uint32_t seed,int biome){
 return dream_palettes[(md_scene_dna(seed,biome)>>9)%11u][0];
}

static void atlas_text(int x,int y,int width,const felucca_font_t *font,const char *name,uint16_t colour){
 if(text_w(font,name)<=width){cv_text(x,y,font,name,colour);return;}
 char fitted[48];
 int n=0;
 while(n+4<(int)sizeof fitted&&name[n]){fitted[n]=name[n];n++;}
 for(;n>=0;n--){
  fitted[n]='.';fitted[n+1]='.';fitted[n+2]='.';fitted[n+3]=0;
  if(text_w(font,fitted)<=width){cv_text(x,y,font,fitted,colour);return;}
 }
}

static void atlas_title(int x,int y,int width,const char *name,uint16_t colour){
 if(text_w(&FONT_B,name)<=width){cv_text(x,y+7,&FONT_B,name,colour);return;}
 char line[48];
 int cut=0;
 for(int i=0;name[i]&&i<(int)sizeof line-1;i++){
  if(name[i]==' '){line[i]=0;if(text_w(&FONT_B,line)<=width)cut=i;}
  line[i]=name[i];
 }
 if(cut){
  line[cut]=0;cv_text(x,y,&FONT_B,line,colour);
  atlas_text(x,y+22,width,&FONT_B,name+cut+1,colour);
 }else atlas_text(x,y+7,width,&FONT_H,name,colour);
}

static void atlas_plate(int x,int y,int w,int h,uint16_t colour){
 if(!atlas_visible(y,h))return;
 uint16_t edge=md_tint(MD_BG,colour,12);
 for(int j=0;j<5;j++){
  const int a=y+j*h/5,b=y+(j+1)*h/5;
  cv_rect(x,a,w,b-a,md_tint(MD_BG,colour,2+(j<3?j:4-j)));
 }
 cv_line(x,y+4,x,y+h-5,edge);cv_line(x+w-1,y+4,x+w-1,y+h-5,edge);
 cv_line(x+4,y,x+w-5,y,edge);cv_line(x+4,y+h-1,x+w-5,y+h-1,edge);
 cv_line(x+3,y+1,x+1,y+3,edge);cv_line(x+w-4,y+1,x+w-2,y+3,edge);
 cv_line(x+1,y+h-4,x+3,y+h-2,edge);cv_line(x+w-2,y+h-4,x+w-4,y+h-2,edge);
}

/* Quiet background filaments underneath the artwork and controls. The seed
 * sets the shape, and each path carries a slow light. */
static void atlas_filaments(int x,int y,int width,int height,uint32_t seed){
 if(!atlas_visible(y,height))return;
 const uint16_t *palette=md_palette();
 const unsigned style=seed%5u;
 const unsigned time=dream_prefs.motion?field_time/63:0;
 for(int strand=0;strand<3;strand++){
  const uint16_t tint=palette[(strand+(seed>>4))&3];
  const uint16_t ink=md_tint(MD_BG,tint,9+strand+field_light/210);
  const int glint=(time/(2+strand)+(seed>>9)+strand*5)%12;
  int px=x,py=y;
  for(int point=0;point<=12;point++){
   const int offset=point-6;
   const int wave=md_sway(time+point*(3+style)+strand*11+(seed&63));
   int nx=x+point*(width-2)/12,ny=y+(strand+1)*height/4+wave/3;
   if(style==1)ny+=(offset*offset-18)/2;
   if(style==2){nx=x+(strand+1)*width/4+wave/3;ny=y+point*(height-2)/12;}
   if(style==3)ny+=(offset<0?-offset:offset)*2-6;
   if(style==4)ny+=md_sway(point*5+strand*11)/2;
   if(point)cv_line(px,py,nx,ny,ink);
   if(point==glint)cv_rect(nx,ny,2,1,md_tint(MD_BG,tint,21));
   px=nx;py=ny;
  }
 }
}

static unsigned atlas_kind(const md_biome_card *card){
 static const uint8_t family_marks[]={ART_MEDUSA,ART_FROND,ART_WIRE,ART_CURRENT,
  ART_CELLS,ART_FROND,ART_SHELL,ART_MEDUSA,ART_CELLS,ART_SHELL,ART_WIRE,
  ART_CURRENT,ART_SHELL,ART_MEDUSA,ART_CELLS,ART_FROND,ART_WIRE,ART_CORAL,ART_FROND};
 return family_marks[card->family<sizeof family_marks?card->family:8];
}

static void atlas_mark(int cx,int cy,int scale,unsigned kind,uint32_t seed,int energy,uint16_t colour){
 if(!atlas_visible(cy-scale-2,2*scale+4))return;
 const int variation=(int)(seed>>15&7)-3;
 const int rise=(energy+7)/12;
 const uint16_t dim=md_tint(MD_BG,colour,13),ink=md_tint(MD_BG,colour,25);
 const uint16_t light=md_tint(colour,MD_INK,7+energy/18);
 switch(kind){
 case ART_FROND:
  cv_line(cx-scale/9,cy+scale*5/6,cx+scale/9,cy-scale*5/6,dim);
  for(int i=0;i<6;i++){
   int y=cy+scale*(2-i)/4,x=cx+scale*(i-2)/24;
   int side=(i&1)?1:-1,width=scale*(14-i+variation)/18+rise;
   int ex=x+side*width,ey=y-scale/4;
   int knee=x+side*(width*2/3);
   cv_line(x,y,knee,ey-scale/10,ink);cv_line(knee,ey-scale/10,ex,ey,light);
   cv_line(ex,ey,knee,y+scale/10,ink);cv_line(knee,y+scale/10,x,y,dim);
   cv_line(x,y,ex,ey,dim);
   if(scale>22)cv_line(knee,y,knee+side*scale/9,ey,dim);
  }
  break;
 case ART_MEDUSA:
  for(int band=0;band<3;band++){
   int px=cx-scale,py=cy+band*3;
   for(int k=1;k<=12;k++){
    int t=k-6,nx=cx+t*scale/6;
    int ny=cy+band*3-(36-t*t)*(scale/2+band+rise)/36;
    cv_line(px,py,nx,ny,band==0?light:ink);px=nx;py=ny;
   }
  }
  for(int j=0;j<5;j++){
   int px=cx+(j-2)*scale/4,py=cy+6;
   for(int k=1;k<=6;k++){
    int nx=cx+(j-2)*scale/4+md_sway(seed%31+k*5+j*4)*k/32;
    int ny=cy+6+k*(scale+variation-5)/7;
    cv_line(px,py,nx,ny,j&1?ink:dim);px=nx;py=ny;
   }
   if(scale>=20)md_circle(px,py,1,light,1);
  }
  break;
 case ART_CURRENT:
  for(int line=0;line<6;line++){
   int y=cy+(line-2)*scale/4,px=cx-scale;
   int phase=(int)(seed>>line&15);
   int py=y+md_sway(phase)*scale/52;
   for(int k=1;k<=12;k++){
    int nx=cx-scale+2*k*scale/12;
    int ny=y+md_sway(k*3+phase)*scale/52-md_sway(k*2+line)*rise/12;
    cv_line(px,py,nx,ny,line%3==1?light:ink);px=nx;py=ny;
   }
  }
  break;
 case ART_WIRE:
  for(int j=0;j<3;j++){
   int x=cx+(j-1)*scale/2,y=cy+(j&1?scale/6:-scale/6);
   int half=scale/3+j*2,tilt=variation+j;
   cv_line(x-half,y+scale/2,x+tilt,y-scale*3/5,light);
   cv_line(x+tilt,y-scale*3/5,x+half,y+scale/2,ink);
   cv_line(x-half,y+scale/2,x+half,y+scale/2,ink);
   cv_line(x,y+scale/2,x+tilt,y-scale*3/5,dim);
   cv_line(x-half/2,y+scale/4,x+half/2,y+scale/4,dim);
   md_circle(x+tilt,y+scale/4-rise,1,light,1);
  }
  break;
 case ART_SHELL:
  for(int j=1;j<=5;j++){
   int radius=2+j*scale/6+(j&1?rise:0);
   int x=cx+(j-3)*variation/2,y=cy+(j-3)*scale/16;
   md_circle(x,y,radius,j==5?light:j&1?ink:dim,0);
   cv_line(x-radius,y,x+radius,y,dim);
  }
  md_circle(cx,cy,2+rise/2,light,1);
  break;
 case ART_CELLS:
  for(int j=0;j<6;j++){
   const int x=cx+((j%3)-1)*scale*5/9+(int)((seed>>(j*3))&7u)-3;
   const int y=cy+(j<3?-1:1)*scale/3+(j%3)*variation/2;
   int radius=scale/5+((seed>>(j+10))&3u)+rise/2;
   md_circle(x,y,radius,j&1?light:ink,0);
   md_circle(x+radius/3,y-radius/4,radius/3,dim,0);
   md_circle(x,y,1,light,1);
  }
  break;
 case ART_CORAL:
  for(int j=-2;j<=2;j++){
   const int tip_x=cx+j*scale/3,tip_y=cy-scale+(j*j)*scale/7-rise;
   const int elbow_x=cx+j*scale/6,elbow_y=cy+scale/5;
   cv_line(cx,cy+scale/2,elbow_x,elbow_y,dim);
   cv_line(elbow_x,elbow_y,tip_x,tip_y,ink);
   cv_line((tip_x+elbow_x)/2,(tip_y+elbow_y)/2,tip_x-scale/8,tip_y+scale/5,dim);
   md_circle(tip_x,tip_y,1+(scale>20),light,0);
  }
  break;
 }
}

static void atlas_brand_mark(int cy,int spread){
 if(!atlas_visible(cy-52,104))return;
 const uint16_t rim=md_tint(MD_BG,dream_colors[2],13);
 md_circle(120,cy,42,rim,0);
 md_circle(120,cy,46,md_tint(MD_BG,dream_colors[4],7),0);
 for(int petal=0;petal<6;petal++){
  static const int8_t dx[]={0,29,29,0,-29,-29},dy[]={-33,-17,17,33,17,-17};
  int x=120+dx[petal]*spread/100,y=cy+dy[petal]*spread/100;
  int ix=120+dx[petal]*spread/310,iy=cy+dy[petal]*spread/310;
  cv_line(ix,iy,120+dx[petal]*spread/154,cy+dy[petal]*spread/154,rim);
  atlas_mark(x,y,10,petal,dream_colors[petal]*17u,spread/5,dream_colors[petal]);
 }
 cv_line(120,cy-8,128,cy,MD_INK);cv_line(128,cy,120,cy+8,MD_INK);
 cv_line(120,cy+8,112,cy,MD_INK);cv_line(112,cy,120,cy-8,MD_INK);
 cv_pset(120,cy,MD_INK);
}
