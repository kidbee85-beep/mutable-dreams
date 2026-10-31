/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#include "dsp/dreams.h"
#include "fastmath.h"
enum { LIVE,SOUND,RHYTHM,GRAIN,MIX,SYSTEM,ENVELOPE,GROOVE,TAPE,HARMONY,FILTER,APPEARANCE,MIXER,BASSLINE,SPACE,MODULATION,PHRASE,MIDI,BANKS,SEQUENCE,STEP_DETAIL,PROGRESSION,CLOUD,KIT,KIT_PAD,KIT_SEND,KIT_SEQ,SEQ_FLOW,LFO_ROUTE,LFO_ROUTE2 };
static struct { int page,track,help,dirty,octave; uint32_t btn,keys,down[NB],held,frame_at,frame_wait; char msg[32]; uint32_t msg_start; } dream_ui;
static int dream_store_hold;static int bank_pending=-1,bank_allow_load;
static uint8_t keyboard_track[27],keyboard_note[27];
static volatile int dream_reloading;
static float md_audio[256],md_gain;static uint32_t midi_link;
typedef struct { uint32_t magic; int32_t motion,boot_sound,button_glow; uint32_t revision; uint8_t midi_route[6],midi_macro,reserved; } md_preferences;
static md_preferences dream_prefs;
static uint32_t intro_start,chime_left;static int intro_pending,intro_active;static float chime_phase[3];
static void dream_intro(void){intro_start=plat_ms();intro_active=1;chime_left=dream_prefs.boot_sound?88200:0;for(int i=0;i<3;i++)chime_phase[i]=0;}
static void dream_dismiss_intro(void){intro_active=chime_left=0;}
static uint32_t overloads,cpu_peak,flower_time,last_events[6],pulse_start[6],pulse_active,quality_notice;
static uint32_t ui_frames,ui_frame_peak_ms;
static const uint16_t dream_colors[6]={RGB(174,207,182),RGB(220,198,148),RGB(134,194,180),RGB(165,190,218),RGB(200,177,216),RGB(204,160,145)};
// Stable track identities, separate from each page's four control colours.
static const uint16_t dream_palettes[7][4]={
 {RGB(174,207,182),RGB(156,193,199),RGB(216,199,159),RGB(192,181,216)}, // moss / pearl
 {RGB(182,204,161),RGB(219,197,149),RGB(145,190,170),RGB(181,191,215)}, // reed / linen
 {RGB(139,199,194),RGB(158,190,220),RGB(186,176,215),RGB(204,213,182)}, // lagoon / mist
 {RGB(200,181,221),RGB(152,194,211),RGB(206,195,160),RGB(162,206,185)}, // dusk / iris
 {RGB(224,200,150),RGB(160,194,172),RGB(181,188,217),RGB(199,167,181)}, // honey / stone
 {RGB(167,195,214),RGB(187,205,181),RGB(199,184,210),RGB(216,205,170)}, // slate / moon
 {RGB(215,190,159),RGB(147,193,180),RGB(191,178,209),RGB(176,197,217)}  // parchment / jade
};
static const uint8_t dream_page_palette[30]={0,1,4,2,0,5,1,4,2,3,1,0,0,4,2,3,4,5,5,4,4,3,2,6,6,2,4,4,3,3};
static const uint16_t *md_palette(void){return dream_palettes[dream_page_palette[dream_ui.page]];}
static uint16_t md_control_ink(int x,int y,uint16_t fallback){return (y==158||y==194)&&(x==8||x==124)?md_palette()[(y==194?2:0)+(x==124)]:fallback;}
#define MD_BG RGB(4,8,13)
#define MD_CARD RGB(28,40,50)
#define MD_INK RGB(255,252,244)
#define MD_DIM RGB(226,236,238)
#define MD_EDGE RGB(116,143,149)
static const char *dream_pages[]={"LIVE BIOME","SOUND","RHYTHM","GRANULAR BUS","MIX / NOTES","QUALITY / SYSTEM","ENVELOPE / BLOOM","GROOVE / REPEATS","TAPE / MOTION","HARMONY / GLOBAL","VELVET FILTER","LOOK / STARTUP","GLOBAL MIXER","BASS / MOTIFS","SHARED SPACE","MARBLES / TIDES / WARPS","PHRASE / NOTES","MIDI / PERFORMANCE","USER BANKS","SEQUENCE / 32 STEPS","STEP / EXPRESSION","CHORD JOURNEY","CLOUD / AIR","LO-FI / RACK MODE","KIT / PAD COLOUR","KIT / SENDS","KIT / 32 STEPS","SEQUENCE / FLOW","TIDAL / LFO A","TIDAL / LFO B"};
static const char *dream_headings[]={"MUTABLE DREAMS","VOICE SHAPING","RHYTHM","GRANULAR CLOUD","NOTES & SEND","PERFORMANCE","BLOOM & DECAY","GROOVE","TAPE MOTION","HARMONY","VELVET FILTER","LIVING DISPLAY","MIXER","BASS & MOTIFS","SHARED SPACE","MODULATION","PHRASE","MIDI CONTROL","USER LIBRARY","YOUR SEQUENCE","STEP EXPRESSION","CHORD JOURNEY","AIR & TEXTURE","LO-FI RACK","PAD COLOUR","KIT SENDS","KIT SEQUENCE","MELODIC FLOW","TIDAL MOTION A","TIDAL MOTION B"};
static const char *help_title[NB]={"GRANULAR BUS","MUTE A TRACK","RETURN","FREEZE","CHOOSE A VOICE","SYSTEM","LIVE MACROS","SAVE","LOCK THE SEED","GENERATE RHYTHMS","PLAY / STOP","MUTATE","LOWER OCTAVE","HIGHER OCTAVE"};
static const char *help_lines[NB][5]={
 {"ONE SHARED BUS / ALL TRACKS","K1 Mix / K2 Grain length","K3 Position / K4 Feedback","Track sends: MIXER, K2","FX again: Tape / Space / Air"},
 {"SEL: mute / unmute this track","SELECT: choose an active track","ALGO: change sound engine","Every engine fits every track.","EDIT + PRESET: sound library"},
 {"ENV: undo the last variation","REC: make a new variation","One previous variation is kept.","Choosing a Biome starts fresh.","SAVE: store the current patch"},
 {"LFO: freeze the granular memory","The cloud keeps playing.","Tracks remain live and playable.","LFO again: resume recording","FX: edit the cloud and tape"},
 {"EDIT: Sound/Filter/Env/Mix/Mixer","PRESET: 352 sound presets","ALGO: fifteen sound engines","Rhythm and notes are preserved.","HOME + PRESET: Biome path"},
 {"GLO: Harmony/System/Look/MIDI/Banks","Harmony: root / scale / offset","System: tempo / quality","Look: motion / sound / LEDs","SAVE stores these preferences."},
 {"K1 DENSITY: number of events","K2 MOTION: melodic movement","K3 COLOUR: brighter / darker","K4 SPACE: granular level","PRESET: 156 Biomes + Blank"},
 {"Stop PLAY; use internal clock.","SAVE stores Biome and preferences.","GLO > BANKS: 16 Biomes / 64 tones","Factory Biomes stay available.","Granular audio is not stored."},
 {"ARP: lock the random seed","The same seed repeats.","REC: create a new variation","ENV: undo the last variation","Unequal lengths keep cycling."},
 {"SEQ: Rhythm > Groove > Bass","Rhythm: hits / length / rotation","K4: event probability","Groove: swing / repeats / accent","K4: individual clock division"},
 {"PLAY: start / stop","Stop lets the sound decay.","SYSTEM + REC: silence all","Keyboard: play the selected track","Start restarts the sequence."},
 {"REC: vary seed and rhythms","ENV: undo the last variation","SYSTEM / MIDI: REC means PANIC.","PANIC also clears the tape.","Holding opens help only."},
 {"OCT-: keyboard one octave down","Sequence notes stay unchanged.","MIX K2: the track base note","The first hardware key is F3.","HOME: return to live controls"},
 {"OCT+: keyboard one octave up","Sequence notes stay unchanged.","MIX K2: the track base note","HOME + PRESET: whole Biome","EDIT + PRESET: single sound"}
};
static int md_context_help(void){
 int p=dream_ui.page,b=dream_ui.help;
 return (p==MIXER&&b==B_EDIT)||((p==BASSLINE||p==PHRASE)&&b==B_SEQ)||
        ((p==SPACE||p==MODULATION||(p==LFO_ROUTE||p==LFO_ROUTE2))&&b==B_FX)||
        ((p==MIDI||p==BANKS)&&b==B_GLO)||(p==BANKS&&b==B_SAVE)||((p==SEQUENCE||p==STEP_DETAIL||p==SEQ_FLOW)&&(b==B_SEQ||b==B_REC))||(p==PROGRESSION&&b==B_GLO)||(p==CLOUD&&b==B_FX);
}
static int dream_cmd(int c,int t,int v){if(c==MD_MIDI_ROUTE)v=clamp(v,0,16);if(c==MD_MIDI_MACRO)v=clamp(v,0,1);uint32_t f=plat_lock();int r=md_command(c,t,v);plat_unlock(f);if(r&&c==MD_MIDI_ROUTE&&t>=0&&t<6)dream_prefs.midi_route[t]=v;if(r&&c==MD_MIDI_MACRO)dream_prefs.midi_macro=v;return r;}
static int dream_install(const void *,unsigned);
static int dream_install_edit(const void *,unsigned);
#include "banks.c"
static void md_num(int v,char *s) {char b[12];int i=0;if(v<0){*s++='-';v=-v;}do{b[i++]='0'+v%10;v/=10;}while(v);while(i)*s++=b[--i];*s=0;}
static void md_note_label(int note,char *s){
 static const char *names[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
 const char *n=names[note%12];while(*n)*s++=*n++;
 md_num(note/12-1,s);while(*s)s++;*s++=' ';*s++='/';*s++=' ';md_num(note,s);
}
static const felucca_font_t *md_label_font(int x,int y,const char *s){
 int width=234-x;
 if(y==25)return &FONT_XS; // Preserve the compact header's two-line rhythm.
 if(y>=224)return dream_ui.page!=LIVE&&text_w(&FONT_H,s)<=230-x?&FONT_H:&FONT_XS;
 if(y==160||y==196)width=96;
 if(dream_ui.page==LIVE&&y==141)width=x<140?124:94;
 if(dream_ui.page==LIVE&&x==98)width=12;
 // Grow short useful labels, never force long names outside their region.
 return text_w(&FONT_H,s)<=width?&FONT_H:&FONT_XS;
}
static void md_text(int x,int y,const char *s,uint16_t c) {
 const felucca_font_t *font=md_label_font(x,y,s);
 if(y>=224&&font==&FONT_H)y=224; // A full 16-pixel footer fits the display.
 // Skip labels outside this tile; the optional one-pixel glyph shadow also
 // participates in clipping. Spaces stay transparent over the living field.
 if(y+cv_oy>=cv_y1||y+cv_oy+(int)font->h+1<=cv_y0)return;
 if(y>=40&&y<155&&dream_ui.help<0)cv_text(x+1,y+1,font,s,MD_BG);
 cv_text(x,y,font,s,c);
}
static void md_circle(int x,int y,int r,uint16_t c,int fill) {
 // Reject rows outside the active tile before walking the circle's pixels.
 // Pixel output is identical; large moving circles no longer run six times.
 int a0=-r,a1=r,b0=-r,b1=r;
 if(a0< -x)a0=-x;if(a1>=(int)cv_w-x)a1=(int)cv_w-x-1;
 if(b0< -y-cv_oy)b0=-y-cv_oy;if(b1>=(int)cv_h-y-cv_oy)b1=(int)cv_h-y-cv_oy-1;
 for(int b=b0;b<=b1;b++)for(int a=a0;a<=a1;a++){int q=a*a+b*b;if(q<=r*r&&(fill||q>=(r-1)*(r-1)))cv_pset(x+a,y+b,c);}
}
// Subpixel coverage for our botanical marks. Audio never calls this renderer.
static void md_soft_point(float x,float y,uint16_t ink){
 if(x<0||y<0||x>=240||y>=240)return;
 int ix=(int)x,iy=(int)y;float fx=x-ix,fy=y-iy;
 for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++){
  int px=ix+dx,py=iy+dy+cv_oy;if(px<0||px>=(int)cv_w||py<0||py>=(int)cv_h)continue;
  float a=(dx?fx:1-fx)*(dy?fy:1-fy);uint16_t old=swap16(cv_px[py*cv_w+px]);
  int red=(int)((old>>11)*(1-a)+(ink>>11)*a+.5f);
  int green=(int)(((old>>5)&63)*(1-a)+((ink>>5)&63)*a+.5f);
  int blue=(int)((old&31)*(1-a)+(ink&31)*a+.5f);
  cv_px[py*cv_w+px]=swap16((red<<11)|(green<<5)|blue);
 }
}
static float bloom_x[6][72],bloom_y[6][72],bloom_cx[6],bloom_cy[6];
static uint32_t bloom_frame=0xffffffffu;
// A small control-rate view of the actual patch. No random activity meter:
// sparse settings leave negative space; the selected page supplies the meaning.
static int field_density=300,field_size=400,field_light=400,field_motion=200;
static uint32_t field_time,field_last_ms;
static void md_field_update(void){
 const md_patch *p=md_state();const md_track *t=&p->track[dream_ui.track];
 const md_extension *e=&p->ext[dream_ui.track];const md_sequence *q=&p->sequence[dream_ui.track];
 int d=p->macro[0],s=(p->macro[0]+p->macro[3])/2,l=p->macro[2],m=p->macro[1];
 switch(dream_ui.page){
 case SOUND:d=(t->tone+t->harm)/2;s=t->level;l=t->tone;m=t->breathe;break;
 case RHYTHM:case GROOVE:d=t->hits*1000/t->length;s=t->prob;l=t->accent;m=(t->ratchet-1)*333;break;
 case GRAIN:d=p->macro[3];s=p->grain_size;l=p->feedback;m=p->grain_pos;break;
 case TAPE:d=p->macro[3];s=p->grain_size;l=p->duck_amount;m=(p->grain_pitch<0?-p->grain_pitch:p->grain_pitch)*1000/24;break;
 case CLOUD:d=(p->grain_noise+p->macro[3])/2;s=p->grain_size;l=p->grain_noise;m=p->freeze?0:p->macro[1];break;
 case MIX:case MIXER:d=(t->send+e->delay_send+e->reverb_send)/3;s=t->level;l=t->level;m=t->breathe;break;
 case ENVELOPE:d=t->level;s=t->decay;l=t->attack;m=t->breathe;break;
 case FILTER:d=t->cutoff;s=t->cutoff;l=t->resonance;m=t->breathe;break;
 case SPACE:d=p->fx_level;s=p->reverb_time;l=p->delay_fb;m=(p->delay_ms-40)*1000/310;break;
 case LFO_ROUTE2:d=p->motion.depth2[dream_ui.track];s=d;l=p->motion.wave2[dream_ui.track]*250;m=1000-p->motion.rate2[dream_ui.track];break;
 case LFO_ROUTE:case MODULATION:d=(e->marbles+e->tides+e->warps)/3;s=e->tides;l=e->warps;m=1000-e->tides_rate;break;
 case BASSLINE:case PHRASE:{int notes=0;for(int i=0;i<16;i++)notes+=e->notes[i]>=0;d=e->bass_mode?notes*1000/16:0;s=e->glide;l=t->accent;m=e->marbles;break;}
 case SEQ_FLOW:case SEQUENCE:case STEP_DETAIL:{int notes=0;for(int i=0;i<q->length;i++)notes+=q->steps[i].note!=0;d=q->enabled?notes*1000/q->length:0;s=q->length*1000/32;l=q->steps[q->edit].velocity*10;m=q->steps[q->edit].chance*10;break;}
 case PROGRESSION:d=p->chord_pattern?600:60;s=p->chord_bars*1000/16;l=400;m=p->chord_pattern?1000/p->chord_bars:0;break;
 case KIT:case KIT_PAD:case KIT_SEND:case KIT_SEQ:{const md_kit *k=&p->kit;unsigned hits=0;for(int n=0;n<k->length;n++)hits+=(k->gates[k->pad]>>n)&1u;d=k->enabled?hits*1000/k->length:60;s=k->pads[k->pad].decay;l=k->pads[k->pad].cutoff;m=k->hiss*10;break;}
 case SYSTEM:case APPEARANCE:case MIDI:case BANKS:d=120;s=260;l=250;m=100;break;
 default:break;
 }
 int target[4]={clamp(d,0,1000),clamp(s,0,1000),clamp(l,0,1000),clamp(m,0,1000)};
 int *values[4]={&field_density,&field_size,&field_light,&field_motion};
 for(int i=0;i<4;i++){int delta=target[i]-*values[i];*values[i]+=dream_prefs.motion?((delta>-8&&delta<8)?delta:delta/5):delta;}
 uint32_t now=plat_ms(),elapsed=field_last_ms?now-field_last_ms:0;field_last_ms=now;if(elapsed>200)elapsed=200;
 if(dream_prefs.motion)field_time+=elapsed*(180+field_motion)/1000;
 bloom_frame=0xffffffffu;
}
static void md_bloom_cache(void){
 if(bloom_frame==flower_time)return;bloom_frame=flower_time;
 for(int j=0;j<md_active_tracks();j++){
  float a=j*(6.2831853f/md_active_tracks())-.52f+flower_time*.000025f;
  int orbit=16+field_density*8/1000;
  bloom_cx[j]=51+fm_cosf(a)*orbit;bloom_cy[j]=95+fm_sinf(a)*orbit;
  float energy=clamp((int)(md_status()->track_peak[j]*60),0,4);
  for(int k=0;k<72;k++){float b=k*.0872664626f,r=10+field_size*.006f+3*fm_sinf(3*b+a)+2*fm_cosf(b+a)+energy;
   bloom_x[j][k]=bloom_cx[j]+fm_cosf(b)*r;bloom_y[j][k]=bloom_cy[j]+fm_sinf(b)*r;
  }
 }
}
// RGB565 translucency over the current 40-line tile: no second framebuffer.
static uint16_t md_tint(uint16_t a,uint16_t b,unsigned mix){
 unsigned rb=(((a&0xf81fu)*(32-mix)+(b&0xf81fu)*mix)>>5)&0xf81fu;
 unsigned g=(((a&0x07e0u)*(32-mix)+(b&0x07e0u)*mix)>>5)&0x07e0u;
 return (uint16_t)(rb|g);
}
static void md_glass_leaf(int x,int y,uint16_t c){
 static const uint8_t edge[8]={12,8,6,4,3,2,1,1};
 for(int row=0;row<32;row++){
  int py=y+row+cv_oy;if(py<0||py>=(int)cv_h)continue;
  int left=row<8?edge[row]:row>28?row-28:0;
  int right=row>23?edge[31-row]:row<3?3-row:0;
  int a=x+left,z=x+107-right;if(a<0)a=0;if(z>=(int)cv_w)z=cv_w-1;
  for(int px=a;px<=z;px++){
   uint16_t old=swap16(cv_px[py*cv_w+px]);
   cv_px[py*cv_w+px]=swap16(md_tint(old,md_tint(MD_CARD,c,2+row/16),11));
  }
  if(a<=z){cv_pset(a,y+row,md_tint(MD_EDGE,c,11));cv_pset(z,y+row,md_tint(MD_EDGE,c,11));}
 }
 // Asymmetric leaf tips and a restrained colour vein leave the field visible.
 cv_rect(x+12,y,92,1,md_tint(MD_EDGE,c,6));cv_rect(x+4,y+31,92,1,md_tint(MD_EDGE,c,6));
 for(int j=0;j<17;j++)cv_pset(x+13+j,y,md_tint(MD_EDGE,c,12));
}
static void md_leaf_slider(int x,int y,int width,int value,int lo,int hi,uint16_t c){
 int fill=clamp(value-lo,0,hi-lo)*width/(hi>lo?hi-lo:1);
 for(int j=0;j<=width;j++){
  int bend=j*(width-j)*3/(width*width/4+1);
  cv_pset(x+j,y+bend,j<=fill?md_tint(c,MD_INK,(unsigned)(j*8/width)):MD_EDGE);
  if(j<=fill)cv_pset(x+j,y+bend+1,md_tint(MD_BG,c,13));
  if(j&&j%14==0){cv_pset(x+j-1,y+bend-1,j<=fill?c:MD_EDGE);cv_pset(x+j-2,y+bend-2,j<=fill?c:MD_EDGE);}
 }
 int bend=fill*(width-fill)*3/(width*width/4+1);
 md_circle(x+fill,y+bend,3,c,1);cv_pset(x+fill,y+bend,MD_INK);
}
static void md_value_span(int x,int y,const char *label,int value,int lo,int hi,uint16_t c) {
 char s[12];c=md_control_ink(x,y,c);md_glass_leaf(x,y,c);md_text(x+6,y+2,label,MD_DIM);md_num(value,s);
 cv_text(x+7,y+17,&FONT_B,s,MD_BG);cv_text(x+6,y+16,&FONT_B,s,MD_INK);
 int start=text_w(&FONT_B,s)+12;if(start<45)start=45;if(start>78)start=78;
 md_leaf_slider(x+start,y+24,100-start,value,lo,hi,c);
}
static void md_value(int x,int y,const char *label,int value,uint16_t c){md_value_span(x,y,label,value,0,100,c);}
static void md_named(int x,int y,const char *label,const char *value,uint16_t c){c=md_control_ink(x,y,c);md_glass_leaf(x,y,c);md_text(x+6,y+2,label,MD_DIM);const felucca_font_t *font=text_w(&FONT_B,value)>96?&FONT_XS:&FONT_B;cv_text(x+7,y+17,font,value,MD_BG);cv_text(x+6,y+16,font,value,md_tint(c,MD_INK,6));}
static void md_mixer_leaf(int x,int level,uint16_t c){
 // A slim leaf reservoir. Height still represents the exact configured level.
 for(int row=0;row<75;row++){
  int inset=row<12?(12-row)*(12-row)/16:row>58?(row-58)*(row-58)/25:0;
  if(inset>11)inset=11;
  uint16_t ink=row>=75-level?c:MD_CARD;
  cv_rect(x+inset,55+row,24-2*inset,1,ink);
 }
 cv_rect(x+11,64,1,57,md_tint(MD_CARD,c,9));
}

