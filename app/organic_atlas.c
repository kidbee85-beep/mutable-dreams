/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Cached living atlas: 480 subpixel vertices replace 2400 isolated RGB points.
 * No framebuffer, allocation or animation work in the audio callback. */
static void md_organic(void){
 int page=dream_ui.page,land=md_landscape(page),family=-1,scene=-2;
 uint32_t dna=page==LIVE?md_world_dna():0;
 if(page==LIVE){family=dna%10u;land=family%5;scene=(int)(0x40000000u|(dna&0x3fffffffu));}
 if(page==SOUND){scene=-100-md_state()->track[dream_ui.track].model;if(md_state()->track[dream_ui.track].model==MD_SKIN)land=2;}
 const uint16_t *palette=md_palette();
 if(organic_page!=page||organic_scene!=scene){
  organic_page=page;organic_scene=scene;
  for(int row=0;row<96;row++){
   float a=(row+1)*3.14159265f/97,u=(row-47.5f)/48.f;
   int width=(int)((land==0?76:land==1?57:land==2?45:land==4?61:65)*fm_sinf(a));
   int centre=(land==2?134:164)+(int)((land==0?18:land==3?24:7)*fm_sinf(u*2.8f+page*.3f));
   landscape_x[row]=clamp(centre-width,1,238);landscape_width[row]=clamp(width*2,0,239-landscape_x[row]);
  }
  for(int tone=0;tone<24;tone++)landscape_ink[tone]=md_tint(palette[0],palette[2],tone);
  for(int band=0;band<ORGANIC_FLOW_ROWS;band++)for(int x=0;x<240;x+=2){
   float f=family>=0?.010f+.003f*(family%5):land==0?.018f:land==1?.012f:land==2?.034f:.021f;
   organic_flow[band][x/2]=44+band*26+(int)(10*fm_sinf(x*f+band*.63f)+4*fm_cosf(x*.041f+page+(dna&15)*.17f));
  }
  for(int path=0;path<5;path++)for(int k=0;k<96;k++){
   float a=k*.065449847f,r=20+path*8,rx,ry;
   if(land==0){r+=3*fm_sinf(5*a+page*.21f);rx=r*1.20f;ry=r*.67f;}
   else if(land==1){r+=2*fm_sinf(3*a+path);rx=r*1.15f;ry=r*.72f;}
   else if(land==2){r+=4*fm_sinf(6*a+path*.23f);rx=r*.92f;ry=r*.92f;}
   else if(land==3){r+=6*fm_sinf(3*a+page*.17f);rx=r*.82f;ry=r*1.05f;}
   else{r+=3*fm_sinf(8*a);rx=r;ry=r*.75f;}
   float x=(land==2?132:166)+rx*fm_cosf(a),y=99+ry*fm_sinf(a);
   if(land==1)x+=ry*fm_sinf(a)*.38f;
   if(family>=0){
    float t=k/95.f,phase=((dna>>(path*4))&15u)*.19f;
    switch(family){
    case 0: /* Floating cells: a cluster, not five nested copies. */
     r=12+path*2+2*fm_sinf(3*a+phase);x=143+(path%3)*19+r*fm_cosf(a);y=78+(path/3)*31+r*fm_sinf(a);break;
    case 1: /* A bell and separate open medusa tendrils. */
     if(!path){x=164+29*fm_cosf(a);y=90+17*fm_sinf(a);}
     else{x=143+path*9+7*fm_sinf(t*7+phase)*t;y=99+t*(35+path*3);}break;
    case 2: /* Deep roots, narrowing into quiet branching ends. */
     x=165+(path?((path&1)?-1:1)*(8+path*8)*t*t:7*fm_sinf(t*5+phase));y=60+t*(87-path*5);break;
    case 3: /* Horizontal currents. */
     x=92+136*t;y=68+path*14+9*fm_sinf(t*6+phase)+3*fm_sinf(t*14+path);break;
    case 4: /* Reed beds: tall individual breathing stems. */
     x=128+path*17+8*fm_sinf(t*2.8f+phase)*t;y=145-82*t+3*fm_sinf(t*5+path);break;
    case 5: { /* Inclined orbital seeds. */
     float tilt=path*.34f+phase*.18f,c=fm_cosf(tilt),s=fm_sinf(tilt);
     rx=(19+path*8)*fm_cosf(a);ry=(12+path*4)*fm_sinf(a);
     x=166+rx*c-ry*s;y=99+rx*s+ry*c;break;}
    case 6: /* Skin-like rippling membranes. */
     r=17+path*8+3*fm_sinf(6*a+phase);x=165+r*fm_cosf(a);y=99+r*.83f*fm_sinf(a);break;
    case 7: { /* Fine mineral facets, using the same 96 cached vertices. */
     int corner=k/8;float u=(k%8)/8.f,b=corner*.523598776f,c=(corner+1)*.523598776f;
     float r0=(18+path*7)*(corner&1?.82f:1.f),r1=(18+path*7)*(corner&1?1.f:.82f);
     x=166+(1-u)*r0*fm_cosf(b)+u*r1*fm_cosf(c);y=99+((1-u)*r0*fm_sinf(b)+u*r1*fm_sinf(c))*.85f;break;}
    case 8: /* Mycelial threads, spreading gently sideways. */
     x=104+119*t;y=97+(path-2)*(5+10*t*t)+5*fm_sinf(9*t+phase)*t;break;
    default: /* Lanterns / seed pods. */
     x=166+(20+path*6)*fm_cosf(a)*(.8f+.2f*fm_sinf(a+phase));y=99+(23+path*7)*fm_sinf(a)+4*fm_cosf(3*a+phase);break;
    }
   }
   unsigned dx=(unsigned)((int)(x*16)-166*16),dy=(unsigned)((int)(y*16)-99*16);
   organic_path[path][k].x=(uint8_t)dx;organic_path[path][k].y=(uint8_t)dy;
   organic_path[path][k].high=(uint8_t)(((dx>>8)&15u)|((dy>>4)&240u));
  }
 }
 unsigned time=dream_prefs.motion?field_time:0;
 int sway=md_sway(time/210+page),scale=470+field_size*530/1000;
 // On Home the atlas lives beside the voice list, where its silhouette stays
 // visible. The six small voice markers remain independent status indicators.
 int origin_x=page==LIVE?51:166,origin_y=page==LIVE?95:99;
 // Fine pigment layers, with a bright rim, instead of a flat coloured wash.
 for(int row=0;row<96;row++){
  int y=99+(row-48)*scale/1000+sway/12;
  if(y+cv_oy<0||y+cv_oy>=(int)cv_h)continue;
  int width=landscape_width[row]*scale/1000,x=landscape_x[row]+(landscape_width[row]-width)/2;
  unsigned strength=3+field_light/190;
  cv_rect(x,y,width,1,md_tint(MD_BG,landscape_ink[row/4],strength));
  if(width>8){
   uint16_t rim=md_tint(MD_BG,palette[1],8+field_light/160);
   cv_pset(x+width-1,y,rim);
   if((row+(int)(time/130))%31<2)cv_rect(x+width-3,y,3,1,md_tint(MD_BG,palette[2],17));
   if(row%11==0)cv_rect(x+width/3,y,width/5,1,md_tint(MD_BG,palette[3],5));
  }
 }
 int count=2+field_density*3/1000;
 for(int path=0;path<count;path++){
  unsigned light=9+field_light/150+(path==1?3:0);
  uint16_t ink=md_tint(MD_BG,palette[(path+page)%4],light);
  int shift=md_sway(time/(160+path*19)+path*9)/3;
  int open=family==2||family==3||family==4||family==8||(family==1&&path>0);
  for(int k=0;k<(open?95:96);k++){
   int next=(k+1)%96;
   int y=origin_y*16+(contour_y(path,k)-99*16)*scale/1000+sway/2;
   int ny=origin_y*16+(contour_y(path,next)-99*16)*scale/1000+sway/2;
   // Most segments miss this 40-row tile. Cull before the x transforms.
   if((y/16+cv_oy<-2&&ny/16+cv_oy<-2)||(y/16+cv_oy>=(int)cv_h&&ny/16+cv_oy>=(int)cv_h))continue;
   int x=origin_x*16+(contour_x(path,k)-166*16)*scale/1000+shift;
   int nx=origin_x*16+(contour_x(path,next)-166*16)*scale/1000+shift;
   md_fine_line(x,y,nx,ny,ink);
  }
  // A small moving nucleus lives on each contour and follows the live patch.
  int at=(int)(time/(120+path*27)+path*17)%96;
  int x=origin_x*16+(contour_x(path,at)-166*16)*scale/1000+shift;
  int y=origin_y*16+(contour_y(path,at)-99*16)*scale/1000+sway/2;
  md_aa_point(x,y,md_tint(palette[path%4],MD_INK,10),28);
 }
 // Flowing filaments continue behind the glass controls; density opens space.
 for(int j=0;j<1+field_density*(ORGANIC_FLOW_ROWS-1)/1000;j++){
  int head=(int)(time%(26000+j*1700))*238/(26000+j*1700);
  for(int tail=0;tail<5+field_size/170;tail++){
   int x=(head-tail*2+238)%238,k=x/2;
   int y=organic_flow[j][k];
   if(y<42||y>221)continue;
   md_aa_point(x*16,y*16,md_tint(MD_BG,palette[(j+page)%4],20-tail),28);
  }
 }
 md_plankton(page,land,time);
}

