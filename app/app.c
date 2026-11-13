/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
#include "dsp/dreams.h"
#include "fastmath.h"
#include "btdev.h"
#ifdef PLAT_HOST
#include <stdlib.h>
#endif
enum { LIVE,SOUND,RHYTHM,GRAIN,MIX,SYSTEM,ENVELOPE,GROOVE,TAPE,HARMONY,FILTER,APPEARANCE,MIXER,BASSLINE,SPACE,MODULATION,PHRASE,MIDI,BANKS,SEQUENCE,STEP_DETAIL,PROGRESSION,CLOUD,KIT,KIT_PAD,KIT_SEND,KIT_SEQ,SEQ_FLOW,LFO_ROUTE,LFO_ROUTE2,BT_PAGE,JOURNEY,JOURNEY_TIME,JOURNEY_KEEP,MIDI_LEARN,MIDI_MAP,LIVING_LINKS,LINK_MOTION,BIOME_DRIFT,DRIFT_SCENES,SEQ_GRID,SEQ_TOOLS,SEQ_RULES,USB_AUDIO,ITINERA,DREAM_PAGE_COUNT };
static int journey_edit,learn_slot,link_slot;
enum { LIVE_ATLAS, LIVE_TRACKS, LIVE_BROWSER, LIVE_VIEW_COUNT };
static struct { int page,track,help,dirty,octave; uint32_t btn,keys,down[NB],held,frame_at,frame_wait; char msg[32]; uint32_t msg_start; uint8_t live_view; } dream_ui;
static int dream_store_hold;static int bank_pending=-1,bank_allow_load;
static uint8_t keyboard_track[27],keyboard_note[27];
static volatile int dream_reloading,ble_link_lost;
static int usb_awake;
static float md_audio[256],md_gain;static uint32_t midi_link;
typedef struct { uint32_t magic; int32_t motion,boot_sound,button_glow; uint32_t revision; uint8_t midi_route[6],midi_macro,reserved; md_midi_mapping mapping[16]; } md_preferences;
static md_preferences dream_prefs;
typedef struct {
 uint8_t version, touch, reserved[2], favorites[64];
} md_performance_preferences;
typedef struct { md_preferences base; md_performance_preferences performance; } md_saved_preferences;
static md_performance_preferences performance_prefs;
_Static_assert(sizeof(md_preferences)==156,"Stable v2 preference layout");
_Static_assert(sizeof(md_saved_preferences)==224,"Stable preferences trailer layout");
/* These preference bits sit above all Biome IDs. Older builds ignore them. */
enum { LOCAL_MONITOR_BIT=510, DISPLAY_POLARITY_BIT=511 };
_Static_assert(MD_BIOMES<=LOCAL_MONITOR_BIT,"Preference bits must not overlap Biome IDs");
static volatile uint8_t local_muted;
int app_local_monitor_muted(void){return local_muted;}
static void set_local_monitor(int mute){
 uint8_t *flags=&performance_prefs.favorites[LOCAL_MONITOR_BIT/8];
 const uint8_t mask=1u<<(LOCAL_MONITOR_BIT%8);
 *flags=mute?*flags|mask:*flags&~mask;
 local_muted=mute!=0;
}
static int dream_inverted(void){return (performance_prefs.favorites[DISPLAY_POLARITY_BIT/8]>>(DISPLAY_POLARITY_BIT%8))&1;}
/* The previous reader truncates the record to its own buffer size. Keep its
 * complete 156-byte prefix so it can still read its original preferences. */
static int dream_prefs_decode(md_preferences *out,const void *data,unsigned n){
 if(n==sizeof(md_saved_preferences)){
  md_performance_preferences extra;
  memcpy(&extra,(const uint8_t*)data+sizeof *out,sizeof extra);
  if(extra.version!=1||extra.touch>100||extra.reserved[0]||extra.reserved[1])return 0;
  n=sizeof *out;
 }
 if(n!=12&&n!=16&&n!=28&&n!=sizeof *out)return 0;
 md_preferences p={0};
 memcpy(&p,data,n);
 if(p.magic!=0x4d445046||p.motion<0||p.motion>1||p.boot_sound<0||p.boot_sound>1||(n>=16&&(p.button_glow<0||p.button_glow>2)))return 0;
 if(n>=28){if(p.revision!=(n==28?1:2)||p.midi_macro>1||p.reserved>3)return 0;for(int i=0;i<6;i++)if(p.midi_route[i]>16)return 0;}
 else {for(int i=0;i<6;i++)p.midi_route[i]=i+1;p.midi_macro=1;if(n==12)p.button_glow=1;}
 if(n==sizeof p)for(int i=0;i<16;i++)if(!md_mapping_valid(&p.mapping[i]))return 0;
 p.revision=2;*out=p;return 1;
}
static void dream_sync_mappings(void){uint32_t f=plat_lock();for(int i=0;i<16;i++)dream_prefs.mapping[i]=*md_mapping(i);plat_unlock(f);}
static int dream_save_preferences(void){
 md_saved_preferences saved={dream_prefs,performance_prefs};
 unsigned used=performance_prefs.touch;
 for(unsigned i=0;i<sizeof performance_prefs.favorites;i++)used|=performance_prefs.favorites[i];
 return plat_store_save(OBJ_PREFS,&saved,used?sizeof saved:sizeof saved.base);
}
int md_ble_at_boot(void){return (dream_prefs.reserved&1)!=0;}
/* A BLE central scan occupies the same radio that advertises the FM-1 to a phone.
 * Search only when requested, so leaving this page open does not hide us. */