// A compact cached one-shot A/D contour. MIDI gate hold/release is separate.
static uint8_t env_plot[180];static int env_attack=-1,env_decay=-1;
static void md_envelope_plot(const md_track *t,uint16_t col){
 if(env_attack!=t->attack||env_decay!=t->decay){env_attack=t->attack;env_decay=t->decay;
  float a=.002f+8.f*t->attack*t->attack/1000000.f,d=.06f+16.f*t->decay*t->decay/1000000.f,total=a+3*d;
  for(int j=0;j<180;j++){float time=total*j/179.f,z=(time-a)/d;float level=time<a?time/a:1.f/(1+z+.48f*z*z+.235f*z*z*z);env_plot[j]=(uint8_t)(132-43*level);}
 }
 for(int j=0;j<180;j++){int y=env_plot[j];if(y+cv_oy<-3||y+cv_oy>=40)continue;
  uint16_t vein=md_tint(MD_BG,col,8);cv_rect(30+j,y+1,1,3,vein);md_soft_point(30+j,(float)y,col);
  if(j&&env_plot[j]!=env_plot[j-1])cv_line(29+j,env_plot[j-1],30+j,y,col);
 }
 md_circle(30,132,2,col,0);md_circle(209,env_plot[179],2,col,0);
}

static uint8_t tidal_plot[180];static int tidal_shape=-1,tidal_depth=-1;
static void md_tidal_plot(int shape,int depth,uint16_t col){
 if(shape!=tidal_shape||depth!=tidal_depth){tidal_shape=shape;tidal_depth=depth;
  for(int j=0;j<180;j++){float phase=j/179.f,w=1-4*(phase>.5f?phase-.5f:.5f-phase);
   if(shape==1||shape==2){w=-fm_cosf(phase*6.2831853f);if(shape==2){float u=(w+1)*.5f;w=2*u*u-1;}}
   else if(shape==3){float u=phase<.25f?phase*4:(1-phase)*(4.f/3.f);w=2*u*u*(3-2*u)-1;}
   tidal_plot[j]=(uint8_t)(111-w*depth*.021f);
  }
 }
 for(int j=0;j<180;j++)md_soft_point(30+j,tidal_plot[j],col);
}