/* A low-contrast, slowly moving layer for Home and the dense views that
 * have their own plots. The plots, controls and text paint over it. */
static void md_atlas_currents(int home,int land,unsigned phase){
 if(-cv_oy>=223||-cv_oy+(int)cv_h<=42)return;
 const uint16_t *palette=md_palette();
 for(int band=0;band<4;band++){
  const int base=54+band*43;
  const int amplitude=3+field_size/140;
  if(base+amplitude+12+cv_oy<0||base-amplitude-12+cv_oy>=(int)cv_h)continue;
  const uint16_t ink=md_tint(MD_BG,palette[band],7+(band&1)*3+field_light/200);
  int px=10,py=base;
  for(int x=10;x<=230;x+=11){
   int j=(x-10)/11,wave=md_sway(phase/(3+band)+j*(4+land*2)+band*9);
   int mid=j-10,skew=land==3?mid*mid/9:land==2?(mid<0?-mid:mid):land==4?mid/2:0;
   int y=base+wave*amplitude/16+skew;
   if(j)cv_line(px,py,x,y,ink);
   if(j==(int)((phase/(5+band)+band*6)%20))cv_rect(x,y,2,1,md_tint(MD_BG,palette[band],19));
   if(home&&(j==5||j==15)){
    int tip=y-5-wave/6;
    cv_line(x,y,x+7,tip,ink);cv_line(x+7,tip,x+12,tip+1,ink);
    cv_line(x+12,tip+1,x+5,y+2,ink);cv_line(x+5,y+2,x,y,ink);
   }
   px=x;py=y;
  }
 }
}