static uint32_t bt_scan_until;
static uint32_t intro_start,chime_left;
static uint16_t chime_gain;
static volatile uint8_t chime_release;
static int intro_pending,intro_active;static float chime_phase[3];
static void dream_intro(void){
 intro_start=plat_ms();intro_active=1;
 uint32_t lock=plat_lock();
 chime_left=dream_prefs.boot_sound?88200:0;chime_gain=128;chime_release=0;
 for(int i=0;i<3;i++)chime_phase[i]=0;
 plat_unlock(lock);
}
static void dream_dismiss_intro(void){intro_active=0;chime_release=1;}
static uint32_t overloads,cpu_peak,flower_time,last_events[6],pulse_start[6],pulse_active,quality_notice;
static uint32_t ui_frames,ui_frame_peak_ms;
enum { DISPLAY_TILES = 240u / DREAM_TILE_ROWS };
static uint32_t display_hashes[DISPLAY_TILES];
static uint32_t display_tiles_sent,display_tiles_skipped;
static uint8_t display_repaint=1;
static void toggle_display_polarity(void){
 performance_prefs.favorites[DISPLAY_POLARITY_BIT/8]^=1u<<(DISPLAY_POLARITY_BIT%8);
 app_redraw();
}
static uint32_t display_static_frame[3];
_Static_assert(240u % DREAM_TILE_ROWS == 0,"Display tiles must cover the screen");
static const uint16_t dream_colors[6]={RGB(174,207,182),RGB(220,198,148),RGB(134,194,180),RGB(165,190,218),RGB(200,177,216),RGB(204,160,145)};
/* A saved Biome already contains its visual seed. No new RAM or file field. */
static uint32_t md_scene_dna(uint32_t seed,int biome){uint32_t x=seed^((uint32_t)biome+1u)*0x9e3779b9u;x^=x>>16;x*=0x85ebca6bu;return x^(x>>13);}
static uint32_t md_world_dna(void){const md_patch *p=md_state();return md_scene_dna(p->seed,p->biome_id);}
// Stable track identities, separate from each page's four control colours.
static const uint16_t dream_palettes[11][4]={
 {RGB(174,207,182),RGB(156,193,199),RGB(216,199,159),RGB(192,181,216)}, // moss / pearl
 {RGB(182,204,161),RGB(219,197,149),RGB(145,190,170),RGB(181,191,215)}, // reed / linen
 {RGB(139,199,194),RGB(158,190,220),RGB(186,176,215),RGB(204,213,182)}, // lagoon / mist
 {RGB(200,181,221),RGB(152,194,211),RGB(206,195,160),RGB(162,206,185)}, // dusk / iris
 {RGB(224,200,150),RGB(160,194,172),RGB(181,188,217),RGB(199,167,181)}, // honey / stone
 {RGB(167,195,214),RGB(187,205,181),RGB(199,184,210),RGB(216,205,170)}, // slate / moon
 {RGB(215,190,159),RGB(147,193,180),RGB(191,178,209),RGB(176,197,217)}, // parchment / jade
 {RGB(78,234,200),RGB(99,190,245),RGB(233,221,164),RGB(201,150,231)},  // living current
 {RGB(152,214,255),RGB(180,172,245),RGB(171,239,224),RGB(237,224,192)},// frost / air
 {RGB(247,203,118),RGB(199,234,155),RGB(237,176,178),RGB(166,216,218)},// hearth / amber
 {RGB(179,229,149),RGB(113,226,202),RGB(194,180,239),RGB(242,218,153)} // luminous moss
};
static const uint8_t dream_page_palette[DREAM_PAGE_COUNT]={0,1,4,2,0,5,1,4,2,3,1,0,0,4,2,3,4,5,5,4,4,3,2,6,6,2,4,4,3,3,2,7,3,10,8,5,10,7,3,8,4,3,4,2,10};
static const uint16_t *md_palette(void){
 unsigned palette=dream_ui.page==LIVE?(md_world_dna()>>9)%11u:dream_page_palette[dream_ui.page];
 return dream_palettes[palette];
}
static uint16_t md_control_ink(int x,int y,uint16_t fallback){return (y==158||y==194)&&(x==8||x==124)?md_palette()[(y==194?2:0)+(x==124)]:fallback;}
#define MD_BG RGB(4,8,13)
#define MD_CARD RGB(13,24,33)
#define MD_INK RGB(255,252,244)
#define MD_DIM RGB(226,236,238)
#define MD_EDGE RGB(106,135,145)
static const char *dream_pages[]={"LIVE ATLAS / 2.0","SOUND","RHYTHM","GRANULAR BUS","MIX / NOTES","QUALITY / SYSTEM","ENVELOPE / BLOOM","GROOVE / REPEATS","TAPE / MOTION","HARMONY / GLOBAL","VELVET FILTER","LOOK / STARTUP","GLOBAL MIXER","BASS / MOTIFS","SHARED SPACE","MARBLES / TIDES / WARPS","PHRASE / NOTES","MIDI / PERFORMANCE","USER BANKS","SEQUENCE / 32 STEPS","STEP / EXPRESSION","CHORD JOURNEY","CLOUD / AIR","LO-FI / RACK MODE","KIT / PAD COLOUR","KIT / SENDS","KIT / 32 STEPS","SEQUENCE / FLOW","TIDAL / LFO A","TIDAL / LFO B","WIRELESS MIDI","JOURNEY / ARRANGE","JOURNEY / TIMING","JOURNEY / ANCHORS","MIDI / LEARN","MIDI / MAPPING","LIVING / LINKS","LINK / MOTION","BIOME / DRIFT","DRIFT / SCENES","SEQUENCER / GRID","SEQUENCER / TOOLS","SEQUENCER / FILL","USB AUDIO","ITINERA / BIOME RADIO"};
static const char *dream_headings[]={"MUTABLE DREAMS","VOICE SHAPING","RHYTHM","GRANULAR CLOUD","NOTES & SEND","PERFORMANCE","BLOOM & DECAY","GROOVE","TAPE MOTION","HARMONY","VELVET FILTER","LIVING DISPLAY","MIXER","BASS & MOTIFS","SHARED SPACE","MODULATION","PHRASE","MIDI CONTROL","USER LIBRARY","YOUR SEQUENCE","STEP EXPRESSION","CHORD JOURNEY","AIR & TEXTURE","LO-FI RACK","PAD COLOUR","KIT SENDS","KIT SEQUENCE","MELODIC FLOW","TIDAL MOTION A","TIDAL MOTION B","WIRELESS MIDI","YOUR JOURNEY","TIME TO UNFOLD","KEEP A THREAD","YOUR CONTROLLERS","MAPPING DETAILS","LET TRACKS RESPOND","SHAPE THE CONNECTION","BETWEEN TWO WORLDS","CAPTURE THE ENDPOINTS","SIX TRACKS","PHRASE TOOLS","STEP RULES","DAW MONITORING","ITINERA"};
static const char *help_title[NB]={"GRANULAR BUS","MUTE A TRACK","RETURN","FREEZE","CHOOSE A VOICE","SYSTEM","LIVE MACROS","SAVE","LOCK THE SEED","GENERATE RHYTHMS","PLAY / STOP","MUTATE","LOWER OCTAVE","HIGHER OCTAVE"};
static const char *help_lines[NB][5]={
 {"ONE SHARED BUS / ALL TRACKS","K1 Mix / K2 Grain length","K3 Position / K4 Feedback","Track sends: MIXER, K2","FX again: Tape / Space / Air"},
 {"SEL: mute / unmute this track","SELECT: choose an active track","ALGO: change sound engine","Every engine fits every track.","EDIT + PRESET: sound library"},
 {"ENV: undo the last variation","REC: make a new variation","One previous variation is kept.","Choosing a Biome starts fresh.","SAVE: store the current patch"},
 {"LFO: freeze the granular memory","The cloud keeps playing.","Tracks remain live and playable.","LFO again: resume recording","FX: edit the cloud and tape"},
 {"EDIT: Sound/Filter/Env/Mix/Mixer","PRESET: 452 sound presets","ALGO: twenty sound engines","Rhythm and notes are preserved.","HOME + PRESET: Biome path"},
 {"GLO: Harmony/System/Look/MIDI/Banks","Harmony: root / scale / offset","System: tempo / quality","Look: motion / sound / LEDs","SAVE stores these preferences."},
 {"K1 DENSITY: number of events","K2 MOTION: melodic movement","K3 COLOUR: brighter / darker","K4 SPACE: granular level","PRESET: browse factory Biomes"},
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
        ((p==MIDI||p==BANKS||p==USB_AUDIO)&&b==B_GLO)||(p==BANKS&&b==B_SAVE)||((p==SEQUENCE||p==STEP_DETAIL||p==SEQ_FLOW)&&(b==B_SEQ||b==B_REC))||(p==PROGRESSION&&b==B_GLO)||(p==CLOUD&&b==B_FX)||((p>=JOURNEY&&p<=JOURNEY_KEEP)&&b==B_SEQ)||((p==MIDI_LEARN||p==MIDI_MAP)&&(b==B_GLO||b==B_REC))||((p>=LIVING_LINKS&&p<=DRIFT_SCENES)&&(b==B_FX||b==B_REC))||
        ((p==SEQ_GRID||p==SEQ_TOOLS||p==SEQ_RULES)&&(b==B_SEQ||b==B_SEL||b==B_REC||b==B_ENV));
}
static int dream_cmd(int c,int t,int v){if(c==MD_MIDI_ROUTE)v=clamp(v,0,16);if(c==MD_MIDI_MACRO)v=clamp(v,0,1);uint32_t f=plat_lock();int r=md_command(c,t,v);plat_unlock(f);if(!r&&md_journey_active())app_say("STOP JOURNEY TO EDIT","");if(r&&c==MD_MIDI_ROUTE&&t>=0&&t<6)dream_prefs.midi_route[t]=v;if(r&&c==MD_MIDI_MACRO)dream_prefs.midi_macro=v;return r;}
static int dream_install(const void *,unsigned);
static int dream_install_edit(const void *,unsigned);
#include "banks.c"
static char *md_num(int v,char *s) {char b[12];int i=0;if(v<0){*s++='-';v=-v;}do{b[i++]='0'+v%10;v/=10;}while(v);while(i)*s++=b[--i];*s=0;return s;}
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
 if(dream_ui.page==LIVE&&x==112&&y>=44&&y<140&&md_state()->track[(y-44)/16].mute)width=94;
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
// Z-3: the same pixels as the per-pixel test (q <= r*r, and q >= (r-1)^2 for a
// ring), found row by row with an integer square root and filled as spans.
static int md_isqrt(int v){unsigned r=0,b=1u<<30,x=(unsigned)v;while(b>x)b>>=2;while(b){if(x>=r+b){x-=r+b;r=(r>>1)+b;}else r>>=1;b>>=2;}return (int)r;}
static void md_span(int x,int y,int lo,int hi,int a0,int a1,uint16_t c){if(lo<a0)lo=a0;if(hi>a1)hi=a1;if(lo<=hi)cv_rect(x+lo,y,hi-lo+1,1,c);}
static void md_circle(int x,int y,int r,uint16_t c,int fill) {
 // Reject rows outside the active tile before walking the circle's rows.
 int a0=-r,a1=r,b0=-r,b1=r;
 if(a0< -x)a0=-x;if(a1>=(int)cv_w-x)a1=(int)cv_w-x-1;
 if(b0< -y-cv_oy)b0=-y-cv_oy;if(b1>=(int)cv_h-y-cv_oy)b1=(int)cv_h-y-cv_oy-1;
 for(int b=b0;b<=b1;b++){
  const int out=r*r-b*b;if(out<0)continue;
  const int amax=md_isqrt(out);
  const int in=(r-1)*(r-1)-b*b;
  if(fill||in<=0){md_span(x,y+b,-amax,amax,a0,a1,c);continue;}
  const int amin=md_isqrt(in-1)+1;
  if(amin>amax)continue;
  md_span(x,y+b,-amax,-amin,a0,a1,c);md_span(x,y+b,amin,amax,a0,a1,c);
 }
}
// Subpixel coverage for our botanical marks. Audio never calls this renderer.
static uint16_t md_tint(uint16_t,uint16_t,unsigned);
// Four-bit subpixel coverage. Integer blending keeps detailed paths inexpensive.
static void md_aa_point(int x,int y,uint16_t ink,unsigned strength){
 int ix=x>>4,iy=y>>4,fx=x&15,fy=y&15;
 for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++){
  int px=ix+dx,py=iy+dy+cv_oy;if(px<0||px>=(int)cv_w||py<0||py>=(int)cv_h)continue;
  unsigned mix=((dx?fx:16-fx)*(dy?fy:16-fy)*strength+128)>>8;
  if(mix){uint16_t old=swap16(cv_px[py*cv_w+px]);cv_px[py*cv_w+px]=swap16(md_tint(old,ink,mix));}
 }
}
static void md_soft_point(float x,float y,uint16_t ink){md_aa_point((int)(x*16),(int)(y*16),ink,32);}
static void md_fine_line(int x,int y,int nx,int ny,uint16_t ink){
 if((y/16+cv_oy<-2&&ny/16+cv_oy<-2)||(y/16+cv_oy>=(int)cv_h&&ny/16+cv_oy>=(int)cv_h))return;
 int dx=nx-x,dy=ny-y,ax=dx<0?-dx:dx,ay=dy<0?-dy:dy,n=((ax>ay?ax:ay)+15)/16;if(n<1)n=1;
 for(int k=0;k<=n;k++)md_aa_point(x+dx*k/n,y+dy*k/n,ink,32);
}
// Full 72-point leaves. Signed ten-bit offsets keep the original Q4 drawing
// precision in three bytes per point, rather than four absolute-coordinate bytes.
static struct { uint8_t x,y,high; } bloom_leaf[6][72];
typedef char bloom_leaf_uses_three_bytes[(sizeof bloom_leaf[0][0]==3)?1:-1];
static uint8_t bloom_cx[6],bloom_cy[6];
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
 case SEQ_FLOW:case SEQUENCE:case STEP_DETAIL:{int notes=0;for(int i=0;i<q->length;i++)notes+=q->steps[i].note!=0;d=q->enabled?notes*1000/q->length:0;s=q->length*1000/32;l=q->steps[q->edit].velocity*10;m=md_step_chance(&q->steps[q->edit])*10;break;}
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
 const int tracks=md_active_tracks();
 const unsigned family=md_world_dna()%10u;
 const int branching=family==2||family==4||family==8;
 const unsigned lobes=family==6?4:family==9?2:3;
 const float step_sin=fm_sinf(.0872664626f),step_cos=fm_cosf(.0872664626f);
 const float lobe_sin=fm_sinf(lobes*.0872664626f),lobe_cos=fm_cosf(lobes*.0872664626f);
 for(int j=0;j<tracks;j++){
  float a=j*(6.2831853f/tracks)-.52f+flower_time*.000025f;
  int orbit=16+field_density*8/1000;
  float centre_x=51+fm_cosf(a)*orbit,centre_y=95+fm_sinf(a)*orbit;
  if(branching){
   int levels=(tracks+1)/2;
   centre_x=51+((j&1)?1:-1)*(13+field_density*5/1000);
   centre_y=levels>1?66+(j/2)*52.f/(levels-1):95;
   a=(j&1)?-.65f:.65f;
  }else if(family==3||family==7){
   const float across=(float)j/(tracks-1);
   centre_x=24+54*across;
   centre_y=family==3?91.f+18.f*fm_sinf(a*.68f):66.f+51.f*across;
   a=family==3?.50f:-.65f;
  }else if(family==1||family==9){
   const float across=(float)j/(tracks-1)-.5f;
   centre_x=50+across*54;
   centre_y=73+across*across*115+(j&1)*9;
   a=1.2f+across;
  }
  float energy=dream_prefs.motion&&!md_state()->track[j].mute?clamp((int)(md_status()->track_peak[j]*60),0,4):0;
  float stretch=branching?1.12f:family==3?1.12f:1.f;
  float rise=branching?.76f:family==3?.80f:1.f;
  float ca=fm_cosf(branching?a:a*.35f),sa=fm_sinf(branching?a:a*.35f);
  bloom_cx[j]=(uint8_t)centre_x;bloom_cy[j]=(uint8_t)centre_y;
  int cx=bloom_cx[j]*16,cy=bloom_cy[j]*16;
  // Walk the contour by rotation. The voice's start angle is the only
  // one needed for all 72 vertices.
  const float angle_sin=fm_sinf(a),angle_cos=fm_cosf(a);
  float sb=0,cb=1,ls=angle_sin,lc=angle_cos;
  for(int k=0;k<72;k++){
   float r=10+field_size*.006f+3*ls+2*(cb*angle_cos-sb*angle_sin)+energy;
   float x=cb*r*stretch,y=sb*r*rise;
   int qx=clamp((int)((centre_x+x*ca-y*sa)*16)-cx,-512,511);
   int qy=clamp((int)((centre_y+x*sa+y*ca)*16)-cy,-512,511);
   bloom_leaf[j][k].x=(uint8_t)qx;bloom_leaf[j][k].y=(uint8_t)qy;
   bloom_leaf[j][k].high=(uint8_t)((((unsigned)qx>>8)&3)|((((unsigned)qy>>8)&3)<<2));
   const float next_b=sb*step_cos+cb*step_sin,next_l=ls*lobe_cos+lc*lobe_sin;
   cb=cb*step_cos-sb*step_sin;sb=next_b;
   lc=lc*lobe_cos-ls*lobe_sin;ls=next_l;
  }
 }
}
static void md_leaf_point(unsigned voice,unsigned at,int *x,int *y){
 unsigned high=bloom_leaf[voice][at].high;
 int dx=bloom_leaf[voice][at].x|((high&3u)<<8),dy=bloom_leaf[voice][at].y|((high&12u)<<6);
 if(dx&512)dx-=1024;if(dy&512)dy-=1024;
 *x=(int)bloom_cx[voice]*16+dx;*y=(int)bloom_cy[voice]*16+dy;
}
// RGB565 translucency over the current tile: no second framebuffer.
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
   cv_px[py*cv_w+px]=swap16(md_tint(old,md_tint(MD_CARD,c,1+row/16),21));
  }
  if(a<=z){cv_pset(a,y+row,md_tint(MD_CARD,c,15));cv_pset(z,y+row,md_tint(MD_CARD,c,15));}
 }
 // Asymmetric leaf tips and a restrained colour vein leave the field visible.
 cv_rect(x+12,y,92,1,md_tint(MD_CARD,c,14));cv_rect(x+4,y+31,92,1,md_tint(MD_CARD,c,14));
 for(int j=0;j<17;j++)cv_pset(x+13+j,y,md_tint(MD_EDGE,c,12));
}
static void md_leaf_slider(int x,int y,int width,int value,int lo,int hi,uint16_t c){
 int fill=clamp(value-lo,0,hi-lo)*width/(hi>lo?hi-lo:1);
 for(int j=0;j<=width;j++){
  int bend=j*(width-j)*3/(width*width/4+1);
  cv_pset(x+j,y+bend,j<=fill?md_tint(c,MD_INK,(unsigned)(j*8/width)):MD_EDGE);
  if(j<=fill)cv_pset(x+j,y+bend+1,md_tint(MD_BG,c,13));
  if(j&&j%14==0)cv_pset(x+j,y+bend+3,md_tint(MD_CARD,c,9));
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
// A compact cached one-shot A/D contour. MIDI gate hold/release is separate.
static uint8_t env_plot[180];static int env_attack=-1,env_decay=-1;
static void md_envelope_plot(const md_track *t,uint16_t col){
 if(env_attack!=t->attack||env_decay!=t->decay){env_attack=t->attack;env_decay=t->decay;
  float a=.002f+8.f*t->attack*t->attack/1000000.f,d=.06f+16.f*t->decay*t->decay/1000000.f,total=a+3*d;
  for(int j=0;j<180;j++){float time=total*j/179.f,z=(time-a)/d;float level=time<a?time/a:1.f/(1+z+.48f*z*z+.235f*z*z*z);env_plot[j]=(uint8_t)(132-43*level);}
 }
 for(int j=0;j<180;j++){int y=env_plot[j];if(y+cv_oy<-3||y+cv_oy>=(int)cv_h)continue;
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

// Two signed 12-bit offsets preserve the full Q4 geometry in three bytes.
static struct {uint8_t x,y,high;} organic_path[5][96];
_Static_assert(sizeof organic_path[0][0]==3,"Contour vertex layout");
static int contour_offset(unsigned raw){return (int)(raw&2047u)-(int)(raw&2048u);}
static int contour_x(int path,int k){return 166*16+contour_offset(organic_path[path][k].x|((organic_path[path][k].high&15u)<<8));}
static int contour_y(int path,int k){return 99*16+contour_offset(organic_path[path][k].y|((organic_path[path][k].high&240u)<<4));}
enum { ORGANIC_FLOW_ROWS=5 };
static uint8_t organic_flow[ORGANIC_FLOW_ROWS][120];
static int organic_page=-1,organic_scene=-2;
// Scanline silhouettes fit in 384 bytes, not an extra framebuffer.
static uint8_t landscape_x[96],landscape_width[96];static uint16_t landscape_ink[24];
static int md_landscape(int page){
 if(page==KIT_SEQ)return 2;
 if(page==KIT_SEND)return 0;
 if(page==KIT||page==KIT_PAD)return 4;
 if(page==GRAIN||page==TAPE||page==SPACE||page==CLOUD)return 0;
 if(page==HARMONY||page==PROGRESSION||page==MODULATION||page==LFO_ROUTE||page==LFO_ROUTE2||page==MIDI)return 1;
 if(page==RHYTHM||page==GROOVE||page==BASSLINE||page==PHRASE||page==SEQUENCE||page==STEP_DETAIL||page==SEQ_FLOW||page==SEQ_GRID||page==SEQ_TOOLS||page==SEQ_RULES)return 2;
 return 3;
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
#include "organic_atlas.c"
#include "atlas_marks.c"
#include "biome_browser.c"
#include "living_views.c"
#include "mix_view.c"
#include "journey_timeline.c"
#include "sequence_view.c"
#include "itinera_view.c"
static void draw_legacy_plant(const md_patch *p){
 if(-cv_oy>=145||-cv_oy+(int)cv_h<=42)return;
  md_bloom_cache();
  unsigned family=md_world_dna()%10u;
  if(family==2||family==4||family==8){
   int px=51*16,py=133*16;uint16_t stem=md_tint(MD_BG,md_palette()[0],22);
   for(int k=1;k<=16;k++){int ny=(133-k*5)*16,nx=51*16+md_sway((unsigned)(k*2)+(dream_prefs.motion?field_time/200:0))*2;md_fine_line(px,py,nx,ny,stem);px=nx;py=ny;}
   for(int i=0;i<md_active_tracks();i++)md_fine_line(51*16,((int)bloom_cy[i]+6)*16,(int)bloom_cx[i]*16,(int)bloom_cy[i]*16,md_tint(MD_BG,dream_colors[i],16));
  }
  for(int i=0;i<md_active_tracks();i++){int x=(int)bloom_cx[i],y=(int)bloom_cy[i];
   int muted=p->track[i].mute;uint16_t quiet=RGB(112,130,137);
   // Layered botanical voices remain the instrument's central visual identity.
   uint16_t leaf=muted?md_tint(MD_BG,quiet,14):dream_colors[i];
   if(!muted)md_circle(x,y,10+field_size/180,md_tint(MD_BG,leaf,i==dream_ui.track?11:5),1);
   for(int k=0;k<72;k++){
    int px,py,nx,ny;md_leaf_point(i,k,&px,&py);md_leaf_point(i,(k+1)%72,&nx,&ny);
    md_fine_line(px,py,nx,ny,leaf);
    if(!muted&&i==dream_ui.track)md_fine_line(x*16+(px-x*16)*3/4,y*16+(py-y*16)*3/4,x*16+(nx-x*16)*3/4,y*16+(ny-y*16)*3/4,md_tint(MD_BG,leaf,13));
    if(!muted&&k%24==0)md_fine_line(x*16,y*16,px,py,md_tint(MD_BG,leaf,9));
   }
   md_circle(x,y,4,muted?quiet:dream_colors[i],!muted&&i==dream_ui.track);
   if(!muted&&dream_prefs.motion&&(pulse_active&(1u<<i))&&(uint32_t)(plat_ms()-pulse_start[i])<160)md_circle(x,y,4,MD_INK,1);
 }
 md_circle(51,95,5,MD_INK,0);
}
static void md_draw_all(void) {
 if(dream_ui.page==ITINERA){radio_draw();return;}
 const md_patch *p=md_state();const md_track *t=&p->track[dream_ui.track];uint16_t col=md_palette()[0];char s[24];int i;
 const int static_view=display_static_frame[0]!=0;
 if(static_view||(dream_ui.page==LIVE&&dream_ui.live_view!=LIVE_ATLAS&&dream_ui.help<0&&
    !intro_active&&md_pending_biome()<0&&bank_pending<0)){
  if(static_view)col=display_static_frame[0]>>8;
  if(dream_ui.live_view==LIVE_BROWSER){draw_biome_browser(col);return;}
  const uint32_t state=display_static_frame[1];
  const int kit=static_view?(state>>11)&1:p->kit.enabled;
  const int playing=static_view?(state>>10)&1:md_playing();
  cv_text(12,7,&FONT_M,"TRACKS",MD_INK);
  cv_text(163,14,&FONT_T,playing?"PLAYING":"STOPPED",col);
  cv_rect(9,37,222,1,col);
  for(i=0;i<(kit?4:6);i++){
   int y=44+i*28,off=static_view?(state>>(12+i))&1:p->track[i].mute;
   uint16_t ink=off?RGB(154,171,174):MD_INK;
   int model=static_view?(display_static_frame[2]>>(5*i))&31:p->track[i].model;
   const char *name=kit&&i==3?"LO-FI KIT":md_model_name(model);
   if(i==dream_ui.track){cv_rect(8,y,224,26,MD_CARD);cv_rect(8,y,3,26,col);}
   md_num(i+1,s);cv_text(15,y+2,&FONT_B,s,dream_colors[i]);
   cv_text(37,y+2,text_w(&FONT_B,name)>150?&FONT_H:&FONT_B,name,ink);
   cv_text(193,y+2,&FONT_B,off?"OFF":"ON",off?ink:dream_colors[i]);
  }
  cv_text(11,212,&FONT_XS,"K4 TOUCH",MD_DIM);
  md_num(performance_prefs.touch,s);
  cv_text(74,212,&FONT_XS,s,col);
  cv_text(11,225,&FONT_XS,"HOME Biomes / GLO User Banks",MD_INK);
  return;
 }
 if(!intro_active&&-cv_oy+(int)cv_h>39&&-cv_oy<224){
  int page=dream_ui.page,home=page==LIVE;
  if((home&&dream_ui.help<0)||page==GRAIN||page==MIXER||page==SPACE||
     page==SEQ_GRID||page==SEQ_TOOLS||page==SEQ_RULES){
   unsigned dna=home?md_world_dna():0;
   md_atlas_currents(home,home?dna%5:md_landscape(page),field_time/52+(home?dna&63:page*9));
  }else md_organic();
 }
 cv_rect(0,0,240,39,MD_BG);
 // A fine, page-coloured aurora at the margin: integer geometry only, above
 // the living field and below the text. It makes each page identifiable.
 for(int j=0;j<4;j++){
  uint16_t aurora=md_tint(MD_BG,md_palette()[j],dream_ui.page==LIVE&&p->biome_id>=157?25:16);
  cv_rect(10+j*55,37,55,1,md_tint(MD_BG,aurora,14));
  cv_rect(10+j*55,38,55,1,aurora);
 }
 cv_rect(10,38,28,1,col);
 cv_rect(0,224,240,16,MD_BG);
 if(dream_ui.page==LIVE&&md_journey_active()){
  char title[]="JOURNEY 1 / 8";title[8]='1'+md_journey_position();title[12]='0'+p->journey.length;cv_text(10,5,&FONT_B,title,MD_INK);
 }else{const char *heading=intro_active?"MUTABLE DREAMS":dream_headings[dream_ui.page];cv_text(10,5,text_w(&FONT_B,heading)>204?&FONT_H:&FONT_B,heading,MD_INK);}
 uint16_t signal=md_playing()?dream_colors[2]:MD_DIM;
 if(-cv_oy<22){
  static const int8_t orbit[6][2]={{0,-8},{7,-4},{7,4},{0,8},{-7,4},{-7,-4}};
  md_circle(226,12,4,signal,0);md_circle(226,12,1,signal,1);
  for(unsigned track=0;track<6;track++){
   const int x=226+orbit[track][0],y=12+orbit[track][1];
   unsigned hit=dream_prefs.motion&&(pulse_active&(1u<<track))&&!p->track[track].mute&&
                (dream_ui.page!=LIVE||dream_ui.live_view==LIVE_ATLAS);
   cv_rect(x,y,hit?2:1,hit?2:1,hit?dream_colors[track]:MD_EDGE);
  }
 }
 md_text(10,25,dream_pages[dream_ui.page],md_tint(col,MD_INK,8));md_num(md_tempo_now(),s);md_text(186,25,s,MD_INK);md_text(211,25,"BPM",MD_DIM);
 if(-cv_oy+(int)cv_h<=39)return;
 if(intro_active&&(uint32_t)(plat_ms()-intro_start)<3200&&dream_ui.help<0){
  unsigned ms=plat_ms()-intro_start;
  int open=dream_prefs.motion?clamp((int)(ms/13),18,100):100;
  atlas_plate(50,47,140,105,col);
  atlas_brand_mark(98,open);
  cv_text((240-text_w(&FONT_M,"2.0"))/2,152,&FONT_M,"2.0",MD_INK);
  md_text(59,180,"THE LIVING ATLAS",col);
  char *caption=md_num(MD_BIOMES-1,s);
  memcpy(caption," BIOMES / ",10);
  caption=md_num(MD_TONE_PRESETS,caption+10);
  memcpy(caption," TONES",7);
  md_text((240-text_w(&FONT_H,s))/2,199,s,MD_DIM);
  cv_rect(26,219,188,2,MD_EDGE);cv_rect(26,219,188*ms/3200,2,col);md_text(48,223,"ANY CONTROL TO BEGIN",MD_DIM);return;
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
  cv_text(17,48,&FONT_T,"YOUR POCKET FIELD GUIDE",MD_DIM);
  md_microbe(214,54,5,1,dream_prefs.motion?field_time/85:0,col);
  for(int seed=0;seed<5;seed++){
   int sway=dream_prefs.motion?md_sway(field_time/110+seed*9)/9:0;
   md_circle(218+sway,108+seed*19,seed==2?2:1,md_tint(MD_CARD,col,12+seed*3),1);
  }
  int contextual=md_context_help()||(dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)||
   (dream_ui.help==B_EDIT&&(dream_ui.page==SOUND||dream_ui.page==MIX||dream_ui.page==FILTER||dream_ui.page==ENVELOPE))||
   (dream_ui.help==B_GLO&&(dream_ui.page==HARMONY||dream_ui.page==SYSTEM||dream_ui.page==APPEARANCE||dream_ui.page==BT_PAGE))||
   (dream_ui.help==B_SEQ&&dream_ui.page==GROOVE)||
   (dream_ui.help==B_FX&&dream_ui.page==TAPE);
  const char *title=contextual?dream_pages[dream_ui.page]:help_title[dream_ui.help];
  cv_text(16,64,text_w(&FONT_B,title)>207?&FONT_XS:&FONT_B,title,col);
  cv_rect(16,84,208,1,MD_EDGE);
  const char *const *lines=help_lines[dream_ui.help];
  static const char *soundhelp[]={"PRESET: sound / ALGO: engine","K1: Tone / brightness","K2: Harmonics / chord","K3: Shape; drums use Decay","K4: Level / EDIT: Filter"};
  static const char *cedarhelp[]={"K1: string brightness","K2: wooden body weight","K3: soft / crisp plectrum","Envelope: attack and tail","K4: level / EDIT: filter"};
  static const char *bowhelp[]={"K1: warm string colour","K2: fundamental / body","K3: continuous bow pressure","Envelope: slow swell / release","K4: level / EDIT: filter"};
  static const char *aerohelp[]={"K1: open the pipe overtones","K2: rounded reed colour","K3: airy breath content","Envelope: swell and release","K4: level / EDIT: filter"};
  static const char *silkhelp[]={"K1: odd overtone colour","K2: spread the two side voices","K3: slow drift depth","Envelope: swell and release","K4: level / EDIT: filter"};
  static const char *copperhelp[]={"K1: level of ring sidebands","K2: shift their interval","K3: strength of the strike","Envelope: body and release","K4: level / EDIT: filter"};
  static const char *vowelhelp[]={"K1: move from oo to ah to ee","K2: focus the upper partials","K3: vowel swell after attack","BREATHE adds slow vowel drift","K4: level / EDIT: filter"};
  static const char *felthelp[]={"K1: body brightness","K2: strike overtone pitch","K3: felt to hard mallet","Tracks / Touch: MIDI response","K4: level / EDIT: filter"};
  static const char *skinhelp[]={"K1: touch / skin brightness","K2: palm to rim overtones","K3: natural drum decay","Velocity: softer / harder hit","K4: level / MIX: cloud send"};
  static const char *reedhelp[]={"K1: open the upper drawbars","K2: pipe / reed harmonics","K3: slow, gentle beating","Envelope: swell and release","K4: level / EDIT: filter"};
  static const char *elementhelp[]={"PRESET: sound / ALGO: engine","K1 strike colour / K2 geometry","K3 Mallet / Plectrum / Particles","ENVELOPE: resonator decay","K4 level / EDIT: filter"};
  static const char *mixhelp[]={"K1: send to granular bus","K2: track base note","K3: pan left / right","K4: clock division, 1 to 8","PRESET: sound / SEL: mute"};
  static const char *lookhelp[]={"K1: screen + light movement","K2: startup sound on / off","K3: replay startup animation","K4: button LEDs off / dim / flow","SEL: invert / Stop + SAVE: keep"};
  static const char *systemhelp[]={"K1: Tempo / K2: ECO or FULL","K3: ducking recovery","K4: reset measured CPU peak","REC: panic / clear tape","GLO: animation and startup"};
  static const char *harmonyhelp[]={"K1: root note C to B","K2: one of thirteen scales","K3: transpose +/-24 semitones","K4: tempo for all tracks","GLO: continue to System"};
  static const char *filterhelp[]={"K1: cutoff, 60 to 18000 Hz","K2: resonance at the cutoff","K3: OFF / LP12 / LP24 / LPG","K4: slow filter movement","LPG darkens with the decay"};
  static const char *envhelp[]={"K1: attack up to 8 seconds","K2: decay up to 16 seconds","Melodic voices can hold MIDI","K3: level / K4: grain send","Percussion: engine-shaped tail"};
  static const char *groovehelp[]={"K1: global swing (0-60)","K2: 1-4 repeats per hit","K3: accent / K4: clock division","Repeats divide one step.","SEQ: return to hits / length"};
  static const char *tapehelp[]={"K1: grain pitch +/-24","K2: forward / reverse","K3: duck source; 0 means off","K4: attenuate other tracks","The source stays unchanged."};
  static const char *bthelp[]={"K1: Bluetooth MIDI on / off","K2: search / choose MIDI device","K3 right: connect / left: let go","K4: MIDI thru to other ports","Search pauses phone discovery"};
  if(dream_ui.help==B_EDIT&&dream_ui.page==SOUND)lines=t->model==MD_ELEMENTS?elementhelp:t->model==MD_CEDAR?cedarhelp:t->model==MD_REED?reedhelp:t->model==MD_SKIN?skinhelp:t->model==MD_BOWED?bowhelp:t->model==MD_AERO?aerohelp:t->model==MD_FELT?felthelp:t->model==MD_SILK?silkhelp:t->model==MD_COPPER?copperhelp:t->model==MD_VOWEL?vowelhelp:soundhelp;
  if(dream_ui.help==B_EDIT&&dream_ui.page==MIX)lines=mixhelp;
  if(dream_ui.help==B_GLO&&dream_ui.page==APPEARANCE)lines=lookhelp;
  if(dream_ui.help==B_GLO&&dream_ui.page==SYSTEM)lines=systemhelp;
  if(dream_ui.help==B_GLO&&dream_ui.page==HARMONY)lines=harmonyhelp;
  if(dream_ui.help==B_EDIT&&dream_ui.page==FILTER)lines=filterhelp;
  if(dream_ui.help==B_EDIT&&dream_ui.page==ENVELOPE)lines=envhelp;
  if(dream_ui.help==B_SEQ&&dream_ui.page==GROOVE)lines=groovehelp;
  if(dream_ui.help==B_FX&&dream_ui.page==TAPE)lines=tapehelp;
  if(dream_ui.help==B_GLO&&dream_ui.page==BT_PAGE)lines=bthelp;
  if(dream_ui.help==B_GLO&&dream_ui.page==USB_AUDIO){
   static const char *audiohelp[]={"K1: local sound on / mute","Speaker and headphones mute","USB stereo keeps recording","MASTER sets the USB level","Stop + SAVE to keep this setting"};
   lines=audiohelp;
  }
  static const char *newhelp[11][5]={
   {"SELECT: track / SEL: mute","K1 level / K2 grain send","K3 delay send / K4 reverb send","FX: SPACE sets effect return","Sends follow track level."},
   {"K1 OFF / EUCLID / STEP","K2 motif / K3 glide / K4 note","SEQ again: edit phrase notes","Marbles varies offbeat notes.","All tracks remain assignable."},
   {"K1 delay time: 40-350 ms","K2 echo feedback / K3 room tail","K4 shared effect return","Mixer: individual track sends","PANIC clears all effect tails."},
   {"K1 Marbles: remembered variation","K2 Tides: modulation depth","K3 Tides cycle / K4 Warps blend","Marbles: EUCLID/STEP phrase mode","FX again: choose target and wave."},
   {"K1 step 1-16 / K2 semitones","-1 is a rest; scale still applies","K3 accent / K4 probability","Bass page: enable this phrase","STEP mode ignores Euclidean gates."},
   {"K1 clock: Internal / USB / TRS / BLE","K2 input scale snap on / off","K3 selected track: Off / CH 1-16","K4 CH16 CC16-19 macro shortcut","Save preferences with SAVE."},
   {"K1: slot / K2: Biome or Tone","K3 load / K4 name cursor","SAVE: save; repeat to overwrite","Stop first; use internal clock","PRESET: edit name character"},
   {"K1 enable / K2 select step","K3 note (23=rest) / K4 length","REC: STEP entry > LIVE > OFF","Play keys or MIDI CH 1-6","RAM only; stop + SAVE to keep"},
   {"K1 velocity / K2 gate length","K3 chance for this step","K4: sparse / arp / pulse / drift","Generator replaces this track","ENV undo / REC arms note entry"},
   {"K1 four-chord progression","K2 bars per chord (1-8)","K3 selected track follows","K4 tonic / HARMONY sets scale","Slow chords; no extra voices"},
   {"K1 noise into the SHARED tape","K2 SHARED granular return","K3 SELECTED track grain send","K4 freeze the SHARED memory","Noise and sends stop in freeze"}};
  if(md_context_help()&&dream_ui.page<=CLOUD)lines=newhelp[dream_ui.page-MIXER];
  static const char *kithelp[]={"KIT mode: 3 synths + 1 kit","K1 rack / K2 kit / K3 pad","Keyboard: eight pads repeat","EDIT: tune, level, filter, tail","SEQ again: Journey / REC: pads"};
  static const char *padhelp[]={"K1 pitch, -12 to +12 semitones","K2 pad level / K3 soft filter","K4 sample envelope tail","EDIT again: granular send","Each pad has independent settings."};
  static const char *sendhelp[]={"K1 pad / K2 pad granular send","MIXER T4 sets the kit bus send","K3 kit delay / K4 kit reverb","One shared tape, no new buffer","Freeze holds the shared tape."};
  static const char *seqhelp[]={"K1 step / K2 hit on or off","K3 length / K4 rhythm generator","PRESET pad / REC step-live-off","Eight patterns can play together","SAVE stores the kit in your Biome"};
  if((dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ))lines=dream_ui.page==KIT?kithelp:dream_ui.page==KIT_PAD?padhelp:dream_ui.page==KIT_SEND?sendhelp:seqhelp;
  static const char *flowhelp[]={"K1: forward/reverse/pendulum/orbit/seed","K2: pattern offset; notes stay stored","K3: gentle velocity variation","K4: generate a new phrase; ENV undo","REC records into the sounding step."};
  static const char *tidalhelp[]={"Two slow LFOs for this synth track","K1 target / K2 curve / K3 depth","K4 cycle: 0.5 to 20 seconds","Grain/FX/Warps need their base sends.","Soft Swell never boosts the peak."};
  static const char *journeyhelp[]={"K1 Off / Once / Loop; PLAY starts","K2 section / K3 Biome / K4 length","THIS BIOME uses your saved sounds.","SEQ: Timing, then Anchors","Stop + SAVE: keep the whole Journey"};
  static const char *journeytimehelp[]={"K1 section / K2 bars (1-64)","K3 repeats / K4 pitch +/-24","One bar = 16 sixteenth notes.","All sections use your home tempo.","Stop Journey before editing."};
  static const char *journeykeephelp[]={"K1 section / K2 incoming track","K3 carry its sound and pattern","New root/scale still apply.","Kit changes carry tracks 1-3 only.","Live mutes/macros reset on Stop."};
 if(dream_ui.page>=JOURNEY&&dream_ui.page<=JOURNEY_KEEP)lines=dream_ui.page==JOURNEY?journeyhelp:dream_ui.page==JOURNEY_TIME?journeytimehelp:journeykeephelp;
  if(dream_ui.page==SEQ_FLOW)lines=flowhelp;
  static const char *gridhelp[]={"K1 selects one of six tracks","K2 step / K3 note or kit pad","K4 sets the sequence length","SEL opens track / REC records","SEQ opens phrase tools"};
  static const char *toolhelp[]={"K1: Step or whole Phrase","K2: step / preview section","K3: pitch shift or amount","K4: choose a phrase action","SEL applies / ENV undoes"};
  if(dream_ui.page==SEQ_GRID)lines=gridhelp;
  if(dream_ui.page==SEQ_TOOLS)lines=toolhelp;
  static const char *rulehelp[]={"K1 track / K2 step / K4 note","K3 picks the step rule","First or every second pattern","SEL queues or cancels next bar","Fill steps only play in that bar"};
  if(dream_ui.page==SEQ_RULES)lines=rulehelp;
  if((dream_ui.page==LFO_ROUTE||dream_ui.page==LFO_ROUTE2))lines=tidalhelp;
  static const char *learnhelp[]={"K1 slot / K2 target / K3 track","K4 right: learn the next safe CC","Move past the saved value to pick up.","REC clears this slot; hold for help.","Stop + SAVE stores all 16 mappings."};
  static const char *maphelp[]={"K1 port: USB / BLE / TRS / Any","K2 channel: 0 means Any","K3 CC: reserved controls are skipped","K4 On / Off; REC clears this slot","Stop + SAVE remembers mappings."};
  static const char *linkhelp[]={"K1 link / K2 source track","K3 destination / K4 target","Next FX: signal / depth / release","Zero depth leaves sound unchanged.","REC varies seed; ENV undoes it."};
  static const char *linkmotionhelp[]={"K1 link / K2 Hit, Env, LFO A/B","K3 signed depth / K4 Hit release","LFO rate/curve: source Tidal pages","Muted sources send no motion.","REC varies seed; ENV undoes it."};
  static const char *drifthelp[]={"K1 mode / K2 manual position","K3 round trip: 10-300 seconds","K4 depth; capture both scenes first","FX again: capture your A and B","REC varies seed; ENV undoes it."};
  static const char *scenehelp[]={"Keep Drift OFF; K1 captures A","Change controls; K2 captures B","K3 depth / K4 Off, Manual, Auto","Tone, filter, decay, mix and macros","REC: seed / ENV: undo last capture"};
  if(dream_ui.page==MIDI_LEARN)lines=learnhelp;
  if(dream_ui.page==MIDI_MAP)lines=maphelp;
  if(dream_ui.page==LIVING_LINKS)lines=linkhelp;
  if(dream_ui.page==LINK_MOTION)lines=linkmotionhelp;
  if(dream_ui.page==BIOME_DRIFT)lines=drifthelp;
  if(dream_ui.page==DRIFT_SCENES)lines=scenehelp;
  static const char *bankhelp[]={"K1 slot / K2 Biomes or Sounds","K4 cursor / PRESET changes letter","K3 right: load / SAVE: store","REC: clear slot / SEL: confirm","Stop playback; use internal clock"};
  if(dream_ui.page==BANKS)lines=bankhelp;
  static const char *trackshelp[]={"HOME: Atlas / Tracks / Biomes","SELECT: track / SEL: mute","K4: Touch for Bow / Aero / Felt","GLO from Tracks: User Banks","Stop + SAVE to keep Touch"};
  static const char *browsehelp[]={"K1: Family / Favorites","K2 or PRESET: choose Biome","SEL: load / REC: favorite","Stop + SAVE: keep favorites","HOME: Itinera / GLO: User Banks"};
  if(dream_ui.page==LIVE){
   if(dream_ui.live_view==LIVE_BROWSER)lines=browsehelp;
   else if(dream_ui.help==B_HOME||(dream_ui.live_view==LIVE_TRACKS&&dream_ui.help==B_GLO))lines=trackshelp;
  }
  for(i=0;i<5;i++){
   const felucca_font_t *font=text_w(&FONT_H,lines[i])<=204?&FONT_H:&FONT_XS;
   cv_text(23,91+i*22,font,lines[i],MD_INK);
   md_soft_point(14,97+i*22,col);md_soft_point(15,96+i*22,col);md_soft_point(16,97+i*22,col);
  }
  cv_text(17,208,&FONT_T,"RELEASE, THEN TAP TO RETURN",MD_DIM);return;
 }
  if(dream_ui.page==USB_AUDIO){
  md_text(12,57,"SPEAKER + HEADPHONES",MD_DIM);
  cv_text(12,79,&FONT_B,local_muted?"MUTED":"ON",col);
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
  md_text(12,114,"USB: MIX 1+2 / STEMS 3-8",MD_INK);
#else
  md_text(12,114,"USB: STEREO MIX / 1 + 2",MD_INK);
#endif
  md_text(12,137,"MASTER: USB LEVEL",MD_DIM);
  md_named(8,158,"K1 LOCAL AUDIO",local_muted?"MUTE":"ON",col);
  md_text(12,226,"HOLD GLO: HELP / SAVE: KEEP",MD_DIM);
  return;
 }
 if(dream_ui.page>=MIDI_LEARN&&dream_ui.page<=DRIFT_SCENES){
  if(dream_ui.page<=MIDI_MAP){const md_midi_mapping *m=md_mapping(learn_slot);static const char *ports[]={"USB","BLE","TRS","ANY"};
   md_text(12,49,"CONTROLLER",MD_DIM);md_num(learn_slot+1,s);md_text(125,49,s,col);md_text(12,77,md_learn_target_name(m->target),MD_INK);
   md_text(12,105,md_learn_armed()==learn_slot?"MOVE A CONTROLLER...":!m->enabled?"UNASSIGNED":md_learn_picked(learn_slot)?"FOLLOWING YOUR HAND":"MOVE TO MATCH THE VALUE",col);
   md_text(12,131,"REC: CLEAR / SAVE: REMEMBER",MD_DIM);
   if(dream_ui.page==MIDI_LEARN){md_value_span(8,158,"K1 SLOT",learn_slot+1,1,16,col);md_named(124,158,"K2 TARGET",md_learn_target_name(m->target),col);md_value_span(8,194,"K3 TRACK",m->track+1,1,6,col);md_named(124,194,"K4 LEARN",md_learn_armed()==learn_slot?"CANCEL <":"ARM >",col);}
   else{md_named(8,158,"K1 PORT",ports[m->port],col);md_value_span(124,158,"K2 CH / 0 ANY",m->channel,0,16,col);md_value_span(8,194,"K3 CC",m->cc,1,119,col);md_named(124,194,"K4 MAPPING",m->enabled?"ON":"OFF",col);}
  }else if(dream_ui.page<=LINK_MOTION){const md_link *l=&p->links[link_slot];md_text(12,49,md_link_signal_name(l->signal),MD_INK);md_text(110,49,md_link_target_name(l->target),col);
   float activity=md_link_value(link_slot);int pulse=(int)((activity<0?-activity:activity)*10.f);for(int x=44;x<194;x+=5){int y=99+md_sway(x/5+(dream_prefs.motion?field_time/110:0))/(9+(1000-(l->depth<0?-l->depth:l->depth))/100);cv_pset(x,y,md_tint(MD_BG,col,5+pulse));}
   md_circle(32,99,14,col,0);md_circle(207,99,14,col,0);md_num(l->source+1,s);md_text(28,93,s,MD_INK);md_num(l->destination+1,s);md_text(203,93,s,MD_INK);md_text(12,131,l->depth?"A MUSICAL CONNECTION":"DEPTH ZERO / NO CHANGE",MD_DIM);
   md_value_span(8,158,"K1 LINK",link_slot+1,1,4,col);
   if(dream_ui.page==LIVING_LINKS){md_value_span(124,158,"K2 SOURCE",l->source+1,1,6,col);md_value_span(8,194,"K3 DEST",l->destination+1,1,6,col);md_named(124,194,"K4 TARGET",md_link_target_name(l->target),col);}
   else{md_named(124,158,"K2 SIGNAL",md_link_signal_name(l->signal),col);md_value_span(8,194,"K3 DEPTH",l->depth/10,-100,100,col);md_value_span(124,194,"K4 RELEASE",l->release/10,0,100,col);}
  }else{const md_drift *d=&p->drift;static const char *mode[]={"OFF","MANUAL","AUTO"};md_text(12,49,md_drift_ready()?"TWO WORLDS / ONE CONTINUUM":"CAPTURE A AND B TO BEGIN",col);
   int x0=29,y0=107;for(int x=29;x<=210;x++){int y=107-((x-29)*(210-x))/260;cv_pset(x,y,md_tint(MD_BG,col,10));}int x=x0+md_drift_position()*181/1000;md_circle(x,107-((x-29)*(210-x))/260,5,col,1);md_circle(29,y0,9,col,0);md_circle(210,y0,9,col,0);md_text(25,124,d->captured&1?"A":"-",MD_INK);md_text(206,124,d->captured&2?"B":"-",MD_INK);
   if(dream_ui.page==BIOME_DRIFT){md_named(8,158,"K1 MODE",mode[d->mode],col);md_value_span(124,158,"K2 POSITION",d->position/10,0,100,col);md_value_span(8,194,"K3 CYCLE SEC",10+d->rate*290/1000,10,300,col);md_value_span(124,194,"K4 DEPTH",d->depth/10,0,100,col);}
   else{md_named(8,158,"K1 CAPTURE A",d->captured&1?"READY / >":"CAPTURE >",col);md_named(124,158,"K2 CAPTURE B",d->captured&2?"READY / >":"CAPTURE >",col);md_value_span(8,194,"K3 DEPTH",d->depth/10,0,100,col);md_named(124,194,"K4 MODE",mode[d->mode],col);}
  }return;
 }
 if(dream_ui.page>=JOURNEY&&dream_ui.page<=JOURNEY_KEEP){
  const md_journey *j=&timeline.plan;
  const md_journey_section *a=&j->section[journey_edit];
  static const char *modes[]={"OFF","ONCE","LOOP"};
  cv_rect(8,44,224,107,MD_CARD);
  md_text(14,49,"SECTION",MD_DIM);md_num(journey_edit+1,s);md_text(70,49,s,col);
  md_text(14,66,a->biome==65535?"THIS BIOME":md_biome_name(a->biome),MD_INK);
  if(timeline.section>=0){
   unsigned bar=timeline.step/16;
   md_text(118,49,"BAR",MD_DIM);md_num(bar%a->bars+1,s);md_text(149,49,s,MD_INK);
   md_text(171,49,"REP",MD_DIM);md_num(bar/a->bars+1,s);md_text(200,49,s,MD_INK);
  }else md_text(160,49,"EDITING",MD_DIM);
  draw_journey_timeline(col);
  draw_journey_anchors(col);
  if(dream_ui.page==JOURNEY){md_named(8,158,"K1 MODE",modes[j->mode],col);md_value_span(124,158,"K2 SECTION",journey_edit+1,1,8,col);md_value_span(8,194,"K3 BIOME",a->biome==65535?0:a->biome+1,0,MD_BIOMES,col);md_value_span(124,194,"K4 LENGTH",j->length,1,8,col);}
  else if(dream_ui.page==JOURNEY_TIME){md_value_span(8,158,"K1 SECTION",journey_edit+1,1,8,col);md_value_span(124,158,"K2 BARS",a->bars,1,64,col);md_value_span(8,194,"K3 REPEATS",a->repeats,1,16,col);md_value_span(124,194,"K4 PITCH st",a->transpose,-24,24,col);}
  else{md_value_span(8,158,"K1 SECTION",journey_edit+1,1,8,col);md_value_span(124,158,"K2 TRACK",dream_ui.track+1,1,6,col);md_named(8,194,"K3 CARRY",(a->keep>>dream_ui.track)&1?"KEEP THREAD":"NEW VOICE",col);md_named(124,194,"K4 MODE",modes[j->mode],col);}
 }else if((dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)){
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
 }else if(dream_ui.page==SEQ_GRID){
  draw_sequence_grid(col);
 }else if(dream_ui.page==SEQ_TOOLS){
  draw_sequence_tools(col);
 }else if(dream_ui.page==SEQ_RULES){
  draw_sequence_rules(col);
 }else if(dream_ui.page>=MIXER){
  const md_extension *e=&p->ext[dream_ui.track];
  if(dream_ui.page==SEQ_FLOW){
   const md_sequence *q=&p->sequence[dream_ui.track];const md_motion *m=&p->motion;
   char label[]="TRACK 1 / SAME NOTES, NEW PATH";label[6]='1'+dream_ui.track;md_text(12,51,label,MD_INK);
   md_text(12,75,md_direction_name(m->direction[dream_ui.track]),col);
   for(int j=0;j<16;j++){unsigned at=md_sequence_position(dream_ui.track,j);int x=15+j*14,y=124-(int)at*32/(q->length>1?q->length-1:1);md_circle(x,y,2,col,q->steps[at].note!=0);if(j){unsigned before=md_sequence_position(dream_ui.track,j-1);cv_line(x-14,124-(int)before*32/(q->length>1?q->length-1:1),x,y,md_tint(MD_BG,col,10));}}
   md_text(12,139,"SEQ: SIX TRACKS / HOLD: HELP",MD_DIM);
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
   int playing_step=sequence_view_head[dream_ui.track];
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
    if(q->enabled&&i==playing_step)md_circle(x,y,6,sequence_view_hits&(1u<<dream_ui.track)?MD_INK:MD_EDGE,0);
   }
   cv_text(12,143,&FONT_T,"PER TRACK / SAVE IN YOUR BIOME",MD_DIM);
   if(dream_ui.page==SEQUENCE){md_named(8,158,"K1 SEQUENCE",q->enabled?"ON":"OFF",col);md_value_span(124,158,"K2 STEP",q->edit+1,1,32,col);if(st->note){md_note_label(st->note,s);md_named(8,194,"K3 NOTE / MIDI",s,col);}else md_named(8,194,"K3 NOTE / MIDI","REST",col);md_value_span(124,194,"K4 LENGTH",q->length,1,32,col);}
   else{md_value(8,158,"K1 VELOCITY",st->velocity,col);md_value(124,158,"K2 GATE",md_step_gate(st),col);md_value(8,194,"K3 CHANCE",md_step_chance(st),col);md_named(124,194,"K4 GENERATE",md_generator_name(q->generator),col);}
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
   md_text(12,65,"MIDI INPUT: OFF OR CH 1-16",MD_INK);md_text(12,94,"VELOCITY / SUSTAIN / +/-2 BEND",MD_DIM);md_text(12,120,"CC7 LEVEL / 74 FILTER / 91 ROOM",MD_DIM);
   md_named(8,158,"K1 CLOCK",md_clock_source()==0?"INTERNAL":md_clock_source()==1?"USB":md_clock_source()==2?"TRS":"BLE",col);md_named(124,158,"K2 SCALE SNAP",md_midi_quantize()?"ON":"OFF",col);
   char route_label[12];if(md_midi_route(dream_ui.track)){memcpy(route_label,"CH ",3);md_num(md_midi_route(dream_ui.track),route_label+3);}else memcpy(route_label,"OFF",4);
   md_named(8,194,"K3 MIDI IN",route_label,col);md_named(124,194,"K4 CH16 MACROS",md_midi_macro_enabled()?"ON":"OFF",col);
  }else if(dream_ui.page==BT_PAGE){
   int bs=plat_ble_state(),ds=plat_ble_device();char name[21]={0};int rssi=0;
   if(ds==BLE_DEV_READY||ds==BLE_DEV_CONNECTING||ds==BLE_DEV_SEARCHING){const char *n=btdev_name();int j=0;while(n[j]&&j<20){name[j]=n[j];j++;}}
   else btdev_found(btdev_pick(),name,&rssi);
   md_text(12,62,"BLUETOOTH MIDI / USB / TRS",MD_INK);
   md_text(12,91,bs==BLE_READY?(ds==BLE_DEV_READY?"EXTERNAL MIDI LINK ACTIVE":plat_ble_connected()?"PHONE / COMPUTER CONNECTED":"READY FOR PHONE / TABLET"):bs==BLE_STARTING?"RADIO STARTING":bs==BLE_FAILED?"RADIO NEEDS RESTART":"RADIO IS OFF",MD_DIM);
   int pin=plat_ble_passkey();
   if(pin>=0){char code[]="PAIRING CODE 000000";for(int j=18;j>=13;j--){code[j]=(char)('0'+pin%10);pin/=10;}md_text(12,117,code,MD_INK);}
   else md_text(12,117,ds==BLE_DEV_READY?"MIDI DEVICE CONNECTED":ds==BLE_DEV_CONNECTING||ds==BLE_DEV_SEARCHING?"CONNECTING TO DEVICE":ds==BLE_DEV_SCANNING?"SCANNING FOR MIDI DEVICES":btdev_count()?"K3: CONNECT SELECTED DEVICE":"K2: SEARCH FOR MIDI DEVICES",MD_DIM);
   md_named(8,158,"K1 RADIO",(dream_prefs.reserved&1)?"ON":"OFF",col);
   md_named(124,158,"K2 DEVICE",name[0]?name:"TURN TO SCAN",col);
   md_named(8,194,"K3 LINK",ds==BLE_DEV_READY?"CONNECTED":ds==BLE_DEV_CONNECTING||ds==BLE_DEV_SEARCHING?"CONNECTING":"TURN RIGHT",col);
   md_named(124,194,"K4 MIDI THRU",(dream_prefs.reserved&2)?"ON":"OFF",col);
  }else if(dream_ui.page==BANKS){
   md_text(12,66,bank_armed<0?"KEEP YOUR OWN SMALL WORLDS":bank_armed&BANK_CLEAR_ARM?"CLEAR THIS SLOT?":"REPLACE THIS SLOT?",MD_INK);md_text(12,96,bank_name,col);char prefix[24];memcpy(prefix,bank_name,bank_char);prefix[bank_char]=0;cv_rect(12+text_w(md_label_font(12,96,bank_name),prefix),113,5,1,col);
   md_text(12,121,bank_armed>=0?"SEL: CONFIRM / HOME: CANCEL":"SAVE: STORE / REC: CLEAR",MD_DIM);
   md_value_span(8,158,"K1 USER SLOT",bank_slot+1,1,bank_kind?64:16,col);md_named(124,158,"K2 BANK",bank_kind?"SOUNDS":"BIOMES",col);md_named(8,194,"K3 TURN RIGHT","LOAD",col);md_value_span(124,194,"K4 NAME CURSOR",bank_char+1,1,23,col);
  }else if(dream_ui.page==MIXER){
   draw_mixer_field(p,col);
   md_value(8,158,"K1 LEVEL",t->level/10,col);md_value(124,158,"K2 GRAIN",t->send/10,col);md_value(8,194,"K3 DELAY",e->delay_send/10,col);md_value(124,194,"K4 REVERB",e->reverb_send/10,col);
  }else if(dream_ui.page==BASSLINE){
   md_text(12,62,"ROOTED PHRASES / ROUND LOW END",MD_INK);
   for(i=0;i<16;i++)md_circle(15+i*14,107-e->notes[i],e->notes[i]<0?1:3,col,e->notes[i]>=0);
   md_named(8,158,"K1 PHRASE",e->bass_mode==2?"STEP":e->bass_mode?"EUCLID":"OFF",col);md_value_span(124,158,"K2 MOTIF",e->pattern+1,1,4,col);md_value(8,194,"K3 GLIDE",e->glide/10,col);md_value_span(124,194,"K4 BASE NOTE",t->note,24,96,col);
  }else if(dream_ui.page==SPACE){
   draw_space_field(p);
   md_value_span(8,158,"K1 ECHO ms",p->delay_ms,40,350,col);md_value(124,158,"K2 FEEDBACK",p->delay_fb/10,col);md_value(8,194,"K3 ROOM TAIL",p->reverb_time/10,col);md_value(124,194,"K4 RETURN",p->fx_level/10,col);
  }else if(dream_ui.page==MODULATION){
   md_text(15,65,"MEMORY / TIDES / RING BLEND",MD_INK);md_text(15,94,md_lfo_target_name(p->motion.target[dream_ui.track]),col);md_text(15,121,"FX AGAIN: TIDAL TARGET / CURVE",MD_DIM);
   md_value(8,158,"K1 MARBLES",e->marbles/10,col);md_value(124,158,"K2 TIDES",e->tides/10,col);md_value(8,194,"K3 CYCLE",e->tides_rate/10,col);md_value(124,194,"K4 WARPS",e->warps/10,col);
  }else{
   for(i=0;i<16;i++){int x=12+(i%8)*28,y=62+(i/8)*36;md_num(e->notes[i],s);md_text(x,y,s,i==e->edit_step?MD_INK:col);if(i==e->edit_step)cv_rect(x,y+14,20,2,col);}
   md_value_span(8,158,"K1 STEP",e->edit_step+1,1,16,col);md_value_span(124,158,"K2 NOTE / REST",e->notes[e->edit_step],-1,24,col);md_value(8,194,"K3 ACCENT",t->accent/10,col);md_value(124,194,"K4 CHANCE",t->prob/10,col);
  }
 }else if(dream_ui.page==LIVE) {
  atlas_plate(8,44,83,94,col);
  atlas_filaments(11,49,77,82,md_world_dna());
  if(living.profile){
   unsigned dna=md_world_dna(),t=field_time/110;
   atlas_mark(22+md_sway(t+(dna&15))/8,111,10,ART_FROND,dna,living.level[1],
              md_tint(MD_BG,md_palette()[2],17));
   atlas_mark(76+md_sway(t+27)/8,64,10,ART_FROND,dna>>3,living.level[4],
              md_tint(MD_BG,md_palette()[1],16));
   draw_living_view();
  }else draw_legacy_plant(p);
  draw_live_seeds(p);
  for(i=0;i<md_active_tracks();i++){
   int muted=p->track[i].mute;uint16_t quiet=RGB(112,130,137);
   if(muted||i==dream_ui.track)cv_rect(94,43+i*16,137,16,muted?RGB(17,26,32):MD_CARD);
   if(muted)cv_rect(98,57+i*16,128,1,quiet);
   md_num(i+1,s);md_text(98,44+i*16,s,muted?quiet:dream_colors[i]);
   md_text(112,44+i*16,p->kit.enabled&&i==3?"LO-FI KIT":md_model_name(p->track[i].model),muted?quiet:MD_INK);
   if(muted)cv_text(211,46+i*16,&FONT_T,"OFF",quiet);
   if(!muted){int meter=clamp((int)(md_status()->track_peak[i]*140),0,17);cv_rect(212,56+i*16,17,1,MD_EDGE);cv_rect(212,55+i*16,meter,2,dream_colors[i]);}
   if(i==dream_ui.track)cv_rect(94,46+i*16,2,9,col);
  }
  md_text(12,141,p->kit.enabled&&dream_ui.track==3?md_kit_name(p->kit.bank):md_tone_name(p->tone_preset[dream_ui.track]),col);md_text(140,141,md_biome_family(p->biome_id),MD_DIM);
  const char *labels[]={"K1 DENSITY","K2 MOTION","K3 COLOUR","K4 SPACE"};for(i=0;i<4;i++)md_value(8+(i%2)*116,158+(i/2)*36,labels[i],p->macro[i]/10,dream_colors[i]);
 } else if(dream_ui.page==SYSTEM) {
  md_text(10,48,"MUTABLE DREAMS 2.0 DEV",MD_INK);md_text(10,69,md_quality_active()?"FULL  24 MODES / 4 GRAINS":p->quality?"ECO NOW, FULL KEPT FOR NEXT":"ECO / ADAPTIVE MODES / 2 GRAINS",col);
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
  const int inverted=dream_inverted();
  for(int look=0;look<2;look++){
   const int x=10+look*114;
   const uint16_t mask=inverted!=look?0xffff:0;
   cv_rect(x,51,104,64,look==inverted?col:MD_EDGE);
   cv_rect(x+2,53,100,60,MD_CARD^mask);
   md_text(x+9,59,look?"INVERTED":"ORIGINAL",MD_INK^mask);
   for(int leaf=0;leaf<4;leaf++){
    const int u=x+15+21*leaf;
    const int y=88+md_sway(field_time/100+leaf*7)/6;
    const uint16_t shade=md_palette()[leaf]^mask;
    cv_line(u-7,y+9,u+7,y-8,shade);
    cv_line(u-7,y+9,u-5,y-2,shade);
    cv_line(u-5,y-2,u+7,y-8,shade);
    cv_line(u+7,y-8,u+5,y+3,shade);
    cv_line(u+5,y+3,u-7,y+9,shade);
   }
  }
  md_text(12,125,inverted?"ACTIVE: INVERTED":"ACTIVE: ORIGINAL",col);
  md_text(12,141,"SEL: CHANGE COLOURS",MD_DIM);
  md_named(8,158,"K1 MOTION",dream_prefs.motion?"LIVING":"STILL",col);md_named(124,158,"K2 START SOUND",dream_prefs.boot_sound?"ON":"OFF",col);
  md_named(8,194,"K3 INTRO","REPLAY",col);md_named(124,194,"K4 BUTTON LED",dream_prefs.button_glow==0?"OFF":dream_prefs.button_glow==1?"DIM":"FLOW",col);
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
   unsigned age=(uint32_t)(plat_ms()-pulse_start[dream_ui.track]);
   int touch=dream_prefs.motion&&(pulse_active&(1u<<dream_ui.track))&&age<520?520-(int)age:0;
   if(t->model==MD_SKIN){
    // Membrane contours expand from actual hits; no random activity display.
    for(int ring=0;ring<3;ring++){
     int scale=300+ring*110+(520-touch)/5;uint16_t ink=md_tint(MD_BG,col,10+ring*3+touch/70);
     for(int k=0;k<96;k++){
      int z=(k+1)%96,y=109*16+(contour_y(ring,k)-99*16)*scale/1000,ny=109*16+(contour_y(ring,z)-99*16)*scale/1000;
      if((y/16+cv_oy<-2&&ny/16+cv_oy<-2)||(y/16+cv_oy>=(int)cv_h&&ny/16+cv_oy>=(int)cv_h))continue;
      md_fine_line(119*16+(contour_x(ring,k)-166*16)*scale/1000,y,119*16+(contour_x(ring,z)-166*16)*scale/1000,ny,ink);
     }
    }
    md_circle(119,109,2+touch/130,md_tint(MD_BG,col,8+touch/28),1);
   }else if(t->model==MD_CEDAR||t->model==MD_BOWED){
    for(int string=0;string<7;string++){
     int x=80+string*13,bow=dream_prefs.motion?md_sway(field_time/45+string*7)*touch/1600:0;
     uint16_t ink=md_tint(MD_BG,col,10+string*2);md_fine_line(x*16,84*16,(x+bow)*16,110*16,ink);md_fine_line((x+bow)*16,110*16,x*16,137*16,ink);
     md_circle(x,137,1,col,1);
    }
   }else if(t->model==MD_SILK||t->model==MD_COPPER||t->model==MD_VOWEL){
    int energy=clamp(touch/5,0,100);
    atlas_mark(119,107,27,t->model==MD_SILK?ART_CURRENT:t->model==MD_COPPER?ART_WIRE:ART_CORAL,
               (uint32_t)t->harm*917u+23u,energy,col);
   }else if(t->model==MD_FELT){
    const int glow=clamp(touch/100,0,10);
    for(int bar=0;bar<5;bar++){
     const int x=78+bar*18,y=90+bar*5,length=45-bar*7;
     const int lift=dream_prefs.motion?(glow*md_sway(field_time/80+bar*8))/80:0;
     const uint16_t ink=md_tint(MD_BG,col,13+bar*2+glow/2);
     cv_rect(x,y+lift,9,length,ink);
     cv_line(x+2,y+lift+2,x+2,y+lift+length-3,md_tint(MD_BG,col,22));
     md_circle(x+4,y+lift-6,2,ink,1);
    }
   }else if(t->model==MD_REED||t->model==MD_AERO){
    for(int pipe=0;pipe<7;pipe++){
     int x=77+pipe*13,h=16+((t->harm/35+pipe*11)%30),swell=dream_prefs.motion?md_sway(field_time/160+pipe*5)/6:0;
     uint16_t ink=md_tint(MD_BG,col,9+pipe*2);
     for(int row=0;row<h;row++){int width=row<3?row+2:row>h-3?h-row+1:5;cv_rect(x+(7-width)/2,132-h+row+swell,width,1,ink);}
     md_fine_line((x+5)*16,(132-h+swell)*16,(x+5)*16,(131+swell)*16,md_tint(MD_BG,col,18+pipe));
    }
   }else for(i=0;i<44;i++){float a=i*.42f+flower_time*.00015f;int r=14+i/2+(int)(3*fm_sinf(i*.25f+flower_time*.001f));md_circle(119+(int)(fm_cosf(a)*r),109+(int)(fm_sinf(a)*r),i%4==0?3:1,col,1);}
   md_value(8,158,t->model==MD_SILK?"K1 OVERTONE":t->model==MD_COPPER?"K1 SIDEBANDS":t->model==MD_VOWEL?"K1 VOWEL":t->model==MD_SKIN?"K1 TOUCH":t->model==MD_FELT?"K1 BODY":"K1 TONE",t->tone/10,col);md_value(124,158,t->model==MD_SKIN?"K2 PALM / RIM":t->model==MD_FELT?"K2 OVERTONE":t->model==MD_SILK?"K2 SPREAD":t->model==MD_COPPER?"K2 INTERVAL":t->model==MD_VOWEL?"K2 FOCUS":"K2 HARMONICS",t->harm/10,col);if(t->model==MD_ELEMENTS)md_named(8,194,"K3 EXCITER",t->shape<334?"MALLET":t->shape<667?"PLECTRUM":"PARTICLES",col);else md_value(8,194,t->model==MD_BOWED?"K3 PRESSURE":t->model==MD_AERO?"K3 BREATH":t->model==MD_FELT?"K3 HARDNESS":t->model==MD_SILK?"K3 DRIFT":t->model==MD_COPPER?"K3 STRIKE":t->model==MD_VOWEL?"K3 SWELL":md_has_envelope(t->model)?"K3 SHAPE":"K3 DECAY",(md_has_envelope(t->model)?t->shape:t->decay)/10,col);md_value(124,194,"K4 LEVEL",t->level/10,col);
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
   draw_grain_tape(p,col);
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
  if(dream_ui.page==LIVE){
   int name_x=8;
   if(p->biome_id>=0){md_num(p->biome_id+1,s);cv_text(8,227,&FONT_T,s,col);name_x=14+text_w(&FONT_T,s);}
   atlas_text(name_x,226,148-name_x,&FONT_XS,md_biome_name(p->biome_id),MD_INK);
   if(p->edited_mask&1)cv_text(151,225,&FONT_XS,"*",col);
   cv_text(167,226,&FONT_T,p->locked?"LOCK":"RAND",MD_DIM);
   cv_text(209,226,&FONT_T,p->freeze?"ICE":"REC",MD_DIM);
  }
  else if(dream_ui.page==SEQ_GRID)md_text(10,226,"SELECT: TRACK / HOLD SEQ: HELP",MD_DIM);
  else if(dream_ui.page==SEQ_TOOLS)md_text(10,226,"SEL APPLY / SEQ STEP RULES",MD_DIM);
  else if(dream_ui.page==APPEARANCE)md_text(10,226,"STOP + SAVE TO KEEP / HOLD GLO",MD_DIM);
  else if(dream_ui.page==SEQ_RULES)md_text(10,226,"SEL FILL / SEQ JOURNEY / HOLD HELP",MD_DIM);
  else if(dream_ui.page>=JOURNEY&&dream_ui.page<=JOURNEY_KEEP)md_text(10,226,"PRESET: SECTION BIOME / HOLD SEQ",MD_DIM);
  else if(dream_ui.page==LIVE)md_text(10,226,"PRESET: BIOME  /  HOLD HOME: ITINERA",MD_DIM);
  else md_text(10,226,(dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)?"PRESET: PAD  /  HOLD: HELP":(dream_ui.page==SOUND||dream_ui.page==FILTER||dream_ui.page==ENVELOPE||dream_ui.page==MIX||dream_ui.page==MIXER||dream_ui.page==BASSLINE||dream_ui.page==MODULATION||dream_ui.page==LFO_ROUTE||dream_ui.page==LFO_ROUTE2||dream_ui.page==SEQ_FLOW||(dream_ui.page==PHRASE||dream_ui.page==SEQUENCE||dream_ui.page==STEP_DETAIL))?"PRESET: TONE  /  HOLD: HELP":"PRESET: BIOME  /  HOLD: HELP",MD_DIM);
 }
}
void app_say(const char *a,const char *b) {unsigned i=0;(void)b;while(a&&a[i]&&i<31){dream_ui.msg[i]=a[i];i++;}dream_ui.msg[i]=0;dream_ui.msg_start=plat_ms();dream_ui.dirty=1;ann_say(dream_ui.msg,0,ANN_NOW);}
void app_init(void) {
 bank_pending=-1;bank_allow_load=0;ble_link_lost=0;learn_slot=link_slot=0;seq_scope=1;seq_action=SEQ_COPY;seq_interval=0;seq_amount=65;sequence_view_notice=0;md_init();midi_link=plat_midi_out_link();usb_awake=plat_usb_awake();overloads=cpu_peak=quality_notice=0;
 memset(&dream_prefs,0,sizeof dream_prefs);dream_prefs.magic=0x4D445046;dream_prefs.motion=1;dream_prefs.boot_sound=1;dream_prefs.button_glow=1;dream_prefs.revision=2;for(int i=0;i<6;i++)dream_prefs.midi_route[i]=i+1;dream_prefs.midi_macro=1;
 performance_prefs=(md_performance_preferences){.version=1};
 browse_filter=0;
 browse_position=0;
 md_saved_preferences prefs;
 int pref_bytes=plat_store_load(OBJ_PREFS,&prefs,sizeof prefs);
 if(pref_bytes>0&&dream_prefs_decode(&dream_prefs,&prefs,pref_bytes)&&pref_bytes==sizeof prefs)
  performance_prefs=prefs.performance;
 set_local_monitor((performance_prefs.favorites[LOCAL_MONITOR_BIT/8]>>(LOCAL_MONITOR_BIT%8))&1);
 md_set_velocity_touch(performance_prefs.touch);
 for(int i=0;i<6;i++)md_set_midi_route(i,dream_prefs.midi_route[i]);md_set_midi_macro_enabled(dream_prefs.midi_macro);for(int i=0;i<16;i++)md_set_mapping(i,&dream_prefs.mapping[i]);
 intro_pending=1;intro_active=chime_left=0;pulse_active=0;md_gain=0;radio_preset=0;memset(radio_saved,0,sizeof radio_saved);memset(&radio_view,0,sizeof radio_view);memset(&dream_ui,0,sizeof dream_ui);dream_ui.help=-1;
 int bytes=plat_store_load(OBJ_PATCH,&bank_patch_io,sizeof bank_patch_io);if(bytes>0)md_import(&bank_patch_io,(unsigned)bytes);
}
void app_render(int32_t *lr,uint32_t n,uint32_t master) {
 if(dream_reloading){memset(lr,0,n*2*sizeof *lr);return;}
 uint32_t pkt,load=plat_cpu_pct();int port;if(load>cpu_peak)cpu_peak=load;
 if(ble_link_lost){ble_link_lost=0;md_midi_lost(PORT_BLE);}
 uint32_t link=plat_midi_out_link();if(link!=midi_link){midi_link=link;md_midi_lost(PORT_USB);}
 // The link number only changes when a computer sets the FM-1 up again. A
 // pulled cable or a sleeping computer ends its held notes, pedal and bend now.
 {const int awake=plat_usb_awake();if(usb_awake&&!awake)md_midi_lost(PORT_USB);usb_awake=awake;}
 md_block_begin();
#if FM1B_MIDI_STAMP
 /* F-2: USB and TRS MIDI carry their landing time. What landed during the last
  * block plays at the same point of this one: a constant one-block latency in
  * place of up to a block of jitter (notes, clock pulses, the steps they start).
  * Bluetooth's are stamped when read: at once, as before. */
 static uint32_t last_block_t24;static uint8_t have_last_block;
 const uint32_t block_t24=plat_ticks24(),block_span=block_t24-last_block_t24;
 const int placed=have_last_block&&block_span>0&&block_span<20000u*PLAT_TICKS_PER_US;
 last_block_t24=block_t24;have_last_block=1;
#endif
 for(unsigned m=0;m<16&&plat_midi_in(&pkt,&port);m++){
  if(dream_prefs.reserved&2){if(port==PORT_BLE)plat_midi_send(pkt,TO_USB);else if(port==PORT_USB||port==PORT_TRS)plat_midi_send(pkt,TO_BLE);}
  unsigned at=0;
#if FM1B_MIDI_STAMP
  if(placed&&port!=PORT_BLE){
   const int32_t since=(int32_t)(plat_midi_in_t24()-(block_t24-block_span));
   at=since<=0?0:(uint32_t)since>=block_span?n-1:(unsigned)((float)since/(float)block_span*n);
  }
#endif
  md_midi_at((pkt>>8)&255,(pkt>>16)&127,(pkt>>24)&127,port,at);
 }
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
 unsigned usb_offset=0;
#endif
 while(n){unsigned k,z=n>128?128:n;
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
  md_usb_stems stems={0};
  stems.first=plat_usb_ch(usb_offset,&stems.first_frames);
  if(stems.first&&stems.first_frames<z){
   unsigned wrapped_frames;
   stems.wrapped=plat_usb_ch(usb_offset+stems.first_frames,&wrapped_frames);
   if(!stems.wrapped||wrapped_frames<z-stems.first_frames)stems.first=0;
  }
  md_render_stems(md_audio,z,stems);
  usb_offset+=z;
#else
  md_render(md_audio,z);
#endif
 if(chime_left&&md_playing())chime_release=1;for(k=0;k<z;k++){md_gain+=((float)master/4096.0f-md_gain)*.004f;float chime=0;

 if(chime_left){unsigned elapsed=88200-chime_left;float env=elapsed<11025?elapsed/11025.0f:chime_left/77175.0f;const float hz[3]={261.6256f,391.9954f,523.2511f};for(int c=0;c<3;c++){chime_phase[c]+=hz[c]/44100.0f;if(chime_phase[c]>=1)chime_phase[c]-=1;chime+=fm_sinf(chime_phase[c]*6.2831853f)*.009f*env;}chime_left--;if(chime_release){chime*=chime_gain*(1.0f/128.0f);if(--chime_gain==0)chime_left=0;}}
 lr[2*k]=(int32_t)((md_audio[2*k]+chime)*md_gain*8380000.0f);lr[2*k+1]=(int32_t)((md_audio[2*k+1]+chime)*md_gain*8380000.0f);}lr+=z*2;n-=z;}
}
void app_panic(void) {dream_cmd(MD_PANIC,0,0);}
void app_redraw(void) {display_repaint=1;dream_ui.dirty=1;}
int app_ble_save_later(void) {return md_playing();}
/* From the audio interrupt (main_fm1.c), right after the render it belongs to: ECO for this render owner and
 * nothing else. dream_cmd could reach app_say and the announcer's SysEx ring, whose writer is the main loop. */
void app_overload(void) {overloads++;md_overload();} /* F-3: ECO for the render, not the saved quality */
static void md_tap(int b) {
 if(md_pending_biome()>=0||bank_pending>=0){
  if(b==B_SEL){if(bank_pending>=0){bank_slot=bank_pending;bank_kind=0;bank_pending=-1;bank_allow_load=1;bank_action(0);bank_allow_load=0;}else dream_cmd(MD_SCENE_CONFIRM,0,1);}
  else if(b==B_HOME||b==B_SAVE){
   bank_pending=-1;dream_cmd(MD_SCENE_CONFIRM,0,0);
   if(b==B_SAVE&&dream_ui.page==BANKS)bank_action(BANK_SAVE_ASK);
   else dream_ui.page=b==B_SAVE?BANKS:LIVE;
  }
  return;
 }
 const md_patch *p=md_state();switch(b){
 case B_HOME:
  bank_armed=-1;
  if(dream_ui.page==LIVE&&dream_ui.live_view==LIVE_BROWSER){dream_ui.page=ITINERA;break;}
  dream_ui.live_view=dream_ui.page==LIVE?(dream_ui.live_view+1)%LIVE_VIEW_COUNT:LIVE_ATLAS;
  dream_ui.page=LIVE;
  if(dream_ui.live_view==LIVE_BROWSER)browse_open(p->biome_id);
  else app_say(dream_ui.live_view==LIVE_TRACKS?"LARGE TRACKS":"LIVE ATLAS","");
  break;
 case B_EDIT:if(p->kit.enabled&&(dream_ui.track==3||(dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ))){dream_ui.track=3;dream_ui.page=dream_ui.page==KIT_PAD?KIT_SEND:dream_ui.page==KIT_SEND?MIXER:KIT_PAD;break;}dream_ui.page=dream_ui.page==SOUND?FILTER:dream_ui.page==FILTER?ENVELOPE:dream_ui.page==ENVELOPE?MIX:dream_ui.page==MIX?MIXER:SOUND;break;
 case B_SEQ:
  if(dream_ui.page==SEQ_FLOW||dream_ui.page==KIT_SEQ){dream_ui.page=SEQ_GRID;break;}
  if(dream_ui.page==SEQ_GRID){dream_ui.page=SEQ_TOOLS;break;}
  if(dream_ui.page==SEQ_TOOLS){dream_ui.page=SEQ_RULES;break;}
  if(dream_ui.page==SEQ_RULES){dream_ui.page=JOURNEY;break;}
  if(dream_ui.page>=JOURNEY&&dream_ui.page<=JOURNEY_KEEP){dream_ui.page=dream_ui.page==JOURNEY?JOURNEY_TIME:dream_ui.page==JOURNEY_TIME?JOURNEY_KEEP:RHYTHM;break;}if(p->kit.enabled&&dream_ui.track==3){dream_ui.page=KIT_SEQ;break;}dream_ui.page=dream_ui.page==RHYTHM?GROOVE:dream_ui.page==GROOVE?BASSLINE:dream_ui.page==BASSLINE?PHRASE:dream_ui.page==PHRASE?SEQUENCE:dream_ui.page==SEQUENCE?STEP_DETAIL:dream_ui.page==STEP_DETAIL?SEQ_FLOW:RHYTHM;break;
 case B_FX:dream_ui.page=dream_ui.page==GRAIN?TAPE:dream_ui.page==TAPE?SPACE:dream_ui.page==SPACE?MODULATION:dream_ui.page==MODULATION?LFO_ROUTE:dream_ui.page==LFO_ROUTE?LFO_ROUTE2:dream_ui.page==LFO_ROUTE2?CLOUD:dream_ui.page==CLOUD?LIVING_LINKS:dream_ui.page==LIVING_LINKS?LINK_MOTION:dream_ui.page==LINK_MOTION?BIOME_DRIFT:dream_ui.page==BIOME_DRIFT?DRIFT_SCENES:GRAIN;break;
 case B_GLO:
  if(dream_ui.page==LIVE&&dream_ui.live_view!=LIVE_ATLAS){
   bank_kind=0;bank_slot=clamp(bank_slot,0,15);bank_armed=-1;dream_ui.page=BANKS;break;
  }
  dream_ui.page=dream_ui.page==HARMONY?PROGRESSION:dream_ui.page==PROGRESSION?SYSTEM:dream_ui.page==SYSTEM?APPEARANCE:dream_ui.page==APPEARANCE?MIDI:dream_ui.page==MIDI?USB_AUDIO:dream_ui.page==USB_AUDIO?MIDI_LEARN:dream_ui.page==MIDI_LEARN?MIDI_MAP:dream_ui.page==MIDI_MAP?BT_PAGE:dream_ui.page==BT_PAGE?BANKS:dream_ui.page==BANKS?KIT:dream_ui.page==KIT?ITINERA:HARMONY;break;
 case B_SEL:
  if(dream_ui.page==BANKS&&bank_armed>=0){
   if((uint32_t)(plat_ms()-bank_arm_start)<=BANK_CONFIRM_MS&&
      (bank_armed&~BANK_CLEAR_ARM)==bank_object()*64+bank_slot)
    bank_action(bank_armed&BANK_CLEAR_ARM?BANK_CLEAR:BANK_SAVE_REPLACE);
   else bank_armed=-1;
   break;
  }
  if(dream_ui.page==ITINERA)break;
  if(dream_ui.page==APPEARANCE){toggle_display_polarity();break;}
  // F-6: an external clock's tempo is the transport's; SEL here keeps it as the project's.
  if(dream_ui.page==MIDI&&md_clock_source()&&md_tempo_now()!=p->tempo){dream_cmd(MD_TEMPO,0,md_tempo_now());app_say("CLOCK TEMPO KEPT","");break;}
  if(dream_ui.page==SEQ_TOOLS){sequence_apply();break;}
  if(dream_ui.page==SEQ_RULES){if(md_playing())dream_cmd(MD_FILL,0,-1);else app_say("PLAY TO QUEUE FILL","");break;}
  if(dream_ui.page==SEQ_GRID){dream_ui.page=p->kit.enabled&&dream_ui.track==3?KIT_SEQ:SEQUENCE;dream_ui.dirty=1;break;}
  if(dream_ui.page==LIVE&&dream_ui.live_view==LIVE_BROWSER){
   int id=browse_biome();
   if(id>=0&&id!=p->biome_id&&dream_cmd(MD_SCENE,0,id))app_say(md_biome_name(id),"");
  }else dream_cmd(MD_MUTE,dream_ui.track,!p->track[dream_ui.track].mute);
  break;
 case B_PLAY:
  if(dream_ui.page==ITINERA&&!md_playing()){
   dream_cmd(MD_ITINERA_START,radio_preset,radio_settings());
  }else dream_cmd(MD_PLAY,0,!md_playing());
  break;case B_ARP:dream_cmd(MD_LOCK,0,!p->locked);break;
 case B_REC:if(dream_ui.page==ITINERA){radio_saved[radio_preset]=0;break;}if(dream_ui.page==BANKS){bank_action(BANK_CLEAR_ASK);break;}
 if(dream_ui.page==SEQ_TOOLS){app_say("OPEN TRACK TO RECORD","");break;}
 if(dream_ui.page==SEQ_GRID||dream_ui.page==SEQ_RULES){dream_cmd(MD_RECORD,dream_ui.track,(md_record_mode()+1)%3);break;}
 if(dream_ui.page==LIVE&&dream_ui.live_view==LIVE_BROWSER){browse_toggle_favorite();break;}if(dream_ui.page==MIDI_LEARN||dream_ui.page==MIDI_MAP){dream_cmd(MD_LEARN_CLEAR,learn_slot,0);break;}if(dream_ui.page==KIT_SEQ){dream_cmd(MD_RECORD,3,(md_record_mode()+1)%3);break;}if((dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)){dream_cmd(MD_KIT_TRIGGER,3,p->kit.pad);break;}if(dream_ui.page==SEQUENCE||dream_ui.page==STEP_DETAIL||dream_ui.page==SEQ_FLOW){dream_cmd(MD_RECORD,dream_ui.track,(md_record_mode()+1)%3);break;}dream_cmd((dream_ui.page==SYSTEM||dream_ui.page==MIDI)?MD_PANIC:MD_MUTATE,0,0);break;case B_ENV:dream_cmd(MD_RETURN,0,0);break;
 case B_LFO:dream_cmd(MD_FREEZE,0,!p->freeze);break;
 case B_OCTDN:dream_ui.octave=clamp(dream_ui.octave-1,-2,2);break;case B_OCTUP:dream_ui.octave=clamp(dream_ui.octave+1,-2,2);break;
 case B_SAVE:if(dream_store_hold){app_say("USB BACKUP IN PROGRESS","");break;}if(dream_ui.page==BANKS){bank_action(1);break;}if(md_playing())app_say("STOP BEFORE SAVING","");else if(md_clock_source())app_say("USE INTERNAL CLOCK TO SAVE","");else {
  dream_sync_mappings();
  if(dream_ui.page==USB_AUDIO){
   app_say(dream_save_preferences()?"SAVE FAILED":"AUDIO PREFS SAVED","");
   break;
  }
  if(dream_ui.page==LIVE&&dream_ui.live_view==LIVE_BROWSER){
   app_say(dream_save_preferences()?"SAVE FAILED":"FAVORITES SAVED","");
   break;
  }
  uint32_t f=plat_lock();
  bank_patch_io=*md_project_state();
  plat_unlock(f);
  int failed=plat_store_save(OBJ_PATCH,&bank_patch_io,sizeof bank_patch_io);
  if(!failed)failed=dream_save_preferences();
  // Hash the image that reached flash, not the live patch: a MIDI edit
  // during the write must stay unsaved.
  if(!failed){f=plat_lock();md_mark_saved_from(&bank_patch_io);plat_unlock(f);}
  app_say(failed?"SAVE FAILED":"BIOME + PREFS SAVED","");
 }break;
 }
}
static void editor_expire(uint32_t now);
void app_input_only(void) {
 unsigned b,now=plat_ms(),btn=plat_buttons(),keys=plat_keys();const md_patch *p=md_state();int d;
 /* Unsigned elapsed times remain correct across the 32-bit clock rollover. */
 if(intro_active&&(uint32_t)(now-intro_start)>=3200)intro_active=0;
 if(dream_ui.msg[0]&&(uint32_t)(now-dream_ui.msg_start)>=1400)dream_ui.msg[0]=0;
 for(int i=0;i<6;i++)if((pulse_active&(1u<<i))&&(uint32_t)(now-pulse_start[i])>=160)pulse_active&=~(1u<<i);
 if(bank_armed>=0&&(dream_ui.page!=BANKS||(uint32_t)(now-bank_arm_start)>BANK_CONFIRM_MS))bank_armed=-1;
 editor_expire(now);
 uint32_t lock=plat_lock();
 const int was_radio=radio_view.active;
 const unsigned old_chapter=radio_view.chapter;
 md_itinera_read(&radio_view);
 plat_unlock(lock);
 if(radio_view.active){
  const unsigned released=dream_ui.btn&~btn;
  dream_ui.page=ITINERA;dream_ui.help=-1;radio_preset=radio_view.preset;
  if(!was_radio||old_chapter!=radio_view.chapter){radio_activity=now;dream_ui.dirty=1;}
  dream_ui.btn=btn;dream_ui.keys=keys;dream_ui.held=0;
  for(b=0;b<7;b++)plat_enc(b);
  if(released&(1u<<B_PLAY))dream_cmd(MD_PLAY,0,0);
  return;
 }
 if(was_radio){
  dream_ui.dirty=1;
  dream_ui.btn=btn;dream_ui.keys=keys;
 }
 for(b=0;b<NB;b++) {
  if((btn>>b&1)&&!(dream_ui.btn>>b&1)){dream_ui.down[b]=now;dream_dismiss_intro();}
  if((btn>>b&1)&&!(dream_ui.held>>b&1)&&now-dream_ui.down[b]>=850){
   dream_ui.held|=1u<<b;
   if(b==B_HOME&&md_pending_biome()<0&&bank_pending<0){dream_ui.page=ITINERA;dream_ui.help=-1;dream_ui.dirty=1;}
   else{dream_ui.help=b;ann_say(help_title[b],0,ANN_NOW);}
  }
  if(!(btn>>b&1)&&(dream_ui.btn>>b&1)){if(!(dream_ui.held>>b&1)){if(dream_ui.help>=0&&md_pending_biome()<0&&bank_pending<0&&!(dream_ui.page==BANKS&&b==B_SAVE))dream_ui.help=-1;else{dream_ui.help=-1;md_tap(b);}}dream_ui.held&=~(1u<<b);}
 }
 dream_ui.btn=btn;
 if(md_pending_biome()>=0||bank_pending>=0){for(unsigned k=0;k<7;k++)plat_enc(k);dream_ui.keys=keys;return;}
 if(dream_ui.page==ITINERA){
  plat_enc(EN_SELECT);plat_enc(EN_ALGO);plat_enc(EN_PRESET);
  for(b=0;b<4;b++)if((d=plat_enc(EN_K1+b)))radio_adjust(b,d);
  dream_ui.keys=keys;
  return;
 }
 if(dream_ui.track>=md_active_tracks())dream_ui.track=md_active_tracks()-1;
 if((d=plat_enc(EN_SELECT))){dream_dismiss_intro();dream_ui.track=clamp(dream_ui.track+d,0,md_active_tracks()-1);bank_armed=-1;}
 if((d=plat_enc(EN_ALGO))&&!(p->kit.enabled&&dream_ui.track==3)){dream_dismiss_intro();dream_cmd(MD_MODEL,dream_ui.track,clamp(p->track[dream_ui.track].model+d,0,MD_MODELS-1));dream_ui.page=SOUND;}
 if((d=plat_enc(EN_PRESET))&&dream_ui.help<0){dream_dismiss_intro();
  if(dream_ui.page==LIVE&&dream_ui.live_view==LIVE_BROWSER)browse_move(d);
  else if(dream_ui.page==SEQ_GRID||dream_ui.page==SEQ_TOOLS||dream_ui.page==SEQ_RULES)
   sequence_panel_set(dream_ui.page,1,sequence_panel_value(dream_ui.page,dream_ui.track,1)+d);
  else if(dream_ui.page>=MIDI_LEARN){if(dream_ui.page<=MIDI_MAP)learn_slot=clamp(learn_slot+d,0,15);else if(dream_ui.page<=LINK_MOTION)link_slot=clamp(link_slot+d,0,3);}
  else if(dream_ui.page>=JOURNEY&&dream_ui.page<=JOURNEY_KEEP){int id=p->journey.section[journey_edit].biome;id=id==65535?-1:id;dream_cmd(MD_JOURNEY_BIOME,journey_edit,clamp(id+d,-1,MD_BIOMES-1));}
  else if((dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)||(p->kit.enabled&&dream_ui.track==3&&dream_ui.page!=LIVE&&dream_ui.page!=BANKS)){dream_cmd(MD_KIT_PAD,3,clamp(p->kit.pad+d,0,7));}
  else if(dream_ui.page==BANKS){for(int j=0;j<=bank_char;j++)if(!bank_name[j])bank_name[j]=' ';bank_name[bank_char]=clamp(bank_name[bank_char]+d,32,126);bank_name[23]=0;bank_armed=-1;}else if(dream_ui.page==SOUND||dream_ui.page==FILTER||dream_ui.page==ENVELOPE||dream_ui.page==MIX||dream_ui.page==MIXER||dream_ui.page==BASSLINE||dream_ui.page==MODULATION||dream_ui.page==LFO_ROUTE||dream_ui.page==LFO_ROUTE2||dream_ui.page==SEQ_FLOW||(dream_ui.page==PHRASE||dream_ui.page==SEQUENCE||dream_ui.page==STEP_DETAIL)){
   int id=p->tone_preset[dream_ui.track];if(id<0)id=md_model_first_tone(p->track[dream_ui.track].model)-(d>0?1:0);
   id=clamp(id+d,0,MD_TONE_PRESETS-1);dream_cmd(MD_TONE_PRESET,dream_ui.track,id);app_say(md_tone_name(id),"");
  }else{int id=md_biome_step(p->biome_id,d);dream_cmd(MD_SCENE,0,id);app_say(md_biome_name(id),"");}
 }
 for(b=0;b<4;b++)if((d=plat_enc(EN_K1+b))&&dream_ui.help<0) {
  dream_dismiss_intro();const md_track *t=&p->track[dream_ui.track];int c=0,v=0,tr=dream_ui.track;
  if(dream_ui.page==LIVE&&dream_ui.live_view==LIVE_BROWSER){
   if(b==0)browse_change_filter(d);
   if(b==1)browse_move(d);
   continue;
  }
  if(dream_ui.page==LIVE&&dream_ui.live_view==LIVE_TRACKS&&b==3){
   performance_prefs.touch=clamp(performance_prefs.touch+d*5,0,100);
   uint32_t lock=plat_lock();
   md_set_velocity_touch(performance_prefs.touch);
   plat_unlock(lock);
   dream_ui.dirty=1;
   continue;
  }
  if(dream_ui.page==SEQ_GRID||dream_ui.page==SEQ_TOOLS||dream_ui.page==SEQ_RULES){
   int current=sequence_panel_value(dream_ui.page,tr,b);
   if(((dream_ui.page==SEQ_GRID&&b==2)||(dream_ui.page==SEQ_RULES&&b==3))&&!(p->kit.enabled&&tr==3)&&current<24)current=23;
   sequence_panel_set(dream_ui.page,b,current+d);
   continue;
  }
  if(dream_ui.page==MIDI_LEARN||dream_ui.page==MIDI_MAP){
   const md_midi_mapping *m=md_mapping(learn_slot);
   if(dream_ui.page==MIDI_LEARN){if(b==0)learn_slot=clamp(learn_slot+d,0,15);else if(b==1)dream_cmd(MD_LEARN_TARGET,learn_slot,m->target+d);else if(b==2)dream_cmd(MD_LEARN_TRACK,learn_slot,m->track+d);else dream_cmd(MD_LEARN_ARM,learn_slot,d>0);}
   else {int cs[]={MD_LEARN_PORT,MD_LEARN_CHANNEL,MD_LEARN_CC,MD_LEARN_ENABLE},vs[]={m->port,m->channel,m->cc,m->enabled};dream_cmd(cs[b],learn_slot,b==2?md_next_learn_cc(m->cc,d):vs[b]+d);}continue;
  }
  if(dream_ui.page==LIVING_LINKS||dream_ui.page==LINK_MOTION){const md_link *l=&p->links[link_slot];if(b==0)link_slot=clamp(link_slot+d,0,3);else {int cs[3],vs[3];if(dream_ui.page==LIVING_LINKS){int c[]={MD_LINK_SOURCE,MD_LINK_DEST,MD_LINK_TARGET},v[]={l->source,l->destination,l->target};memcpy(cs,c,sizeof c);memcpy(vs,v,sizeof v);}else {int c[]={MD_LINK_SIGNAL,MD_LINK_DEPTH,MD_LINK_RELEASE},v[]={l->signal,l->depth,l->release};memcpy(cs,c,sizeof c);memcpy(vs,v,sizeof v);}dream_cmd(cs[b-1],link_slot,vs[b-1]+d*(dream_ui.page==LINK_MOTION&&b>=2?20:1));}continue;}
  if(dream_ui.page==BIOME_DRIFT){int cs[]={MD_DRIFT_MODE,MD_DRIFT_POSITION,MD_DRIFT_RATE,MD_DRIFT_DEPTH},vs[]={p->drift.mode,p->drift.position,p->drift.rate,p->drift.depth};dream_cmd(cs[b],0,vs[b]+d*(b?20:1));continue;}
  if(dream_ui.page==DRIFT_SCENES){if(b<2&&d>0){if(p->drift.mode)app_say("TURN DRIFT OFF TO CAPTURE","");else dream_cmd(MD_DRIFT_CAPTURE,0,b);}else if(b==2)dream_cmd(MD_DRIFT_DEPTH,0,p->drift.depth+d*20);else if(b==3)dream_cmd(MD_DRIFT_MODE,0,p->drift.mode+d);continue;}
  if(dream_ui.page>=JOURNEY&&dream_ui.page<=JOURNEY_KEEP){const md_journey_section *a=&p->journey.section[journey_edit];
   if((dream_ui.page==JOURNEY&&b==1)||(dream_ui.page!=JOURNEY&&b==0)){journey_edit=clamp(journey_edit+d,0,7);continue;}
   if(dream_ui.page==JOURNEY_KEEP&&b==1){dream_ui.track=clamp(dream_ui.track+d,0,md_active_tracks()-1);continue;}
   if(dream_ui.page==JOURNEY){int cs[]={MD_JOURNEY_MODE,0,MD_JOURNEY_BIOME,MD_JOURNEY_LENGTH};int vs[]={p->journey.mode,0,a->biome==65535?-1:a->biome,p->journey.length};dream_cmd(cs[b],journey_edit,vs[b]+d);}
   else if(dream_ui.page==JOURNEY_TIME){int cs[]={0,MD_JOURNEY_BARS,MD_JOURNEY_REPEATS,MD_JOURNEY_TRANSPOSE},vs[]={0,a->bars,a->repeats,a->transpose};dream_cmd(cs[b],journey_edit,vs[b]+d);}
   else if(b==2)dream_cmd(MD_JOURNEY_KEEP_TRACK,journey_edit,dream_ui.track*2+(d>0));else dream_cmd(MD_JOURNEY_MODE,0,p->journey.mode+d);
   continue;
  }
  if((dream_ui.page>=KIT&&dream_ui.page<=KIT_SEQ)){const md_kit *k=&p->kit;const md_pad *a=&k->pads[k->pad];int cs[4],vs[4],mul[4]={1,1,1,1};
   if(dream_ui.page==KIT){int cc[]={MD_KIT_MODE,MD_KIT_BANK,MD_KIT_PAD,MD_KIT_HISS},vv[]={k->enabled,k->bank,k->pad,k->hiss};memcpy(cs,cc,sizeof cs);memcpy(vs,vv,sizeof vs);mul[3]=5;}
   else if(dream_ui.page==KIT_PAD){int cc[]={MD_KIT_TUNE,MD_KIT_LEVEL,MD_KIT_CUTOFF,MD_KIT_DECAY},vv[]={a->tune,a->level,a->cutoff,a->decay};memcpy(cs,cc,sizeof cs);memcpy(vs,vv,sizeof vs);mul[1]=mul[2]=mul[3]=20;}
   else if(dream_ui.page==KIT_SEND){int cc[]={MD_KIT_PAD,MD_KIT_SEND,MD_DELAY_SEND,MD_REVERB_SEND},vv[]={k->pad,a->send,p->ext[3].delay_send,p->ext[3].reverb_send};memcpy(cs,cc,sizeof cs);memcpy(vs,vv,sizeof vs);mul[1]=mul[2]=mul[3]=20;}
   else{int cc[]={MD_KIT_STEP,MD_KIT_GATE,MD_KIT_LENGTH,MD_KIT_GENERATE},vv[]={k->step,(k->gates[k->pad]>>k->step)&1,k->length,k->generator};memcpy(cs,cc,sizeof cs);memcpy(vs,vv,sizeof vs);}
   dream_cmd(cs[b],3,vs[b]+d*mul[b]);continue;
  }
  if(dream_ui.page==BANKS){if(b==0){bank_slot=clamp(bank_slot+d,0,bank_kind?63:15);bank_armed=-1;}else if(b==1){bank_kind=clamp(bank_kind+d,0,1);bank_slot=clamp(bank_slot,0,bank_kind?63:15);bank_armed=-1;}else if(b==2&&d>0)bank_action(0);else if(b==3)bank_char=clamp(bank_char+d,0,22);continue;}
  if(dream_ui.page==USB_AUDIO){if(b==0)set_local_monitor(d<0);continue;}
  if(dream_ui.page==MIDI){if(b==0)dream_cmd(MD_CLOCK_SOURCE,0,md_clock_source()+d);else if(b==1)dream_cmd(MD_MIDI_QUANTIZE,0,md_midi_quantize()+d);else if(b==2)dream_cmd(MD_MIDI_ROUTE,dream_ui.track,dream_prefs.midi_route[dream_ui.track]+d);else dream_cmd(MD_MIDI_MACRO,0,dream_prefs.midi_macro+d);continue;}
  if(dream_ui.page==BT_PAGE){
   if(b==0){if(md_playing()){app_say("STOP PLAY BEFORE RADIO","");continue;}int on=d>0;dream_prefs.reserved=(dream_prefs.reserved&~1)|on;if(!on)bt_scan_until=0;plat_ble_enable(on);app_say(on?"BLUETOOTH MIDI ON":"BLUETOOTH MIDI OFF","");}
   else if(b==1){if(!(dream_prefs.reserved&1)){app_say("TURN RADIO ON FIRST","");continue;}if(!btdev_count()||(d<0&&btdev_pick()==0)){bt_scan_until=plat_ms()+8000u;app_say("SCANNING MIDI DEVICES","");}else btdev_choose(btdev_pick()+d);}
   else if(b==2){if(d<0){plat_ble_disconnect();app_say("MIDI DEVICE RELEASED","");}else if(d>0&&(dream_prefs.reserved&1)){int ev=btdev_save();
    // Letting go suppresses BTDEV_LOST: release what the BLE device held here.
    if(ev==BTDEV_LET_GO)ble_link_lost=1;
    if(ev==BTDEV_NO_DEVICE){bt_scan_until=plat_ms()+8000u;app_say("SCANNING MIDI DEVICES","");}else app_say(ev==BTDEV_CONNECTING?"CONNECTING TO MIDI":btdev_word(ev),"");}}
   else {int old=(dream_prefs.reserved&2)!=0;int on=d>0;if(old&&!on)for(unsigned ch=0;ch<16;ch++)plat_midi_send(0x0bu|((0xb0u+ch)<<8)|(123u<<16),TO_USB|TO_BLE);dream_prefs.reserved=(dream_prefs.reserved&~2)|(on?2:0);}
   dream_ui.dirty=1;continue;
  }
  if(dream_ui.page==APPEARANCE){if(b==0)dream_prefs.motion=clamp(dream_prefs.motion+d,0,1);else if(b==1)dream_prefs.boot_sound=clamp(dream_prefs.boot_sound+d,0,1);else if(b==2)dream_intro();else dream_prefs.button_glow=clamp(dream_prefs.button_glow+d,0,2);continue;}
  if(dream_ui.page==LIVE){c=MD_MACRO;tr=b;v=p->macro[b]+d*20;}
  else if(dream_ui.page>=MIXER){const md_extension *e=&p->ext[tr];
   if(dream_ui.page==SEQUENCE){const md_sequence *q=&p->sequence[tr];const int cs[]={MD_SEQ_ENABLE,MD_SEQ_STEP,MD_SEQ_NOTE,MD_SEQ_LENGTH},vs[]={q->enabled,q->edit,q->steps[q->edit].note?q->steps[q->edit].note:23,q->length};c=cs[b];v=vs[b]+d;}
   else if(dream_ui.page==SEQ_FLOW){const md_motion *m=&p->motion;const int cs[]={MD_SEQ_DIRECTION,MD_SEQ_SHIFT,MD_SEQ_FEEL,MD_SEQ_GENERATE},vs[]={m->direction[tr],m->shift[tr],m->feel[tr],p->sequence[tr].generator};c=cs[b];v=vs[b]+d*(b==2?5:1);}
   else if(dream_ui.page==LFO_ROUTE2){if(p->kit.enabled&&tr==3){app_say("SELECT A SYNTH TRACK","");return;}const int cs[]={MD_LFO2_TARGET,MD_LFO2_SHAPE,MD_LFO2_DEPTH,MD_LFO2_RATE},vs[]={p->motion.target2[tr],p->motion.wave2[tr],p->motion.depth2[tr],p->motion.rate2[tr]};c=cs[b];v=vs[b]+d*(b>=2?20:1);}
   else if(dream_ui.page==LFO_ROUTE){if(p->kit.enabled&&tr==3){app_say("SELECT A SYNTH TRACK","");return;}const int cs[]={MD_LFO_TARGET,MD_LFO_SHAPE,MD_TIDES,MD_TIDES_RATE},vs[]={p->motion.target[tr],p->motion.wave[tr],e->tides,e->tides_rate};c=cs[b];v=vs[b]+d*(b>=2?20:1);}
   else if(dream_ui.page==STEP_DETAIL){const md_sequence *q=&p->sequence[tr];const md_step *st=&q->steps[q->edit];const int cs[]={MD_SEQ_VELOCITY,MD_SEQ_GATE,MD_SEQ_CHANCE,MD_SEQ_GENERATE},vs[]={st->velocity,md_step_gate(st),md_step_chance(st),q->generator};c=cs[b];v=vs[b]+d*(b==3?1:5);}
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
void app_ui_init(void) {app_redraw();dream_ui.frame_at=plat_ms();dream_ui.frame_wait=0;if(intro_pending){intro_pending=0;dream_intro();}}
#include "lights.c"
static int display_static_unchanged(void){
 uint32_t key[3]={0};
 // The static view paints the same audio-state snapshot used for its cache
 // key. MIDI edits cannot leave a mixed set of rows cached as a full frame.
 uint32_t lock=plat_lock();
 if(dream_ui.page==LIVE&&dream_ui.live_view!=LIVE_ATLAS&&!intro_active&&
    dream_ui.help<0&&md_pending_biome()<0&&bank_pending<0&&!dream_ui.msg[0]){
  const md_patch *p=md_state();
  key[0]=dream_ui.live_view|((uint32_t)md_palette()[0]<<8);
  if(dream_ui.live_view==LIVE_TRACKS){
   key[1]=dream_ui.track|(performance_prefs.touch<<3)|(md_playing()<<10)|(p->kit.enabled<<11);
   for(unsigned i=0;i<6;i++){
    key[1]|=(uint32_t)p->track[i].mute<<(12+i);
    key[2]|=(uint32_t)p->track[i].model<<(5*i);
   }
  }else{
   int id=browse_biome();
   key[1]=browse_filter|((uint32_t)(id+1)<<8)|((uint32_t)!!biome_favorite(id)<<17);
   key[2]=(uint32_t)(p->biome_id+1);
  }
 }
 plat_unlock(lock);
 int same=key[0]&&key[0]==display_static_frame[0]&&key[1]==display_static_frame[1]&&key[2]==display_static_frame[2];
 memcpy(display_static_frame,key,sizeof key);
 return same;
}
static void draw_display(void){
 const int repaint=display_repaint;
 display_repaint=0;
 if(display_static_unchanged()&&!repaint){display_tiles_skipped+=DISPLAY_TILES;return;}
 for(unsigned tile=0;tile<DISPLAY_TILES;tile++){
  unsigned y=tile*DREAM_TILE_ROWS;
  cv_begin(240,DREAM_TILE_ROWS,MD_BG);
  cv_oy=-(int)y;
  md_draw_all();
  // Retain only a signature, never a second pixel buffer. A mirror refresh or
  // a return from a system screen always sends the complete display.
  // Z-2: two pixels a step (one multiply per pair, half the work of a pixel
  // a step); only compared with this tile's own last value.
  uint32_t hash=2166136261u;
  for(unsigned i=0;i<240u*DREAM_TILE_ROWS;i+=2)hash=(hash^((uint32_t)cv_px[i]|(uint32_t)cv_px[i+1]<<16))*16777619u;
  if(repaint||hash!=display_hashes[tile]){
   display_hashes[tile]=hash;
   // Only changed tiles are transformed. The LCD and USB viewer see the
   // same RGB565 pixels, including Help and the startup animation.
   if(dream_inverted())for(unsigned i=0;i<240u*DREAM_TILE_ROWS;i++)cv_px[i]^=0xffffu;
   cv_blit(0,y);
   display_tiles_sent++;
  }else display_tiles_skipped++;
 }
}
void app_frame(void) {
 app_input_only();if(dream_ui.page!=BT_PAGE)bt_scan_until=0;btdev_page(dream_ui.page==BT_PAGE&&(dream_prefs.reserved&1)&&bt_scan_until&&(int32_t)(bt_scan_until-plat_ms())>0);int bev;while((bev=btdev_event())!=BTDEV_NONE){if(bev==BTDEV_LOST||bev==BTDEV_COMPUTER_GONE||bev==BTDEV_LET_GO)ble_link_lost=1;app_say(btdev_word(bev),"");dream_ui.dirty=1;}if(md_status()->quality_reductions!=quality_notice){quality_notice=md_status()->quality_reductions;app_say("ECO: DENSE PATCH","");}flower_time=dream_prefs.motion?plat_ms():0;for(int i=0;i<6;i++)if(md_status()->events[i]!=last_events[i]){last_events[i]=md_status()->events[i];pulse_start[i]=plat_ms();pulse_active|=1u<<i;}dream_lights(plat_ms());if((uint32_t)(plat_ms()-dream_ui.frame_at)<dream_ui.frame_wait&&!dream_ui.dirty)return;dream_ui.frame_at=plat_ms();uint32_t audio_load=plat_cpu_pct();dream_ui.frame_wait=dream_prefs.motion?(audio_load>72?96:audio_load>40?66:58):80;dream_ui.dirty=0;
 uint32_t frame_started=plat_ms();
 if(dream_ui.page==ITINERA){
  dream_ui.frame_wait=!dream_prefs.motion?500:audio_load>72?250:audio_load>40?125:66;
 }else{
  md_field_update();living_update(frame_started);timeline_update();sequence_view_update();
 }
 draw_display();
 ann_tick(plat_ms());
 uint32_t elapsed=plat_ms()-frame_started;if(elapsed>ui_frame_peak_ms)ui_frame_peak_ms=elapsed;ui_frames++;
}
#ifdef PLAT_HOST
int app_host_cmd(const char *cmd,const char *a,const char *b,const char *c,const char *d){
 (void)c;(void)d;
 // Host-only listening probe. No test interface is compiled into the FM-1.
 if(!strcmp(cmd,"journey")){dream_cmd(atoi(a),atoi(b),atoi(c));dream_ui.dirty=1;return 1;}
 if(!strcmp(cmd,"page")){dream_ui.page=clamp(atoi(a),0,DREAM_PAGE_COUNT-1);dream_ui.dirty=1;return 1;}
 if(!strcmp(cmd,"scene")){dream_cmd(MD_SCENE,0,atoi(a));dream_ui.dirty=1;return 1;}
 if(!strcmp(cmd,"mute")){dream_cmd(MD_MUTE,atoi(a),atoi(b));dream_ui.dirty=1;return 1;}
 if(!strcmp(cmd,"level")){dream_cmd(MD_LEVEL,atoi(a),atoi(b));dream_ui.dirty=1;return 1;}
 if(!strcmp(cmd,"nograin")){dream_cmd(MD_MACRO,3,0);dream_cmd(MD_FEEDBACK,0,0);for(int i=0;i<6;i++)dream_cmd(MD_SEND,i,0);dream_ui.dirty=1;return 1;}
 if(!strcmp(cmd,"noroom")){dream_cmd(MD_FX_LEVEL,0,0);for(int i=0;i<6;i++){dream_cmd(MD_DELAY_SEND,i,0);dream_cmd(MD_REVERB_SEND,i,0);}dream_ui.dirty=1;return 1;}
 if(!strcmp(cmd,"nodelay")){for(int i=0;i<6;i++)dream_cmd(MD_DELAY_SEND,i,0);dream_ui.dirty=1;return 1;}
 if(!strcmp(cmd,"noreverb")){for(int i=0;i<6;i++)dream_cmd(MD_REVERB_SEND,i,0);dream_ui.dirty=1;return 1;}
 return 0;
}
int app_expect(const char *what,int *got) {
 if(!strcmp(what,"radio_active")){*got=radio_view.active;return 1;}
 if(!strcmp(what,"radio_preset")){*got=radio_preset;return 1;}
 if(!strcmp(what,"radio_settings")){*got=radio_settings();return 1;}
 if(!strcmp(what,"tiles_skipped")){*got=display_tiles_skipped>0;return 1;}
 if(!strcmp(what,"living_profile")){*got=living.profile;return 1;}
 if(!strcmp(what,"timeline_next")){*got=timeline.next;return 1;}
 if(!strcmp(what,"timeline_keep")){*got=timeline.keep;return 1;}
 if(!strcmp(what,"large_tracks")){*got=dream_ui.live_view==LIVE_TRACKS;return 1;}
 if(!strcmp(what,"live_view")){*got=dream_ui.live_view;return 1;}
 if(!strcmp(what,"browse_biome")){*got=browse_biome();return 1;}
 if(!strcmp(what,"browse_family")){*got=md_biome_family_index(browse_biome());return 1;}
 if(!strcmp(what,"browse_favorite")){*got=biome_favorite(browse_biome());return 1;}
 if(!strcmp(what,"velocity_touch")){*got=performance_prefs.touch;return 1;}
 if(!strcmp(what,"selected_muted")){*got=md_state()->track[dream_ui.track].mute;return 1;}
 if(!strcmp(what,"world_family")){*got=(int)(md_world_dna()%10u);return 1;}
 if(!strcmp(what,"led_keys")){*got=(int)dream_ui.keys;return 1;}
 if(!strcmp(what,"led_buttons")){*got=(int)dream_light_buttons();return 1;}if(!strcmp(what,"led_target_ns")){*got=(int)dream_light_target(plat_ms());return 1;}if(!strcmp(what,"led_note")){*got=(int)dream_light_note(plat_ms());return 1;}if(!strcmp(what,"record_mode")){*got=md_record_mode();return 1;}
 if(!strcmp(what,"journey_active")){*got=md_journey_active();return 1;}if(!strcmp(what,"journey_at")){*got=md_journey_position();return 1;}
 if(!strcmp(what,"unsaved")){*got=md_unsaved();return 1;}if(!strcmp(what,"pending_biome")){*got=md_pending_biome();return 1;}if(!strcmp(what,"kit_mode")){*got=md_state()->kit.enabled;return 1;}
 if(!strcmp(what,"patch_valid")){*got=md_validate(md_state());return 1;}
 if(!strcmp(what,"field_density")){*got=field_density;return 1;}
 if(!strcmp(what,"field_size")){*got=field_size;return 1;}
 if(!strcmp(what,"ble_enabled")){*got=(dream_prefs.reserved&1)!=0;return 1;}
 if(!strcmp(what,"midi_thru")){*got=(dream_prefs.reserved&2)!=0;return 1;}
 if(!strcmp(what,"ble_events")){*got=(int)md_status()->events[dream_ui.track];return 1;}
 if(!strcmp(what,"cutoff")){*got=md_state()->track[dream_ui.track].cutoff;return 1;}
 if(!strcmp(what,"clock_source")){*got=md_clock_source();return 1;}
 if(!strcmp(what,"midi_held_port")){*got=md_midi_held_port(dream_ui.track);return 1;}
 if(!strcmp(what,"help_context")){*got=md_context_help();return 1;}
 if(!strcmp(what,"local_monitor_muted"))*got=app_local_monitor_muted();
 else if(!strcmp(what,"motion"))*got=dream_prefs.motion;else if(!strcmp(what,"boot_sound"))*got=dream_prefs.boot_sound;else if(!strcmp(what,"button_glow"))*got=dream_prefs.button_glow;else if(!strcmp(what,"intro"))*got=intro_active&&(uint32_t)(plat_ms()-intro_start)<3200;else if(!strcmp(what,"track"))*got=dream_ui.track;else if(!strcmp(what,"model"))*got=md_state()->track[dream_ui.track].model;else if(!strcmp(what,"help"))*got=dream_ui.help;
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

void app_splash(void){for(unsigned y=0;y<240;y+=DREAM_TILE_ROWS){cv_begin(240,DREAM_TILE_ROWS,MD_BG);cv_oy=-(int)y;atlas_plate(50,46,140,108,dream_colors[2]);atlas_brand_mark(99,100);cv_text(42,168,&FONT_B,"MUTABLE DREAMS",MD_INK);md_text(60,194,"A BIOME IS WAKING",MD_DIM);cv_blit(0,y);}}