// Cached organic contours: generate only when changing page, never in audio.
// Native display coordinates fit in bytes: 4,800 bytes less UI RAM.
static struct {uint8_t x,y;uint16_t c;} organic_points[2400];
static int16_t organic_flow[7][120];
static unsigned organic_count;static int organic_page=-1;
// Scanline silhouettes fit in 384 bytes, not an extra framebuffer.
static uint8_t landscape_x[96],landscape_width[96];static uint16_t landscape_ink[96];
static int md_landscape(int page){
 if(page==KIT_SEQ)return 2;
 if(page==KIT_SEND)return 0;
 if(page==KIT||page==KIT_PAD)return 4;
 if(page==GRAIN||page==TAPE||page==SPACE||page==CLOUD)return 0;
 if(page==HARMONY||page==PROGRESSION||page==MODULATION||page==LFO_ROUTE||page==LFO_ROUTE2||page==MIDI)return 1;
 if(page==RHYTHM||page==GROOVE||page==BASSLINE||page==PHRASE||page==SEQUENCE||page==STEP_DETAIL||page==SEQ_FLOW)return 2;
 return 3;
}
static void md_seed_point(int x,int y,uint16_t ink){
 if(x>=0&&x<240&&y>=40&&y<222&&organic_count<2400){organic_points[organic_count].x=x;organic_points[organic_count].y=y;organic_points[organic_count++].c=ink;}
}
// Tiny plankton: bounded integer geometry, no texture, heap or extra buffer.
// Draw behind labels. Quiet settings leave only two small creatures visible.
static int md_sway(unsigned phase){int p=phase%64;return (p<32?p:64-p)-16;}
static float md_sway_soft(unsigned time,unsigned interval,unsigned offset){
 unsigned tick=time/interval+offset;
 float a=(float)md_sway(tick),b=(float)md_sway(tick+1);
 return a+(b-a)*(float)(time%interval)/(float)interval;
}
static void md_microbe(int cx,int cy,int size,int kind,unsigned phase,uint16_t ink){
 static const int8_t ring[24][2]={{16,0},{15,4},{14,8},{11,11},{8,14},{4,15},{0,16},{-4,15},{-8,14},{-11,11},{-14,8},{-15,4},{-16,0},{-15,-4},{-14,-8},{-11,-11},{-8,-14},{-4,-15},{0,-16},{4,-15},{8,-14},{11,-11},{14,-8},{15,-4}};
 if(cy+size+18+cv_oy<0||cy-size-3+cv_oy>=(int)cv_h)return;
 int breath=md_sway(phase)/8,rx=size+(kind==2?5:0),ry=size+breath;
 uint16_t membrane=md_tint(MD_BG,ink,15+field_light/250);
 uint16_t core=md_tint(MD_BG,ink,24),soft=md_tint(MD_BG,ink,8);
 int first=kind==0?12:0;
 for(int j=first;j<24;j++){
  int n=(j+1)%24,ruffle=kind==1&&j%4==0?1:0;
  int x=cx+ring[j][0]*(rx+ruffle)/16,y=cy+ring[j][1]*(ry+ruffle)/16;
  int nx=cx+ring[n][0]*rx/16,ny=cy+ring[n][1]*ry/16;
  cv_line(x,y,nx,ny,membrane);
  if(kind==1&&j%4==0)cv_line(x,y,cx+ring[j][0]*(rx+3)/16,cy+ring[j][1]*(ry+3)/16,soft);
 }
 if(kind==0){ // A gently pulsing bell with three trailing tentacles.
  cv_line(cx-rx,cy,cx,cy+2,membrane);cv_line(cx,cy+2,cx+rx,cy,membrane);
  for(int arm=-1;arm<=1;arm++){
   int x=cx+arm*rx/2,y=cy+2;
   for(int k=1;k<=4;k++){int nx=cx+arm*rx/2+md_sway(phase+k*9+arm*7)/5,ny=cy+2+k*3;
    cv_line(x,y,nx,ny,k<3?membrane:soft);x=nx;y=ny;}
  }
  md_circle(cx,cy-ry/3,2,core,0);
 }else{
  int nucleus=md_sway(phase+15)/9;
  md_circle(cx+nucleus,cy,2,core,1);cv_pset(cx-3,cy-2,membrane);cv_pset(cx+4,cy+2,membrane);
  if(kind==2){ // A capsule and one flexible flagellum.
   int x=cx+rx,y=cy;
   for(int k=1;k<=4;k++){int nx=cx+rx+k*3,ny=cy+md_sway(phase+k*11)/5;cv_line(x,y,nx,ny,soft);x=nx;y=ny;}
  }
 }
}
static void md_plankton(int page,int land,unsigned time){
 // Distinct little habitats for the page families. Very quiet settings keep
 // one creature; brighter/denser patches invite a few more into the scene.
 static const uint8_t habitat[5][3][2]={
  {{24,77},{204,132},{72,145}},  // water / granular
  {{25,112},{204,69},{196,151}}, // orbit / harmony
  {{24,140},{204,74},{79,59}},   // rhythm / steps
  {{207,72},{25,151},{73,79}},   // leaves / voices
  {{24,73},{204,138},{79,145}}   // kit / seeds
 };
 int count=1+(field_density>160)+(field_density>730),phase=time/85;
 for(int j=0;j<count;j++){
  int cx=habitat[land][j][0],cy=habitat[land][j][1];
  cx+=md_sway(phase/2+j*21+page*3)/3;
  cy+=md_sway(phase+j*17+page*5)/4;
  int kind=(land==0)?0:(land==2||land==4)?2:1;
  if(j==1)kind=(kind+1)%3;
  md_microbe(cx,cy,5+field_size/400,kind,phase+j*19,md_palette()[(j+page)%4]);
  // A bounded, integer-only bioluminescent trail. It follows the same live
  // controls as the scene and stays behind every label and control card.
  int spores=(field_light>350?2:0)+(field_density>700?2:0);
  uint16_t halo=md_tint(MD_BG,md_palette()[(j+page+1)%4],13+field_light/125);
  for(int k=0;k<spores;k++){
   float x=cx+md_sway_soft(time,85,k*13+j*7)*.5f;
   float y=cy+10+k*5+md_sway_soft(time,170,k*17)*.2f;
   md_soft_point(x,y,halo);
   if(k==0)md_soft_point(x+1.2f,y+1.1f,md_tint(MD_BG,halo,15));
  }
 }
}
static void md_organic(void){
 int page=dream_ui.page,land=md_landscape(page);
 const uint16_t *palette=md_palette();
 // Deep clear field; only local silhouettes/rims add colour, no full-screen wash.
 if(organic_page!=page){organic_page=page;organic_count=0;
  uint16_t ink=md_tint(MD_BG,palette[0],18);
  // Four recognisable silhouettes: tidal veil, eclipsed moon, seed lantern,
  // and a broad leaf. All expensive geometry is cached on page transitions.
  for(int row=0;row<96;row++){
   float u=(row-47.5f)/48.0f,curve=fm_sinf((row+1)*3.14159265f/97);
   int width=(int)((land==0?86:land==1?58:land==2?52:land==4?65:70)*curve);
   int centre=(land==2?120:160)+(int)((land==0?17:land==3?23:8)*fm_sinf(u*2.8f+page*.3f));
   if(land==3)width=(int)(width*(.72f+.25f*fm_sinf(row*.065f)));
   landscape_x[row]=clamp(centre-width,1,238);landscape_width[row]=clamp(width*2,0,239-landscape_x[row]);
   // A low-contrast colour gradient makes the silhouette recognisable at
   // arm's length. Its final brightness still follows the live light value.
   landscape_ink[row]=md_tint(MD_BG,md_tint(ink,palette[2],7+row/6),18+row/32);
  }
  uint16_t leaf=md_tint(ink,palette[1],8);
  for(int band=0;band<7;band++)for(int x=0;x<240;x+=2){
   float frequency=land==0?.018f:land==1?.012f:land==2?.038f:.021f;
   int y=42+band*27+(int)(12*fm_sinf(x*frequency+band*.63f)+5*fm_cosf(x*.041f+page));
   organic_flow[band][x/2]=y;md_seed_point(x,y,ink);
  }
  for(int petal=0;petal<5;petal++)for(int k=0;k<96;k++){
   float a=k*.065449847f;float radius=22+petal*8+7*fm_sinf((3+page%5)*a+page*.4f);
   if(land==0)radius=22+petal*10+2*fm_sinf(5*a+page);
   if(land==1)radius=18+petal*11+3*fm_sinf(2*a+page);
   if(land==2)radius=23+petal*8+9*fm_sinf(4*a+page*.2f);
   if(land==4)radius=14+petal*10+3*fm_sinf(8*a+page*.2f);
   int x=land==2?128:178,y=land==0?103:94;
   x+=(int)(radius*fm_cosf(a));y+=(int)(radius*fm_sinf(a)*(land==0?.48f:land==1?.72f:land==4?.7f:1.0f));
   md_seed_point(x,y,ink);
  }
  // A different pressed fern on each page; cached once, below all labels/cards.
  if(land==3){int side=page%2?1:-1,origin=side>0?9:230;
  for(int stem=0;stem<90;stem++)md_seed_point(origin+side*stem/5,207-stem,leaf);
  for(int sprig=0;sprig<8;sprig++)for(int sideleaf=-1;sideleaf<=1;sideleaf+=2)for(int k=0;k<40;k++){
   float u=k/39.0f;int width=7+(7-sprig)*2;
   int x=origin+side*(sprig*2+(int)(u*width))+sideleaf*(int)(5*fm_sinf(u*3.14159265f));
   int y=194-sprig*10-(int)(u*(12+sprig));md_seed_point(x,y,leaf);md_seed_point(x+side,y,leaf);
  }}else{
   // Constellations, water reeds and rhythm seeds; prepared only on a page change.
   for(int j=0;j<9;j++){
    int x=14+(j*47+page*11)%208,y=55+(j*23+page*7)%93;
    for(int k=0;k<18;k++){
     float a=k*.34906585f;int rad=land==2?3+j%3:2+j%2;
     md_seed_point(x+(int)(rad*fm_cosf(a)),y+(int)(rad*fm_sinf(a)),leaf);
    }
    if(land==1&&j<8)for(int k=0;k<22;k++)md_seed_point(x+k,y+k/3,ink);
    if(land==0)for(int k=0;k<20;k++)md_seed_point(x+k/6,170-k,leaf);
   }
  }
 }
 // Solid translucent-looking forms make the page families readable at a
 // glance. A two-pixel breath is integer-only, subordinate to the foreground.
 uint32_t time=dream_prefs.motion?field_time:0;
 int phase=(time/160)%64,drift=(phase<32?phase:64-phase)/8-2;
 int scale=420+field_size*580/1000,ink_strength=10+field_light*10/1000;
 for(int row=0;row<96;row++){
  int y=97+(row-48)*scale/1000+drift;if(y+cv_oy<0||y+cv_oy>=40)continue;
  int width=landscape_width[row]*scale/1000,x=landscape_x[row]+(landscape_width[row]-width)/2;
  uint16_t body=md_tint(MD_BG,landscape_ink[row],ink_strength);
  cv_rect(x,y,width,1,body);
  // Sparse luminous rims; no glow buffer or per-pixel blur.
  if(row>5&&row<90&&width>8){
   uint16_t accent=palette[1];
   int edge=x+(land==1?0:width-1);
   cv_rect(edge-1,y,3,1,md_tint(body,accent,4+field_light*3/1000));
   if((row+phase/4)%24<4+field_density/250)cv_pset(edge,y,md_tint(body,accent,12+field_light/100));
  }
  if(row%12==0&&land==0)cv_rect(x+2,y,width>4?width-4:0,1,md_tint(body,dream_colors[5],3));
  if(land==1){int cut=width*2/5;cv_rect(x,y,cut,1,md_tint(MD_BG,body,7));}
  // Sparse "gills" catch the page palette inside each living shape. They
  // add detail without a second buffer, antialias pass or text occlusion.
  if(row>12&&row<84&&row%9==0&&width>24){
   int inset=5+((row/9+page)%4)*3;
   int glint_x=land==0||land==3?x+inset:x+width-inset-5;
   cv_rect(glint_x,y,4+field_density/350,1,
           md_tint(body,palette[(row/9+page)%4],8+field_light/250));
  }
 }
 // Sparse drifting pollen; transparent glyphs stay above the moving field.
 for(int k=0;k<2+field_density*13/1000;k++){int x=4+(k*61+page*17)%232,y=43+(k*29+(int)(time/(130+k*11)))%110;
  unsigned phase=(time/(80+k*9)+k*11)%48,light=phase<24?phase:48-phase;
  uint16_t ink=md_tint(MD_BG,palette[(k+page)%4],5+light/3+field_light/200);
  cv_pset(x,y,ink);cv_pset(x+1,y+1,ink);if(k%3==0){cv_pset(x-1,y+1,ink);cv_pset(x,y+2,ink);}}
 // Reject other display tiles before evaluating the density mask. The same
 // cached field is visited for six tiles; this preserves every visible pixel.
 for(unsigned k=0;k<organic_count;k++)if(organic_points[k].y+cv_oy>=0&&organic_points[k].y+cv_oy<40&&(k*37)%1000<(unsigned)(100+field_density*8/10))cv_pset(organic_points[k].x,organic_points[k].y,md_tint(MD_BG,organic_points[k].c,ink_strength));
 // Seven flowing shoots, including behind translucent controls. Interpolate
 // cached contours; a soft head and fading trail need no per-frame trigonometry.
 for(unsigned j=0;j<(unsigned)(1+field_density*6/1000);j++){
  float pos=(time%(26000+j*1700))*(238.0f/(26000+j*1700));
  for(int tail=0;tail<4+field_size/100;tail++){
   float x=pos-tail*1.6f;if(x<0)x+=238;int k=(int)(x*.5f);if(k>118)k=118;
   float f=x*.5f-k,y=organic_flow[j][k]+(organic_flow[j][k+1]-organic_flow[j][k])*f;
   if(y<41||y>221||y+cv_oy<-2||y+cv_oy>=40)continue;
   uint16_t ink=md_tint(MD_BG,palette[(j+page)%4],(unsigned)(20+field_light/100-tail));
   md_soft_point(x,y,ink);
   if(tail==0){md_soft_point(x-1,y-1,ink);md_soft_point(x+1,y+1,ink);md_soft_point(x-2,y+1,ink);}
  }
 }
 md_plankton(page,land,time);
}
static void md_draw_all(void) {
 const md_patch *p=md_state();const md_track *t=&p->track[dream_ui.track];uint16_t col=md_palette()[0];char s[24];int i;
 md_organic();
 cv_rect(0,0,240,39,MD_BG);
 for(int j=0;j<4;j++)cv_rect(10+j*55,38,55,1,md_tint(MD_EDGE,md_palette()[j],7));
 cv_rect(10,38,28,1,col);
 cv_rect(0,224,240,16,MD_BG);
 cv_text(10,5,&FONT_B,intro_active?"MUTABLE DREAMS":dream_headings[dream_ui.page],MD_INK);md_circle(226,12,3,md_playing()?dream_colors[2]:MD_DIM,1);
 md_text(10,25,dream_pages[dream_ui.page],md_tint(col,MD_INK,8));md_num(p->tempo,s);md_text(186,25,s,MD_INK);md_text(211,25,"BPM",MD_DIM);
 if(intro_active&&(uint32_t)(plat_ms()-intro_start)<3200&&dream_ui.help<0){
  float progress=(plat_ms()-intro_start)/3200.0f;
  for(int ring=0;ring<3;ring++)for(int k=0;k<84;k++){float a=k*.074799825f;float r=48+ring*13+4*fm_sinf(3*a+progress*6);cv_pset(120+(int)(fm_cosf(a)*r),104+(int)(fm_sinf(a)*r),MD_CARD);}
  for(int j=0;j<6;j++){float a=j*1.04719755f+progress*.9f;int x=120+(int)(fm_cosf(a)*(22+10*progress)),y=104+(int)(fm_sinf(a)*(22+10*progress));md_circle(x,y,24,dream_colors[j],0);md_circle(x,y,4,dream_colors[j],1);}
  md_circle(120,104,7,MD_INK,0);cv_text(29,174,&FONT_B,"LET YOUR BIOME BLOOM",MD_INK);md_text(41,198,"156 BIOMES / 352 TONES",MD_DIM);
  cv_rect(32,220,(int)(176*progress),2,col);md_text(48,223,"ANY CONTROL TO BEGIN",MD_DIM);return;
 }
 if(md_pending_biome()>=0||bank_pending>=0){
  cv_rect(8,45,224,175,MD_CARD);cv_text(18,57,&FONT_B,"KEEP YOUR CHANGES?",MD_INK);
  md_text(18,88,"YOUR BIOME HAS UNSAVED EDITS",col);
  md_text(18,112,bank_pending>=0?"Load selected user Biome?":md_biome_name(md_pending_biome()),MD_INK);
  md_text(18,145,"HOME: keep editing",MD_INK);md_text(18,165,"SAVE: open User Banks",MD_INK);
  md_text(18,185,"SEL: discard and load",RGB(231,174,154));
  md_text(18,207,"Playback continues until you choose.",MD_DIM);return;
 }
 if(dream_ui.help>=0) {
  // A quiet rounded guide card, with the living page visible at the edges.
  for(int row=0;row<178;row++){int edge=row<8?8-row:row>169?row-169:0;cv_rect(6+edge,43+row,228-edge*2,1,MD_CARD);}
  cv_text(17,48,&FONT_T,"A LITTLE GUIDANCE",MD_DIM);
  md_microbe(214,54,5,1,dream_prefs.motion?field_time/85:0,col);
  for(int seed=0;seed<5;seed++){
   int sway=dream_prefs.motion?md_sway(field_time/110+seed*9)/9:0;
   md_circle(218+sway,108+seed*19,seed==2?2:1,md_tint(MD_CARD,col,12+seed*3),1);
  }
  int contextual=md_context_help()||(dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)||
   (dream_ui.help==B_EDIT&&(dream_ui.page==SOUND||dream_ui.page==MIX||dream_ui.page==FILTER||dream_ui.page==ENVELOPE))||
   (dream_ui.help==B_GLO&&(dream_ui.page==HARMONY||dream_ui.page==SYSTEM||dream_ui.page==APPEARANCE))||
   (dream_ui.help==B_SEQ&&dream_ui.page==GROOVE)||
   (dream_ui.help==B_FX&&dream_ui.page==TAPE);
  const char *title=contextual?dream_pages[dream_ui.page]:help_title[dream_ui.help];
  cv_text(16,64,text_w(&FONT_B,title)>207?&FONT_XS:&FONT_B,title,col);
  cv_rect(16,84,208,1,MD_EDGE);
  const char *const *lines=help_lines[dream_ui.help];
  static const char *soundhelp[]={"PRESET: sound / ALGO: engine","K1: Tone / brightness","K2: Harmonics / chord","K3: Shape; drums use Decay","K4: Level / EDIT: Filter"};
  static const char *elementhelp[]={"PRESET: sound / ALGO: engine","K1 strike colour / K2 geometry","K3 Mallet / Plectrum / Particles","ENVELOPE: resonator decay","K4 level / EDIT: filter"};
  static const char *mixhelp[]={"K1: send to granular bus","K2: track base note","K3: pan left / right","K4: clock division, 1 to 8","PRESET: sound / SEL: mute"};
  static const char *lookhelp[]={"K1: animation on / off","K2: startup sound on / off","K3: replay startup animation","K4: button LEDs off / dim / soft","Stop + SAVE: keep preferences"};
  static const char *systemhelp[]={"K1: Tempo / K2: ECO or FULL","K3: ducking recovery","K4: reset measured CPU peak","REC: panic / clear tape","GLO: animation and startup"};
  static const char *harmonyhelp[]={"K1: root note C to B","K2: one of thirteen scales","K3: transpose +/-24 semitones","K4: tempo for all tracks","GLO: continue to System"};
  static const char *filterhelp[]={"K1: cutoff, 60 to 18000 Hz","K2: resonance at the cutoff","K3: OFF / LP12 / LP24 / LPG","K4: slow filter movement","LPG darkens with the decay"};
  static const char *envhelp[]={"K1: attack up to 8 seconds","K2: decay up to 16 seconds","FM / VA / Harmonic / Choir","K3: level / K4: grain send","Drums and Rings: engine decay"};
  static const char *groovehelp[]={"K1: global swing (0-60)","K2: 1-4 repeats per hit","K3: accent / K4: clock division","Repeats divide one step.","SEQ: return to hits / length"};
  static const char *tapehelp[]={"K1: grain pitch +/-24","K2: forward / reverse","K3: duck source; 0 means off","K4: attenuate other tracks","The source stays unchanged."};
  if(dream_ui.help==B_EDIT&&dream_ui.page==SOUND)lines=t->model==MD_ELEMENTS?elementhelp:soundhelp;
  if(dream_ui.help==B_EDIT&&dream_ui.page==MIX)lines=mixhelp;
  if(dream_ui.help==B_GLO&&dream_ui.page==APPEARANCE)lines=lookhelp;
  if(dream_ui.help==B_GLO&&dream_ui.page==SYSTEM)lines=systemhelp;
  if(dream_ui.help==B_GLO&&dream_ui.page==HARMONY)lines=harmonyhelp;
  if(dream_ui.help==B_EDIT&&dream_ui.page==FILTER)lines=filterhelp;
  if(dream_ui.help==B_EDIT&&dream_ui.page==ENVELOPE)lines=envhelp;
  if(dream_ui.help==B_SEQ&&dream_ui.page==GROOVE)lines=groovehelp;
  if(dream_ui.help==B_FX&&dream_ui.page==TAPE)lines=tapehelp;
  static const char *newhelp[11][5]={
   {"SELECT: track / SEL: mute","K1 level / K2 grain send","K3 delay send / K4 reverb send","FX: SPACE sets effect return","Sends follow track level."},
   {"K1 OFF / EUCLID / STEP","K2 motif / K3 glide / K4 note","SEQ again: edit phrase notes","Marbles varies offbeat notes.","All tracks remain assignable."},
   {"K1 delay time: 40-350 ms","K2 echo feedback / K3 room tail","K4 shared effect return","Mixer: individual track sends","PANIC clears all effect tails."},
   {"K1 Marbles: remembered variation","K2 Tides: modulation depth","K3 Tides cycle / K4 Warps blend","Marbles: EUCLID/STEP phrase mode","FX again: choose target and wave."},
   {"K1 step 1-16 / K2 semitones","-1 is a rest; scale still applies","K3 accent / K4 probability","Bass page: enable this phrase","STEP mode ignores Euclidean gates."},
   {"K1 clock / K2 input scale snap","K3 selected track: Off / CH 1-16","K4 CH16 CC16-19 macro shortcut","Save preferences with SAVE.","Tracks may share a MIDI channel."},
   {"K1: slot / K2: Biome or Tone","K3 load / K4 name cursor","SAVE: save; repeat to overwrite","Stop first; use internal clock","PRESET: edit name character"},
   {"K1 enable / K2 select step","K3 note (23=rest) / K4 length","REC: STEP entry > LIVE > OFF","Play keys or MIDI CH 1-6","RAM only; stop + SAVE to keep"},
   {"K1 velocity / K2 gate length","K3 chance for this step","K4: sparse / arp / pulse / drift","Generator replaces this track","ENV undo / REC arms note entry"},
   {"K1 four-chord progression","K2 bars per chord (1-8)","K3 selected track follows","K4 tonic / HARMONY sets scale","Slow chords; no extra voices"},
   {"K1 noise into the SHARED tape","K2 SHARED granular return","K3 SELECTED track grain send","K4 freeze the SHARED memory","Noise and sends stop in freeze"}};
  if(md_context_help())lines=newhelp[dream_ui.page-MIXER];
  static const char *kithelp[]={"KIT mode: 3 synths + 1 kit","K1 rack / K2 kit / K3 pad","Keyboard: eight pads repeat","EDIT: tune, level, filter, tail","SEQ: pad steps / REC: preview"};
  static const char *padhelp[]={"K1 pitch, -12 to +12 semitones","K2 pad level / K3 soft filter","K4 sample envelope tail","EDIT again: granular send","Each pad has independent settings."};
  static const char *sendhelp[]={"K1 pad / K2 pad granular send","MIXER T4 sets the kit bus send","K3 kit delay / K4 kit reverb","One shared tape, no new buffer","Freeze holds the shared tape."};
  static const char *seqhelp[]={"K1 step / K2 hit on or off","K3 length / K4 rhythm generator","PRESET pad / REC step-live-off","Eight patterns can play together","SAVE stores the kit in your Biome"};
  if((dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ))lines=dream_ui.page==KIT?kithelp:dream_ui.page==KIT_PAD?padhelp:dream_ui.page==KIT_SEND?sendhelp:seqhelp;
  static const char *flowhelp[]={"K1: forward/reverse/pendulum/orbit/seed","K2: pattern offset; notes stay stored","K3: gentle velocity variation","K4: generate a new phrase; ENV undo","REC records into the sounding step."};
  static const char *tidalhelp[]={"Two slow LFOs for this synth track","K1 target / K2 curve / K3 depth","K4 cycle: 0.5 to 20 seconds","Grain/FX/Warps need their base sends.","Soft Swell never boosts the peak."};
  if(dream_ui.page==SEQ_FLOW)lines=flowhelp;
  if((dream_ui.page==LFO_ROUTE||dream_ui.page==LFO_ROUTE2))lines=tidalhelp;
  for(i=0;i<5;i++){
   const felucca_font_t *font=text_w(&FONT_H,lines[i])<=204?&FONT_H:&FONT_XS;
   cv_text(23,91+i*22,font,lines[i],MD_INK);
   md_soft_point(14,97+i*22,col);md_soft_point(15,96+i*22,col);md_soft_point(16,97+i*22,col);
  }
  cv_text(17,208,&FONT_T,"RELEASE, THEN TAP TO RETURN",MD_DIM);return;
 }
 if((dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)){
  const md_kit *k=&p->kit;const md_pad *a=&k->pads[k->pad];
  md_text(12,48,dream_ui.page==KIT_SEQ&&md_record_mode()?(md_record_mode()==1?"REC STEP / PLAY THE PADS":"REC LIVE / OVERDUB PADS"):k->enabled?"3 SYNTH VOICES + ONE LO-FI KIT":"SIX SYNTH VOICES / KIT OFF",col);
  md_text(12,68,md_kit_name(k->bank),MD_INK);md_text(12,87,md_pad_name(k->pad),MD_DIM);
  if(dream_ui.page==KIT_SEQ){for(i=0;i<32;i++){int x=16+(i%16)*14,y=112+(i/16)*24;int on=(k->gates[k->pad]>>i)&1;md_circle(x,y,on?4:2,i==k->step?MD_INK:i<k->length?col:MD_EDGE,on);if(i==k->step)cv_rect(x-5,y+7,10,1,MD_INK);}
   md_value_span(8,158,"K1 STEP",k->step+1,1,k->length,col);md_named(124,158,"K2 HIT",(k->gates[k->pad]>>k->step)&1?"ON":"OFF",col);md_value_span(8,194,"K3 LENGTH",k->length,1,32,col);static const char *g[]={"DUST","SHUFFLE","SPARSE","TEXTURE"};md_named(124,194,"K4 GENERATE",g[k->generator],col);
  }else{
   for(i=0;i<8;i++){int x=24+i*27,y=123+(i%2)*8;md_circle(x,y,i==k->pad?9:5,i==k->pad?MD_INK:col,0);if(i==k->pad)md_circle(x,y,3,col,1);}
   if(dream_ui.page==KIT){md_named(8,158,"K1 RACK",k->enabled?"3 SYNTH + KIT":"6 SYNTH",col);md_value_span(124,158,"K2 KIT",k->bank+1,1,5,col);md_value_span(8,194,"K3 PAD",k->pad+1,1,8,col);md_value(124,194,"K4 TAPE HISS",k->hiss,col);}
   else if(dream_ui.page==KIT_PAD){md_value_span(8,158,"K1 PITCH",a->tune,-12,12,col);md_value(124,158,"K2 PAD LEVEL",a->level/10,col);md_value(8,194,"K3 SOFT FILTER",a->cutoff/10,col);md_value(124,194,"K4 TAIL",a->decay/10,col);}
   else{md_value_span(8,158,"K1 PAD",k->pad+1,1,8,col);md_value(124,158,"K2 PAD GRAIN",a->send/10,col);md_value(8,194,"K3 KIT DELAY",p->ext[3].delay_send/10,col);md_value(124,194,"K4 KIT REVERB",p->ext[3].reverb_send/10,col);}
  }
 }else if(dream_ui.page>=MIXER){
  const md_extension *e=&p->ext[dream_ui.track];
  if(dream_ui.page==SEQ_FLOW){
   const md_sequence *q=&p->sequence[dream_ui.track];const md_motion *m=&p->motion;
   char label[]="TRACK 1 / SAME NOTES, NEW PATH";label[6]='1'+dream_ui.track;md_text(12,51,label,MD_INK);
   md_text(12,75,md_direction_name(m->direction[dream_ui.track]),col);
   for(int j=0;j<16;j++){unsigned at=md_sequence_position(dream_ui.track,j);int x=15+j*14,y=124-(int)at*32/(q->length>1?q->length-1:1);md_circle(x,y,2,col,q->steps[at].note!=0);if(j){unsigned before=md_sequence_position(dream_ui.track,j-1);cv_line(x-14,124-(int)before*32/(q->length>1?q->length-1:1),x,y,md_tint(MD_BG,col,10));}}
   md_text(12,139,"SEQ AGAIN: RHYTHM / HOLD: HELP",MD_DIM);
   md_named(8,158,"K1 DIRECTION",md_direction_name(m->direction[dream_ui.track]),col);md_value_span(124,158,"K2 OFFSET",m->shift[dream_ui.track],0,31,col);
   md_value(8,194,"K3 FEEL",m->feel[dream_ui.track],col);md_named(124,194,"K4 GENERATE",md_generator_name(q->generator),col);
  }else if(dream_ui.page==LFO_ROUTE||dream_ui.page==LFO_ROUTE2){
   int tr=dream_ui.track,second=dream_ui.page==LFO_ROUTE2,target=second?p->motion.target2[tr]:p->motion.target[tr],shape=second?p->motion.wave2[tr]:p->motion.wave[tr],depth=second?p->motion.depth2[tr]:e->tides,rate=second?p->motion.rate2[tr]:e->tides_rate;
   char label[]="TRACK 1 / TWO INDEPENDENT LFOS";label[6]='1'+tr;md_text(12,51,p->kit.enabled&&tr==3?"SELECT A SYNTH TRACK / 1 TO 3":label,MD_INK);
   md_text(12,75,md_lfo_target_name(target),col);md_tidal_plot(shape,depth,col);
   md_text(12,139,second?"FX AGAIN: CLOUD / HOLD: HELP":"FX AGAIN: LFO B / HOLD: HELP",MD_DIM);
   md_named(8,158,"K1 TARGET",md_lfo_target_name(target),col);md_named(124,158,"K2 CURVE",md_lfo_wave_name(shape),col);
   md_value(8,194,"K3 DEPTH",depth/10,col);md_value_span(124,194,"K4 CYCLE ms",500+rate*195/10,500,20000,col);
  }else if(dream_ui.page==SEQUENCE||dream_ui.page==STEP_DETAIL){
   const md_sequence *q=&p->sequence[dream_ui.track];const md_step *st=&q->steps[q->edit];
   md_text(12,45,md_record_mode()?(md_record_track()==dream_ui.track?(md_record_mode()==1?"REC STEP / PLAY A KEY":"REC LIVE / OVERDUB"):"RECORDING ANOTHER TRACK"):"REC: STEP > LIVE > OFF",MD_INK);
   unsigned playing_step=md_sequence_position(dream_ui.track,(md_status()->step?md_status()->step-1:0)/t->speed);
   // Pitch forms a growing stem; the little bar beneath it shows velocity.
   // Integer-only layout adds no framebuffer or audio-rate work.
   int low=96,high=24;for(i=0;i<q->length;i++)if(q->steps[i].note){if(q->steps[i].note<low)low=q->steps[i].note;if(q->steps[i].note>high)high=q->steps[i].note;}
   if(low>high){low=48;high=60;}else if(high-low<12){low-=(12-(high-low))/2;high=low+12;}
   cv_text(12,62,&FONT_T,"HEIGHT: NOTE / BAR: VELOCITY",MD_DIM);
   for(i=0;i<32;i++){int x=15+(i%16)*14,base=97+(i/16)*33;int on=i<q->length&&q->steps[i].note;
    int y=on?base-5-(q->steps[i].note-low)*18/(high-low):base-3;
    uint16_t ink=i==q->edit?MD_INK:i<q->length?col:MD_EDGE;
    if(on){cv_rect(x,y,1,base-y+1,MD_EDGE);cv_rect(x-5,base+3,2+q->steps[i].velocity*8/100,2,ink);}
    md_circle(x,y,on?3:2,ink,on);
    if(i==q->edit)cv_rect(x-5,base+7,10,1,MD_INK);
    if(q->enabled&&md_playing()&&i==(int)playing_step)md_circle(x,y,6,MD_INK,0);
   }
   cv_text(12,143,&FONT_T,"PER TRACK / SAVE IN YOUR BIOME",MD_DIM);
   if(dream_ui.page==SEQUENCE){md_named(8,158,"K1 SEQUENCE",q->enabled?"ON":"OFF",col);md_value_span(124,158,"K2 STEP",q->edit+1,1,32,col);if(st->note){md_note_label(st->note,s);md_named(8,194,"K3 NOTE / MIDI",s,col);}else md_named(8,194,"K3 NOTE / MIDI","REST",col);md_value_span(124,194,"K4 LENGTH",q->length,1,32,col);}
   else{md_value(8,158,"K1 VELOCITY",st->velocity,col);md_value(124,158,"K2 GATE",st->gate,col);md_value(8,194,"K3 CHANCE",st->chance,col);md_named(124,194,"K4 GENERATE",md_generator_name(q->generator),col);}
  }else if(dream_ui.page==PROGRESSION){
   md_text(12,57,"GLOBAL HARMONY / CHOSEN TRACKS",MD_INK);md_text(12,85,md_chord_name(p->chord_pattern),col);
   const char *roman[]={"I","II","III","IV","V","VI","VII"};int n=p->chord_pattern>4?8:4,now=(md_status()->step/(16*p->chord_bars))%n;
   for(i=0;i<n;i++){int x=24+i*192/n;md_circle(x,113,4,i==now?MD_INK:col,i==now&&md_playing());md_text(x-3,122,roman[md_chord_degree(p->chord_pattern,i)],col);}
   md_num(dream_ui.track+1,s);md_text(166,140,"TRACK",MD_DIM);md_text(207,140,s,col);
   md_value_span(8,158,"K1 JOURNEY",p->chord_pattern,0,8,col);md_value_span(124,158,"K2 BARS / CHORD",p->chord_bars,1,16,col);md_named(8,194,"K3 THIS TRACK",(p->chord_tracks>>dream_ui.track)&1?"FOLLOW":"STAY",col);md_named(124,194,"K4 ROOT MOVES",p->motion.compact?"NEARBY":"ORIGINAL",col);
  }else if(dream_ui.page==CLOUD){
   md_text(12,58,p->kit.enabled?"ONE CLOUD / FOUR TRACK SENDS":"ONE CLOUD / SIX TRACK SENDS",MD_INK);
   for(i=0;i<md_active_tracks();i++){int x=24+i*(p->kit.enabled?56:38);int y=96+(int)(8*fm_sinf(flower_time*.001f+i));md_circle(x,y,4+p->track[i].send/200,dream_colors[i],0);}
   md_text(12,130,"AIR ENTERS TAPE / FREEZE HOLDS",MD_DIM);
   md_value(8,158,"K1 ALL: AIR",p->grain_noise/10,col);md_value(124,158,"K2 ALL: SPACE",p->macro[3]/10,col);md_value(8,194,"K3 THIS: SEND",t->send/10,col);md_named(124,194,"K4 ALL: FREEZE",p->freeze?"ON":"OFF",col);
  }else if(dream_ui.page==MIDI){
   md_text(12,65,"EACH TRACK: MIDI OFF OR CH 1-16",MD_INK);md_text(12,94,"VELOCITY / SUSTAIN / +/-2 BEND",MD_DIM);md_text(12,120,"CC7 LEVEL / 74 FILTER / 91 ROOM",MD_DIM);
   md_named(8,158,"K1 CLOCK",md_clock_source()==0?"INTERNAL":md_clock_source()==1?"USB":"TRS",col);md_named(124,158,"K2 SCALE SNAP",md_midi_quantize()?"ON":"OFF",col);
   char route_label[12];if(md_midi_route(dream_ui.track)){memcpy(route_label,"CH ",3);md_num(md_midi_route(dream_ui.track),route_label+3);}else memcpy(route_label,"OFF",4);
   md_named(8,194,"K3 THIS TRACK",route_label,col);md_named(124,194,"K4 CH16 MACROS",md_midi_macro_enabled()?"ON":"OFF",col);
  }else if(dream_ui.page==BANKS){
   md_text(12,66,"KEEP YOUR OWN SMALL WORLDS",MD_INK);md_text(12,96,bank_name,col);char prefix[24];memcpy(prefix,bank_name,bank_char);prefix[bank_char]=0;cv_rect(12+text_w(md_label_font(12,96,bank_name),prefix),113,5,1,col);md_text(12,121,"STOP FIRST / INTERNAL CLOCK",MD_DIM);
   md_value_span(8,158,"K1 USER SLOT",bank_slot+1,1,bank_kind?64:16,col);md_named(124,158,"K2 BANK",bank_kind?"SOUNDS":"BIOMES",col);md_named(8,194,"K3 TURN RIGHT","LOAD",col);md_value_span(124,194,"K4 NAME CURSOR",bank_char+1,1,23,col);
  }else if(dream_ui.page==MIXER){
   for(i=0;i<md_active_tracks();i++){int x=12+i*(p->kit.enabled?56:38),h=p->track[i].level*65/1000;md_mixer_leaf(x,h,p->track[i].mute?MD_DIM:dream_colors[i]);md_num(i+1,s);md_text(x+8,134,s,i==dream_ui.track?MD_INK:MD_DIM);int meter=clamp((int)(md_status()->track_peak[i]*260),0,65);cv_rect(x+26,130-meter,2,meter,MD_INK);if(i==dream_ui.track)cv_rect(x,148,24,2,col);}
   md_value(8,158,"K1 LEVEL",t->level/10,col);md_value(124,158,"K2 GRAIN",t->send/10,col);md_value(8,194,"K3 DELAY",e->delay_send/10,col);md_value(124,194,"K4 REVERB",e->reverb_send/10,col);
  }else if(dream_ui.page==BASSLINE){
   md_text(12,62,"ROOTED PHRASES / ROUND LOW END",MD_INK);
   for(i=0;i<16;i++)md_circle(15+i*14,107-e->notes[i],e->notes[i]<0?1:3,col,e->notes[i]>=0);
   md_named(8,158,"K1 PHRASE",e->bass_mode==2?"STEP":e->bass_mode?"EUCLID":"OFF",col);md_value_span(124,158,"K2 MOTIF",e->pattern+1,1,4,col);md_value(8,194,"K3 GLIDE",e->glide/10,col);md_value_span(124,194,"K4 BASE NOTE",t->note,24,96,col);
  }else if(dream_ui.page==SPACE){
   md_text(18,70,"ONE ROOM / SIX SMALL WORLDS",MD_INK);md_text(18,100,"MIXER SETS EACH TRACK SEND",MD_DIM);
   md_value_span(8,158,"K1 ECHO ms",p->delay_ms,40,350,col);md_value(124,158,"K2 FEEDBACK",p->delay_fb/10,col);md_value(8,194,"K3 ROOM TAIL",p->reverb_time/10,col);md_value(124,194,"K4 RETURN",p->fx_level/10,col);
  }else if(dream_ui.page==MODULATION){
   md_text(15,65,"MEMORY / TIDES / RING BLEND",MD_INK);md_text(15,94,md_lfo_target_name(p->motion.target[dream_ui.track]),col);md_text(15,121,"FX AGAIN: TIDAL TARGET / CURVE",MD_DIM);
   md_value(8,158,"K1 MARBLES",e->marbles/10,col);md_value(124,158,"K2 TIDES",e->tides/10,col);md_value(8,194,"K3 CYCLE",e->tides_rate/10,col);md_value(124,194,"K4 WARPS",e->warps/10,col);
  }else{
   for(i=0;i<16;i++){int x=12+(i%8)*28,y=62+(i/8)*36;md_num(e->notes[i],s);md_text(x,y,s,i==e->edit_step?MD_INK:col);if(i==e->edit_step)cv_rect(x,y+14,20,2,col);}
   md_value_span(8,158,"K1 STEP",e->edit_step+1,1,16,col);md_value_span(124,158,"K2 NOTE / REST",e->notes[e->edit_step],-1,24,col);md_value(8,194,"K3 ACCENT",t->accent/10,col);md_value(124,194,"K4 CHANCE",t->prob/10,col);
  }
 }else if(dream_ui.page==LIVE) {
  md_bloom_cache();
  for(i=0;i<md_active_tracks();i++){int x=(int)bloom_cx[i],y=(int)bloom_cy[i];
   int muted=p->track[i].mute;uint16_t quiet=RGB(112,130,137);
   // Muted voices leave a quiet outline instead of the same bright blossom.
   if(!muted)for(int k=0;k<72;k++)md_soft_point(bloom_x[i][k],bloom_y[i][k],dream_colors[i]);
   md_circle(x,y,6+i%3,muted?quiet:dream_colors[i],!muted&&i==dream_ui.track);
   if(!muted&&dream_prefs.motion&&(pulse_active&(1u<<i))&&(uint32_t)(plat_ms()-pulse_start[i])<160)md_circle(x,y,4,MD_INK,1);
   if(muted||i==dream_ui.track)cv_rect(94,43+i*16,137,16,muted?RGB(17,26,32):MD_CARD);
   if(muted)cv_rect(98,57+i*16,128,1,quiet);
   md_num(i+1,s);md_text(98,44+i*16,s,muted?quiet:dream_colors[i]);
   md_text(112,44+i*16,muted?"MUTED":(p->kit.enabled&&i==3?"LO-FI KIT":md_model_name(p->track[i].model)),muted?quiet:MD_INK);
   if(!muted){int meter=clamp((int)(md_status()->track_peak[i]*100),0,18);cv_rect(212,55+i*16,meter,2,dream_colors[i]);}
   if(i==dream_ui.track)cv_rect(94,46+i*16,2,9,col);
  }
  md_circle(51,95,5,MD_INK,0);
  md_text(12,141,p->kit.enabled&&dream_ui.track==3?md_kit_name(p->kit.bank):md_tone_name(p->tone_preset[dream_ui.track]),col);md_text(140,141,md_biome_family(p->biome_id),MD_DIM);
  const char *labels[]={"K1 DENSITY","K2 MOTION","K3 COLOUR","K4 SPACE"};for(i=0;i<4;i++)md_value(8+(i%2)*116,158+(i/2)*36,labels[i],p->macro[i]/10,dream_colors[i]);
 } else if(dream_ui.page==SYSTEM) {
  md_text(10,48,"MUTABLE DREAMS 1.4 DEV",MD_INK);md_text(10,69,p->quality?"FULL  24 MODES / 4 GRAINS":"ECO / ADAPTIVE MODES / 2 GRAINS",col);
  md_text(10,93,"LOAD / MAX %",MD_DIM);
#ifdef PLAT_HOST
  md_text(139,93,"HOST ONLY",MD_INK);
#else
  md_num(plat_cpu_pct(),s);md_text(144,93,s,MD_INK);md_text(172,93,"/",MD_DIM);md_num(cpu_peak,s);md_text(186,93,s,MD_INK);
#endif
  md_text(10,116,"XRUNS",MD_DIM);md_num(plat_xruns(),s);md_text(160,116,s,MD_INK);
  md_text(10,139,"DSP MEMORY / BYTES",MD_DIM);md_num(md_memory_bytes(),s);md_text(160,139,s,MD_INK);
  md_text(10,166,"K1 TEMPO   K2 ECO / FULL",MD_INK);md_text(10,188,"REC: PANIC / CLEAR TAPE",col);
  md_text(10,211,"K3: DUCK RELEASE / K4: RESET",MD_DIM);
 } else if(dream_ui.page==APPEARANCE) {
  for(i=0;i<6;i++){float a=i*1.04719755f+flower_time*.0003f;md_circle(120+(int)(fm_cosf(a)*23),100+(int)(fm_sinf(a)*23),19,dream_colors[i],0);}
  md_text(24,140,"SAVE KEEPS THESE SETTINGS",MD_DIM);
  md_named(8,158,"K1 MOTION",dream_prefs.motion?"LIVING":"STILL",col);md_named(124,158,"K2 START SOUND",dream_prefs.boot_sound?"ON":"OFF",col);
  md_named(8,194,"K3 INTRO","REPLAY",col);md_named(124,194,"K4 BUTTON LED",dream_prefs.button_glow==0?"OFF":dream_prefs.button_glow==1?"DIM":"SOFT",col);
 } else if(dream_ui.page==HARMONY) {
  static const char *notes[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  cv_text(24,60,&FONT_M,notes[p->root],col);cv_text(76,68,&FONT_B,md_scale_name(p->scale),MD_INK);
  md_text(12,108,"MELODIC VOICES FOLLOW SCALE",MD_DIM);md_text(12,130,"GLO AGAIN: CHORD JOURNEY",MD_DIM);
  md_named(8,158,"K1 ROOT",notes[p->root],col);md_named(124,158,"K2 SCALE",md_scale_name(p->scale),col);
  md_value_span(8,194,"K3 TRANSPOSE",p->transpose,-24,24,col);md_value_span(124,194,"K4 TEMPO",p->tempo,30,220,col);
 } else {
  if(dream_ui.page!=GRAIN&&dream_ui.page!=TAPE){
  md_num(dream_ui.track+1,s);md_text(10,47,"TRACK",MD_DIM);cv_text(48,42,&FONT_M,s,col);
  cv_rect(72,46,160,18,MD_BG);cv_text(74,48,&FONT_B,md_model_name(t->model),MD_INK);
  md_text(14,69,md_tone_name(p->tone_preset[dream_ui.track]),col);if(p->edited_mask&(2u<<dream_ui.track))md_text(210,69,"*",col);
  }else if(dream_ui.page==TAPE){md_text(12,51,"GLOBAL TAPE / ALL TRACK SENDS",MD_INK);}
  if(dream_ui.page==SOUND) {
   for(i=0;i<44;i++){float a=i*.42f+flower_time*.00015f;int r=14+i/2+(int)(3*fm_sinf(i*.25f+flower_time*.001f));md_circle(119+(int)(fm_cosf(a)*r),109+(int)(fm_sinf(a)*r),i%4==0?3:1,col,1);}
   md_value(8,158,"K1 TONE",t->tone/10,col);md_value(124,158,"K2 HARMONICS",t->harm/10,col);if(t->model==MD_ELEMENTS)md_named(8,194,"K3 EXCITER",t->shape<334?"MALLET":t->shape<667?"PLECTRUM":"PARTICLES",col);else md_value(8,194,md_has_envelope(t->model)?"K3 SHAPE":"K3 DECAY",(md_has_envelope(t->model)?t->shape:t->decay)/10,col);md_value(124,194,"K4 LEVEL",t->level/10,col);
  } else if(dream_ui.page==FILTER) {
   static const char *modes[]={"OFF","LP 12 dB","LP 24 dB","LOW PASS GATE"};
   for(i=0;i<180;i++){float z=(i-t->cutoff*.15f)*.07f;int y=86+(int)(45.0f*(z>0?z/(1+z):0));cv_pset(30+i,y,col);cv_pset(30+i,y+1,col);}
   md_text(40,137,"SOFT EDGES / SLOW BREATH",MD_DIM);
   md_value_span(8,158,"K1 CUTOFF Hz",md_cutoff_hz(t->cutoff),60,18000,col);md_value(124,158,"K2 RESONANCE",t->resonance/10,col);
   md_named(8,194,"K3 CHARACTER",modes[t->filter],col);md_value(124,194,"K4 BREATHE",t->breathe/10,col);
  } else if(dream_ui.page==RHYTHM) {
   for(i=0;i<t->length;i++){float a=i*6.2831853f/t->length-1.5707963f;md_circle(120+(int)(fm_cosf(a)*35),110+(int)(fm_sinf(a)*35),3,md_hit(i,t->hits,t->length,t->rotate)?col:MD_DIM,md_hit(i,t->hits,t->length,t->rotate));}
   if(md_playing()){float a=(md_status()->step%t->length)*6.2831853f/t->length-1.5707963f;md_circle(120+(int)(fm_cosf(a)*27),110+(int)(fm_sinf(a)*27),2,MD_INK,1);}
   md_value_span(8,158,"K1 HITS",t->hits,0,t->length,col);md_value_span(124,158,"K2 LENGTH",t->length,1,32,col);md_value_span(8,194,"K3 ROTATION",t->rotate,0,t->length-1,col);md_value(124,194,"K4 CHANCE",t->prob/10,col);
  } else if(dream_ui.page==GRAIN) {
   md_text(20,55,"GLOBAL / ALL ACTIVE TRACK SENDS",MD_INK);
   // An illustrative cloud, not one marker per DSP grain: mix controls its
   // population, size its width, position its starting offset. No trigonometry.
   for(i=0;i<3+p->macro[3]*21/1000;i++){
    int drift=(dream_prefs.motion?field_time:0)/(65+p->grain_size/20+i%7);
    int x=18+(i*47+p->grain_pos/5+(p->reverse?204-drift%204:drift))%204;
    int wave=(drift+i*11)%16;wave=wave<8?wave:16-wave;
    int y=82+(i*29)%45+wave/2,rad=1+p->grain_size*3/1000;
    md_circle(x,y,rad,dream_colors[i%6],0);
    if(p->grain_size>250)cv_rect(x-rad-1-p->grain_size/200,y,p->grain_size/200,1,md_tint(MD_BG,dream_colors[i%6],12));
   }
   md_text(63,139,p->freeze?"FROZEN MEMORY":"LIVE INTERNAL SENDS",MD_INK);
   md_value(8,158,"K1 SPACE",p->macro[3]/10,col);md_value(124,158,"K2 GRAIN SIZE",p->grain_size/10,col);md_value(8,194,"K3 POSITION",p->grain_pos/10,col);md_value(124,194,"K4 FEEDBACK",p->feedback/10,col);
  } else if(dream_ui.page==ENVELOPE) {
   if(md_has_envelope(t->model))md_envelope_plot(t,col);else for(int j=0;j<4;j++)md_circle(120,109,10+j*7,md_tint(MD_BG,col,25-j*5),0);
   md_text(24,139,md_has_envelope(t->model)?"ONE-SHOT A/D / MIDI CAN HOLD":"ENGINE-SHAPED TAIL",MD_DIM);
   if(md_has_envelope(t->model)){md_value_span(8,158,"K1 ATTACK ms",8*t->attack*t->attack/1000,0,8000,col);md_value_span(124,158,"K2 DECAY ms",60+16*t->decay*t->decay/1000,60,16060,col);}else{md_named(8,158,"K1 MELODIC ONLY","-",MD_DIM);md_value(124,158,"K2 DECAY",t->decay/10,col);}
   md_value(8,194,"K3 LEVEL",t->level/10,col);md_value(124,194,"K4 SEND",t->send/10,col);
  } else if(dream_ui.page==GROOVE) {
   md_text(12,84,"SWING IS SHARED BY ALL TRACKS",MD_DIM);md_text(12,108,"REPEATS / ACCENT PER TRACK",MD_DIM);
   md_value_span(8,158,"K1 SWING",p->swing/10,0,60,col);md_value_span(124,158,"K2 REPEATS",t->ratchet,1,4,col);
   md_value(8,194,"K3 ACCENT",t->accent/10,col);md_value_span(124,194,"K4 CLOCK DIV",t->speed,1,8,col);
  } else if(dream_ui.page==TAPE) {
   md_text(24,80,p->reverse?"<  REVERSE MEMORY":"FORWARD MEMORY  >",col);
   md_text(12,109,"DUCK SOURCE: ANY TRACK / 0 OFF",MD_DIM);
   md_value_span(8,158,"K1 PITCH st",p->grain_pitch,-24,24,col);md_named(124,158,"K2 DIRECTION",p->reverse?"REVERSE":"FORWARD",col);
   if(p->duck_source<0)md_named(8,194,"K3 DUCK FROM","OFF",col);else md_value_span(8,194,"K3 DUCK TRACK",p->duck_source+1,1,6,col);md_value_span(124,194,"K4 DUCK %",p->duck_amount*90/1000,0,90,col);
  } else {
   md_text(10,82,"ALGO: CHANGE SOUND",MD_DIM);md_text(10,104,"SEL: MUTE THIS TRACK",MD_DIM);md_text(10,126,t->mute?"MUTED":"TRACK AUDIBLE",col);
   md_value(8,158,"K1 SEND",t->send/10,col);md_value_span(124,158,"K2 MIDI NOTE",t->note,24,96,col);md_value(8,194,"K3 PAN",t->pan/10,col);md_value_span(124,194,"K4 CLOCK DIV",t->speed,1,8,col);
  }
 }
 if(dream_ui.msg[0]&&(uint32_t)(plat_ms()-dream_ui.msg_start)<1400){cv_rect(0,224,240,16,col);md_text(6,224,dream_ui.msg,MD_BG);}
 else {
  if(dream_ui.page==LIVE){md_num(p->biome_id+1,s);md_text(8,226,s,col);md_text(24,226,md_biome_name(p->biome_id),MD_INK);if(p->edited_mask&1)md_text(151,226,"*",col);md_text(164,226,p->locked?"LOCK":"RAND",MD_DIM);md_text(203,226,p->freeze?"ICE":"REC",MD_DIM);}
  else md_text(10,226,(dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)?"PRESET: PAD  /  HOLD: HELP":(dream_ui.page==SOUND||dream_ui.page==FILTER||dream_ui.page==ENVELOPE||dream_ui.page==MIX||dream_ui.page==MIXER||dream_ui.page==BASSLINE||dream_ui.page==MODULATION||dream_ui.page==LFO_ROUTE||dream_ui.page==LFO_ROUTE2||dream_ui.page==SEQ_FLOW||(dream_ui.page==PHRASE||dream_ui.page==SEQUENCE||dream_ui.page==STEP_DETAIL))?"PRESET: TONE  /  HOLD: HELP":"PRESET: BIOME  /  HOLD: HELP",MD_DIM);
 }
}
void app_say(const char *a,const char *b) {unsigned i=0;(void)b;while(a&&a[i]&&i<31){dream_ui.msg[i]=a[i];i++;}dream_ui.msg[i]=0;dream_ui.msg_start=plat_ms();dream_ui.dirty=1;ann_say(dream_ui.msg,0,ANN_NOW);}
void app_init(void) {
 bank_pending=-1;bank_allow_load=0;md_patch saved;md_init();midi_link=plat_midi_out_link();overloads=cpu_peak=quality_notice=0;
 dream_prefs.magic=0x4D445046;dream_prefs.motion=1;dream_prefs.boot_sound=1;dream_prefs.button_glow=1;dream_prefs.revision=1;for(int i=0;i<6;i++)dream_prefs.midi_route[i]=i+1;dream_prefs.midi_macro=1;dream_prefs.reserved=0;
 md_preferences prefs={0};
 int pref_bytes=plat_store_load(OBJ_PREFS,&prefs,sizeof prefs);
 int valid=prefs.magic==0x4D445046&&prefs.motion>=0&&prefs.motion<=1&&prefs.boot_sound>=0&&prefs.boot_sound<=1&&(pref_bytes==12||(prefs.button_glow>=0&&prefs.button_glow<=2));
 if(pref_bytes==(int)sizeof prefs){valid=valid&&prefs.revision==1&&prefs.midi_macro<=1&&prefs.reserved==0;for(int i=0;i<6;i++)valid=valid&&prefs.midi_route[i]<=16;}
 else if(pref_bytes!=12&&pref_bytes!=16)valid=0;
 if(valid){dream_prefs.motion=prefs.motion;dream_prefs.boot_sound=prefs.boot_sound;if(pref_bytes>=16)dream_prefs.button_glow=prefs.button_glow;if(pref_bytes==(int)sizeof prefs){for(int i=0;i<6;i++)dream_prefs.midi_route[i]=prefs.midi_route[i];dream_prefs.midi_macro=prefs.midi_macro;}}
 for(int i=0;i<6;i++)md_set_midi_route(i,dream_prefs.midi_route[i]);md_set_midi_macro_enabled(dream_prefs.midi_macro);
 intro_pending=1;intro_active=chime_left=0;pulse_active=0;md_gain=0;memset(&dream_ui,0,sizeof dream_ui);dream_ui.help=-1;
 int bytes=plat_store_load(OBJ_PATCH,&saved,sizeof saved);if(bytes>0)md_import(&saved,(unsigned)bytes);
}
void app_render(int32_t *lr,uint32_t n,uint32_t master) {
 if(dream_reloading){memset(lr,0,n*2*sizeof *lr);return;}
 uint32_t pkt,load=plat_cpu_pct();int port;if(load>cpu_peak)cpu_peak=load;
 uint32_t link=plat_midi_out_link();if(link!=midi_link){midi_link=link;for(unsigned i=0;i<16;i++)md_midi(0xb0+i,123,0,0);}
 for(unsigned m=0;m<16&&plat_midi_in(&pkt,&port);m++)md_midi((pkt>>8)&255,(pkt>>16)&127,(pkt>>24)&127,port);
 while(n){unsigned k,z=n>128?128:n;md_render(md_audio,z);if(md_playing())chime_left=0;for(k=0;k<z;k++){md_gain+=((float)master/4096.0f-md_gain)*.004f;float chime=0;

 if(chime_left){unsigned elapsed=88200-chime_left;float env=elapsed<11025?elapsed/11025.0f:chime_left/77175.0f;const float hz[3]={261.6256f,391.9954f,523.2511f};for(int c=0;c<3;c++){chime_phase[c]+=hz[c]/44100.0f;if(chime_phase[c]>=1)chime_phase[c]-=1;chime+=fm_sinf(chime_phase[c]*6.2831853f)*.009f*env;}chime_left--;}
 lr[2*k]=(int32_t)((md_audio[2*k]+chime)*md_gain*8380000.0f);lr[2*k+1]=(int32_t)((md_audio[2*k+1]+chime)*md_gain*8380000.0f);}lr+=z*2;n-=z;}
}
void app_panic(void) {dream_cmd(MD_PANIC,0,0);}
void app_overload(void) {overloads++;dream_cmd(MD_QUALITY,0,0);}
static void md_tap(int b) {
 if(md_pending_biome()>=0||bank_pending>=0){
  if(b==B_SEL){if(bank_pending>=0){bank_slot=bank_pending;bank_kind=0;bank_pending=-1;bank_allow_load=1;bank_action(0);bank_allow_load=0;}else dream_cmd(MD_SCENE_CONFIRM,0,1);}
  else if(b==B_HOME||b==B_SAVE){bank_pending=-1;dream_cmd(MD_SCENE_CONFIRM,0,0);dream_ui.page=b==B_SAVE?BANKS:LIVE;}
  return;
 }
 const md_patch *p=md_state();switch(b){
 case B_HOME:dream_ui.page=LIVE;break;
 case B_EDIT:if(p->kit.enabled&&(dream_ui.track==3||(dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ))){dream_ui.track=3;dream_ui.page=dream_ui.page==KIT_PAD?KIT_SEND:dream_ui.page==KIT_SEND?MIXER:KIT_PAD;break;}dream_ui.page=dream_ui.page==SOUND?FILTER:dream_ui.page==FILTER?ENVELOPE:dream_ui.page==ENVELOPE?MIX:dream_ui.page==MIX?MIXER:SOUND;break;
 case B_SEQ:if(p->kit.enabled&&dream_ui.track==3){dream_ui.page=KIT_SEQ;break;}dream_ui.page=dream_ui.page==RHYTHM?GROOVE:dream_ui.page==GROOVE?BASSLINE:dream_ui.page==BASSLINE?PHRASE:dream_ui.page==PHRASE?SEQUENCE:dream_ui.page==SEQUENCE?STEP_DETAIL:dream_ui.page==STEP_DETAIL?SEQ_FLOW:RHYTHM;break;
 case B_FX:dream_ui.page=dream_ui.page==GRAIN?TAPE:dream_ui.page==TAPE?SPACE:dream_ui.page==SPACE?MODULATION:dream_ui.page==MODULATION?LFO_ROUTE:dream_ui.page==LFO_ROUTE?LFO_ROUTE2:dream_ui.page==LFO_ROUTE2?CLOUD:GRAIN;break;
 case B_GLO:dream_ui.page=dream_ui.page==HARMONY?PROGRESSION:dream_ui.page==PROGRESSION?SYSTEM:dream_ui.page==SYSTEM?APPEARANCE:dream_ui.page==APPEARANCE?MIDI:dream_ui.page==MIDI?BANKS:dream_ui.page==BANKS?KIT:HARMONY;break;
 case B_SEL:dream_cmd(MD_MUTE,dream_ui.track,!p->track[dream_ui.track].mute);break;
 case B_PLAY:dream_cmd(MD_PLAY,0,!md_playing());break;case B_ARP:dream_cmd(MD_LOCK,0,!p->locked);break;
 case B_REC:if(dream_ui.page==KIT_SEQ){dream_cmd(MD_RECORD,3,(md_record_mode()+1)%3);break;}if((dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)){dream_cmd(MD_KIT_TRIGGER,3,p->kit.pad);break;}if(dream_ui.page==SEQUENCE||dream_ui.page==STEP_DETAIL||dream_ui.page==SEQ_FLOW){dream_cmd(MD_RECORD,dream_ui.track,(md_record_mode()+1)%3);break;}dream_cmd((dream_ui.page==SYSTEM||dream_ui.page==MIDI)?MD_PANIC:MD_MUTATE,0,0);break;case B_ENV:dream_cmd(MD_RETURN,0,0);break;
 case B_LFO:dream_cmd(MD_FREEZE,0,!p->freeze);break;
 case B_OCTDN:dream_ui.octave=clamp(dream_ui.octave-1,-2,2);break;case B_OCTUP:dream_ui.octave=clamp(dream_ui.octave+1,-2,2);break;
 case B_SAVE:if(dream_store_hold){app_say("USB BACKUP IN PROGRESS","");break;}if(dream_ui.page==BANKS){bank_action(1);break;}if(md_playing())app_say("STOP BEFORE SAVING","");else if(md_clock_source())app_say("USE INTERNAL CLOCK TO SAVE","");else {md_patch snapshot;uint32_t f=plat_lock();snapshot=*p;plat_unlock(f);int failed=plat_store_save(OBJ_PATCH,&snapshot,sizeof snapshot);if(!failed)failed=plat_store_save(OBJ_PREFS,&dream_prefs,sizeof dream_prefs);if(!failed){uint32_t f=plat_lock();md_mark_saved();plat_unlock(f);}app_say(failed?"SAVE FAILED":"BIOME + PREFS SAVED","");}break;
 }
}
static void editor_expire(uint32_t now);
void app_input_only(void) {
 unsigned b,now=plat_ms(),btn=plat_buttons(),keys=plat_keys();const md_patch *p=md_state();int d;
 /* Unsigned elapsed times remain correct across the 32-bit clock rollover. */
 if(intro_active&&(uint32_t)(now-intro_start)>=3200)intro_active=0;
 if(dream_ui.msg[0]&&(uint32_t)(now-dream_ui.msg_start)>=1400)dream_ui.msg[0]=0;
 for(int i=0;i<6;i++)if((pulse_active&(1u<<i))&&(uint32_t)(now-pulse_start[i])>=160)pulse_active&=~(1u<<i);
 if(bank_armed>=0&&(uint32_t)(now-bank_arm_start)>3000)bank_armed=-1;
 editor_expire(now);
 for(b=0;b<NB;b++) {
  if((btn>>b&1)&&!(dream_ui.btn>>b&1)){dream_ui.down[b]=now;dream_dismiss_intro();}
  if((btn>>b&1)&&!(dream_ui.held>>b&1)&&now-dream_ui.down[b]>=850){dream_ui.held|=1u<<b;dream_ui.help=b;ann_say(help_title[b],0,ANN_NOW);}
  if(!(btn>>b&1)&&(dream_ui.btn>>b&1)){if(!(dream_ui.held>>b&1)){if(dream_ui.help>=0&&md_pending_biome()<0&&bank_pending<0)dream_ui.help=-1;else{dream_ui.help=-1;md_tap(b);}}dream_ui.held&=~(1u<<b);}
 }
 dream_ui.btn=btn;
 if(md_pending_biome()>=0||bank_pending>=0){for(unsigned k=0;k<7;k++)plat_enc(k);dream_ui.keys=keys;return;}
 if(dream_ui.track>=md_active_tracks())dream_ui.track=md_active_tracks()-1;
 if((d=plat_enc(EN_SELECT))){dream_dismiss_intro();dream_ui.track=clamp(dream_ui.track+d,0,md_active_tracks()-1);}
 if((d=plat_enc(EN_ALGO))&&!(p->kit.enabled&&dream_ui.track==3)){dream_dismiss_intro();dream_cmd(MD_MODEL,dream_ui.track,clamp(p->track[dream_ui.track].model+d,0,MD_MODELS-1));dream_ui.page=SOUND;}
 if((d=plat_enc(EN_PRESET))&&dream_ui.help<0){dream_dismiss_intro();
  if((dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)||(p->kit.enabled&&dream_ui.track==3&&dream_ui.page!=LIVE&&dream_ui.page!=BANKS)){dream_cmd(MD_KIT_PAD,3,clamp(p->kit.pad+d,0,7));}
  else if(dream_ui.page==BANKS){for(int j=0;j<=bank_char;j++)if(!bank_name[j])bank_name[j]=' ';bank_name[bank_char]=clamp(bank_name[bank_char]+d,32,126);bank_name[23]=0;bank_armed=-1;}else if(dream_ui.page==SOUND||dream_ui.page==FILTER||dream_ui.page==ENVELOPE||dream_ui.page==MIX||dream_ui.page==MIXER||dream_ui.page==BASSLINE||dream_ui.page==MODULATION||dream_ui.page==LFO_ROUTE||dream_ui.page==LFO_ROUTE2||dream_ui.page==SEQ_FLOW||(dream_ui.page==PHRASE||dream_ui.page==SEQUENCE||dream_ui.page==STEP_DETAIL)){
   int id=p->tone_preset[dream_ui.track];if(id<0)id=(p->track[dream_ui.track].model>=12?224+(p->track[dream_ui.track].model-12)*16:p->track[dream_ui.track].model==11?208:p->track[dream_ui.track].model==10?160:p->track[dream_ui.track].model*16)-(d>0?1:0);
   id=clamp(id+d,0,MD_TONE_PRESETS-1);dream_cmd(MD_TONE_PRESET,dream_ui.track,id);app_say(md_tone_name(id),"");
  }else{int id=md_biome_step(p->biome_id,d);dream_cmd(MD_SCENE,0,id);app_say(md_biome_name(id),"");}
 }
 for(b=0;b<4;b++)if((d=plat_enc(EN_K1+b))&&dream_ui.help<0) {
  dream_dismiss_intro();const md_track *t=&p->track[dream_ui.track];int c=0,v=0,tr=dream_ui.track;
  if((dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)){const md_kit *k=&p->kit;const md_pad *a=&k->pads[k->pad];int cs[4],vs[4],mul[4]={1,1,1,1};
   if(dream_ui.page==KIT){int cc[]={MD_KIT_MODE,MD_KIT_BANK,MD_KIT_PAD,MD_KIT_HISS},vv[]={k->enabled,k->bank,k->pad,k->hiss};memcpy(cs,cc,sizeof cs);memcpy(vs,vv,sizeof vs);mul[3]=5;}
   else if(dream_ui.page==KIT_PAD){int cc[]={MD_KIT_TUNE,MD_KIT_LEVEL,MD_KIT_CUTOFF,MD_KIT_DECAY},vv[]={a->tune,a->level,a->cutoff,a->decay};memcpy(cs,cc,sizeof cs);memcpy(vs,vv,sizeof vs);mul[1]=mul[2]=mul[3]=20;}
   else if(dream_ui.page==KIT_SEND){int cc[]={MD_KIT_PAD,MD_KIT_SEND,MD_DELAY_SEND,MD_REVERB_SEND},vv[]={k->pad,a->send,p->ext[3].delay_send,p->ext[3].reverb_send};memcpy(cs,cc,sizeof cs);memcpy(vs,vv,sizeof vs);mul[1]=mul[2]=mul[3]=20;}
   else{int cc[]={MD_KIT_STEP,MD_KIT_GATE,MD_KIT_LENGTH,MD_KIT_GENERATE},vv[]={k->step,(k->gates[k->pad]>>k->step)&1,k->length,k->generator};memcpy(cs,cc,sizeof cs);memcpy(vs,vv,sizeof vs);}
   dream_cmd(cs[b],3,vs[b]+d*mul[b]);continue;
  }
  if(dream_ui.page==BANKS){if(b==0){bank_slot=clamp(bank_slot+d,0,bank_kind?63:15);bank_armed=-1;}else if(b==1){bank_kind=clamp(bank_kind+d,0,1);bank_slot=clamp(bank_slot,0,bank_kind?63:15);bank_armed=-1;}else if(b==2&&d>0)bank_action(0);else if(b==3)bank_char=clamp(bank_char+d,0,22);continue;}
  if(dream_ui.page==MIDI){if(b==0)dream_cmd(MD_CLOCK_SOURCE,0,md_clock_source()+d);else if(b==1)dream_cmd(MD_MIDI_QUANTIZE,0,md_midi_quantize()+d);else if(b==2)dream_cmd(MD_MIDI_ROUTE,dream_ui.track,dream_prefs.midi_route[dream_ui.track]+d);else dream_cmd(MD_MIDI_MACRO,0,dream_prefs.midi_macro+d);continue;}
  if(dream_ui.page==APPEARANCE){if(b==0)dream_prefs.motion=clamp(dream_prefs.motion+d,0,1);else if(b==1)dream_prefs.boot_sound=clamp(dream_prefs.boot_sound+d,0,1);else if(b==2)dream_intro();else dream_prefs.button_glow=clamp(dream_prefs.button_glow+d,0,2);continue;}
  if(dream_ui.page==LIVE){c=MD_MACRO;tr=b;v=p->macro[b]+d*20;}
  else if(dream_ui.page>=MIXER){const md_extension *e=&p->ext[tr];
   if(dream_ui.page==SEQUENCE){const md_sequence *q=&p->sequence[tr];const int cs[]={MD_SEQ_ENABLE,MD_SEQ_STEP,MD_SEQ_NOTE,MD_SEQ_LENGTH},vs[]={q->enabled,q->edit,q->steps[q->edit].note?q->steps[q->edit].note:23,q->length};c=cs[b];v=vs[b]+d;}
   else if(dream_ui.page==SEQ_FLOW){const md_motion *m=&p->motion;const int cs[]={MD_SEQ_DIRECTION,MD_SEQ_SHIFT,MD_SEQ_FEEL,MD_SEQ_GENERATE},vs[]={m->direction[tr],m->shift[tr],m->feel[tr],p->sequence[tr].generator};c=cs[b];v=vs[b]+d*(b==2?5:1);}
   else if(dream_ui.page==LFO_ROUTE2){if(p->kit.enabled&&tr==3){app_say("SELECT A SYNTH TRACK","");return;}const int cs[]={MD_LFO2_TARGET,MD_LFO2_SHAPE,MD_LFO2_DEPTH,MD_LFO2_RATE},vs[]={p->motion.target2[tr],p->motion.wave2[tr],p->motion.depth2[tr],p->motion.rate2[tr]};c=cs[b];v=vs[b]+d*(b>=2?20:1);}
   else if(dream_ui.page==LFO_ROUTE){if(p->kit.enabled&&tr==3){app_say("SELECT A SYNTH TRACK","");return;}const int cs[]={MD_LFO_TARGET,MD_LFO_SHAPE,MD_TIDES,MD_TIDES_RATE},vs[]={p->motion.target[tr],p->motion.wave[tr],e->tides,e->tides_rate};c=cs[b];v=vs[b]+d*(b>=2?20:1);}
   else if(dream_ui.page==STEP_DETAIL){const md_sequence *q=&p->sequence[tr];const md_step *st=&q->steps[q->edit];const int cs[]={MD_SEQ_VELOCITY,MD_SEQ_GATE,MD_SEQ_CHANCE,MD_SEQ_GENERATE},vs[]={st->velocity,st->gate,st->chance,q->generator};c=cs[b];v=vs[b]+d*(b==3?1:5);}
   else if(dream_ui.page==PROGRESSION){const int cs[]={MD_CHORD_PATTERN,MD_CHORD_BARS,MD_CHORD_FOLLOW,MD_CHORD_COMPACT},vs[]={p->chord_pattern,p->chord_bars,(p->chord_tracks>>tr)&1,p->motion.compact};c=cs[b];v=vs[b]+d;}
   else if(dream_ui.page==CLOUD){const int cs[]={MD_GRAIN_NOISE,MD_MACRO,MD_SEND,MD_FREEZE},vs[]={p->grain_noise,p->macro[3],t->send,p->freeze};c=cs[b];v=vs[b]+d*(b==3?1:20);if(b==1)tr=3;}
   else if(dream_ui.page==MIXER){const int cs[]={MD_LEVEL,MD_SEND,MD_DELAY_SEND,MD_REVERB_SEND},vs[]={t->level,t->send,e->delay_send,e->reverb_send};c=cs[b];v=vs[b]+d*20;}
   else if(dream_ui.page==BASSLINE){const int cs[]={MD_BASS_MODE,MD_BASS_PATTERN,MD_GLIDE,MD_NOTE},vs[]={e->bass_mode,e->pattern,e->glide,t->note};c=cs[b];v=vs[b]+d*(b==2?20:1);}
   else if(dream_ui.page==SPACE){const int cs[]={MD_DELAY_TIME,MD_DELAY_FB,MD_REVERB_TIME,MD_FX_LEVEL},vs[]={p->delay_ms,p->delay_fb,p->reverb_time,p->fx_level};c=cs[b];v=vs[b]+d*(b==0?5:20);}
   else if(dream_ui.page==MODULATION){const int cs[]={MD_MARBLES,MD_TIDES,MD_TIDES_RATE,MD_WARPS},vs[]={e->marbles,e->tides,e->tides_rate,e->warps};c=cs[b];v=vs[b]+d*20;}
   else{const int cs[]={MD_BASS_STEP,MD_BASS_VALUE,MD_ACCENT,MD_PROB},vs[]={e->edit_step,e->notes[e->edit_step],t->accent,t->prob};c=cs[b];v=vs[b]+d*(b<2?1:20);}
  }
  else if(dream_ui.page==SOUND){const int cmds[]={MD_TONE,MD_HARM,(md_has_envelope(t->model)||t->model==MD_ELEMENTS)?MD_SHAPE:MD_DECAY,MD_LEVEL};const int vals[]={t->tone,t->harm,(md_has_envelope(t->model)||t->model==MD_ELEMENTS)?t->shape:t->decay,t->level};c=cmds[b];v=vals[b]+d*20;}
  else if(dream_ui.page==FILTER){const int cmds[]={MD_CUTOFF,MD_RESONANCE,MD_FILTER,MD_BREATHE};const int vals[]={t->cutoff,t->resonance,t->filter,t->breathe};c=cmds[b];v=vals[b]+d*(b==2?1:20);}
  else if(dream_ui.page==RHYTHM){const int cmds[]={MD_HITS,MD_LENGTH,MD_ROTATE,MD_PROB};const int vals[]={t->hits,t->length,t->rotate,t->prob};c=cmds[b];v=vals[b]+d*(b==3?20:1);}
  else if(dream_ui.page==GRAIN){const int cmds[]={MD_MACRO,MD_GRAIN_SIZE,MD_GRAIN_POS,MD_FEEDBACK};const int vals[]={p->macro[3],p->grain_size,p->grain_pos,p->feedback};c=cmds[b];v=vals[b]+d*20;tr=3;}
  else if(dream_ui.page==MIX){const int cmds[]={MD_SEND,MD_NOTE,MD_PAN,MD_SPEED};const int vals[]={t->send,t->note,t->pan,t->speed};c=cmds[b];v=vals[b]+d*((b==0||b==2)?20:1);}
  else if(dream_ui.page==ENVELOPE){const int cmds[]={MD_ATTACK,MD_DECAY,MD_LEVEL,MD_SEND};const int vals[]={t->attack,t->decay,t->level,t->send};c=cmds[b];v=vals[b]+d*20;}
  else if(dream_ui.page==GROOVE){const int cmds[]={MD_SWING,MD_RATCHET,MD_ACCENT,MD_SPEED};const int vals[]={p->swing,t->ratchet,t->accent,t->speed};c=cmds[b];v=vals[b]+d*((b==0||b==2)?20:1);}
  else if(dream_ui.page==TAPE){const int cmds[]={MD_GRAIN_PITCH,MD_REVERSE,MD_DUCK_SOURCE,MD_DUCK};const int vals[]={p->grain_pitch,p->reverse,p->duck_source,p->duck_amount};c=cmds[b];v=vals[b]+d*(b==3?20:1);}
  else if(dream_ui.page==HARMONY){const int cmds[]={MD_ROOT,MD_SCALE,MD_TRANSPOSE,MD_TEMPO};const int vals[]={p->root,p->scale,p->transpose,p->tempo};c=cmds[b];v=vals[b]+d;}
  else {if(b==3){cpu_peak=0;plat_audio_peak_reset();continue;}const int cmds[]={MD_TEMPO,MD_QUALITY,MD_DUCK_RELEASE};const int vals[]={p->tempo,p->quality,p->duck_release};c=cmds[b];v=vals[b]+d*(b==2?20:1);}
  dream_cmd(c,tr,v);
 }
 for(b=0;b<27;b++){
  if((keys>>b&1)&&!(dream_ui.keys>>b&1)){dream_dismiss_intro();if(dream_ui.help<0){keyboard_track[b]=dream_ui.track;keyboard_note[b]=clamp(53+b+dream_ui.octave*12,24,96);if(p->kit.enabled&&dream_ui.track==3)dream_cmd(MD_KIT_TRIGGER,3,b%8);else dream_cmd(MD_TRIGGER,keyboard_track[b],keyboard_note[b]);}}
  if(!(keys>>b&1)&&(dream_ui.keys>>b&1))dream_cmd(MD_NOTE_OFF,keyboard_track[b],keyboard_note[b]);
 }
 dream_ui.keys=keys;
 /* The printed SEL key mutes; EDIT cycles sound, envelope, mix, avoiding a hidden modifier. */
}
void app_ui_init(void) {dream_ui.dirty=1;dream_ui.frame_at=plat_ms();dream_ui.frame_wait=0;if(intro_pending){intro_pending=0;dream_intro();}}
void app_frame(void) {
 unsigned y;app_input_only();if(md_status()->quality_reductions!=quality_notice){quality_notice=md_status()->quality_reductions;app_say("ECO: DENSE PATCH","");}flower_time=dream_prefs.motion?plat_ms():0;for(int i=0;i<6;i++)if(md_status()->events[i]!=last_events[i]){last_events[i]=md_status()->events[i];pulse_start[i]=plat_ms();pulse_active|=1u<<i;}if((uint32_t)(plat_ms()-dream_ui.frame_at)<dream_ui.frame_wait&&!dream_ui.dirty)return;dream_ui.frame_at=plat_ms();uint32_t audio_load=plat_cpu_pct();dream_ui.frame_wait=dream_prefs.motion?(audio_load>72?96:audio_load>40?66:58):80;dream_ui.dirty=0;
 uint32_t frame_started=plat_ms();md_field_update();
 for(y=0;y<240;y+=40){cv_begin(240,40,MD_BG);cv_oy=-(int)y;md_draw_all();cv_blit(0,y);}
 uint32_t bright=(1u<<B_HOME)|(md_playing()?LED_PLAY_GREEN:0)|(md_state()->locked?(1u<<B_ARP):0)|(md_state()->freeze?(1u<<B_LFO):0);
 plat_lights(bright,dream_ui.keys,((1u<<NB)-1u)&~bright,0,dream_prefs.button_glow);
 ann_tick(plat_ms());
 uint32_t elapsed=plat_ms()-frame_started;if(elapsed>ui_frame_peak_ms)ui_frame_peak_ms=elapsed;ui_frames++;
}
#ifdef PLAT_HOST
int app_expect(const char *what,int *got) {
 if(!strcmp(what,"unsaved")){*got=md_unsaved();return 1;}if(!strcmp(what,"pending_biome")){*got=md_pending_biome();return 1;}if(!strcmp(what,"kit_mode")){*got=md_state()->kit.enabled;return 1;}
 if(!strcmp(what,"field_density")){*got=field_density;return 1;}
 if(!strcmp(what,"field_size")){*got=field_size;return 1;}
 if(!strcmp(what,"help_context")){*got=md_context_help();return 1;}
 if(!strcmp(what,"motion"))*got=dream_prefs.motion;else if(!strcmp(what,"boot_sound"))*got=dream_prefs.boot_sound;else if(!strcmp(what,"button_glow"))*got=dream_prefs.button_glow;else if(!strcmp(what,"intro"))*got=intro_active&&(uint32_t)(plat_ms()-intro_start)<3200;else if(!strcmp(what,"track"))*got=dream_ui.track;else if(!strcmp(what,"model"))*got=md_state()->track[dream_ui.track].model;else if(!strcmp(what,"help"))*got=dream_ui.help;
 else if(!strcmp(what,"biome"))*got=md_state()->biome_id;else if(!strcmp(what,"tone_preset"))*got=md_state()->tone_preset[dream_ui.track];
 else if(!strcmp(what,"page"))*got=dream_ui.page;else if(!strcmp(what,"playing"))*got=md_playing();else if(!strcmp(what,"locked"))*got=md_state()->locked;
 else if(!strcmp(what,"density"))*got=md_state()->macro[0];else if(!strcmp(what,"freeze"))*got=md_state()->freeze;else if(!strcmp(what,"quality"))*got=md_state()->quality;
 else if(!strcmp(what,"scale"))*got=md_state()->scale;else if(!strcmp(what,"attack"))*got=md_state()->track[dream_ui.track].attack;else if(!strcmp(what,"ratchet"))*got=md_state()->track[dream_ui.track].ratchet;
 else if(!strcmp(what,"invalid"))*got=md_status()->nonfinite;else if(!strcmp(what,"queue_drops"))*got=md_status()->dropped_commands;else return 0;return 1;
}

#endif

#include "transfer.c"
#include "web.c"
#include "diagnostics.c"

void app_splash(void){for(unsigned y=0;y<240;y+=40){cv_begin(240,40,MD_BG);cv_oy=-(int)y;for(int j=0;j<6;j++){float a=j*1.04719755f;md_circle(120+(int)(fm_cosf(a)*27),100+(int)(fm_sinf(a)*27),23,dream_colors[j],0);}cv_text(42,168,&FONT_B,"MUTABLE DREAMS",MD_INK);md_text(60,194,"A BIOME IS WAKING",MD_DIM);cv_blit(0,y);}}
