/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
static constexpr unsigned itinera_scenes=32;
static constexpr unsigned itinera_biome_base=64;
static constexpr uint8_t itinera_biome_code(unsigned id){return id-itinera_biome_base;}
#define ITINERA_PAIR(i) itinera_biome_code(anchors[i]),itinera_biome_code(extras[i])
struct ItineraPreset {
 const char *name;
 uint8_t biomes[itinera_scenes];
 uint8_t tempo, pace, hall;
 // Interleave the anchors and the additional scenes. The final anchor
 // closes the story at every density, while each slot costs one byte.
 constexpr ItineraPreset(const char *title,const uint16_t (&anchors)[16],const uint16_t (&extras)[16],
                         uint8_t bpm,uint8_t speed,uint8_t space):
  name(title),biomes{ITINERA_PAIR(0),ITINERA_PAIR(1),ITINERA_PAIR(2),ITINERA_PAIR(3),
                     ITINERA_PAIR(4),ITINERA_PAIR(5),ITINERA_PAIR(6),ITINERA_PAIR(7),
                     ITINERA_PAIR(8),ITINERA_PAIR(9),ITINERA_PAIR(10),ITINERA_PAIR(11),
                     ITINERA_PAIR(12),ITINERA_PAIR(13),ITINERA_PAIR(14),
                     itinera_biome_code(extras[15]),itinera_biome_code(anchors[15])},
  tempo(bpm),pace(speed),hall(space) {}
};
#undef ITINERA_PAIR
static constexpr ItineraPreset itinera_presets[MD_ITINERA_PRESETS]={
 {"Hortus Quietus", {77,170,177,193,205,181,209,204,213,211,228,267,254,264,269,185},
  {78,82,86,88,95,99,122,138,141,149,178,186,216,230,245,265}, 62,0,1},
 {"Via Siderum", {168,172,173,207,217,220,234,242,263,244,271,251,253,256,260,169},
  {85,108,114,119,127,136,146,158,163,165,186,202,208,235,243,249}, 68,1,2},
 {"Silva Nocturna", {177,181,205,193,241,198,213,202,254,211,268,227,269,264,257,188},
  {67,70,74,82,86,88,95,97,99,125,138,141,147,159,178,230}, 72,1,1},
 {"Mare Nebularum", {176,188,183,208,243,222,247,242,248,253,260,254,263,256,257,211},
  {73,79,84,98,100,112,115,117,131,133,139,155,157,249,244,265}, 61,0,2},
 {"Lumen Primum", {185,179,209,194,215,199,222,207,245,214,253,216,210,224,269,170},
  {66,68,75,81,87,91,93,94,154,166,174,180,200,223,247,254}, 76,1,1},
 {"Terra Pluvia", {190,189,174,191,241,195,194,242,198,197,225,202,239,254,268,177},
  {70,79,83,88,98,104,115,117,123,133,138,147,155,160,223,248}, 83,2,0},
 {"Aurora Polaris", {244,173,207,217,220,234,235,242,169,251,227,253,208,263,271,168},
  {85,108,114,119,127,130,136,146,149,158,163,165,222,221,231,243}, 74,2,2},
 {"Somnus Lenis", {204,170,178,181,216,211,228,224,233,227,265,234,257,253,260,77},
  {65,69,71,72,78,79,80,89,95,96,98,100,109,114,122,130}, 57,0,1},
 {"Murmur Arenae", {266,174,184,232,194,246,219,250,225,258,237,262,239,255,261,197},
  {80,90,101,102,105,106,111,113,120,129,135,161,179,215,233,247}, 79,1,1},
 {"Speculum Glaciei", {251,207,234,208,242,220,231,235,243,244,253,256,271,260,263,217},
  {73,79,84,100,114,130,133,146,158,163,165,176,183,209,249,247}, 58,0,2},
 {"Ignes Errantes", {202,172,186,221,191,226,198,239,200,249,203,252,225,254,268,265},
  {68,88,91,97,102,103,107,135,159,174,185,195,201,210,214,245}, 81,2,1},
 {"Fungi Lucentes", {264,206,205,223,213,245,224,259,228,236,243,260,248,254,267,257},
  {67,74,82,86,95,103,109,125,138,141,147,177,181,216,230,269}, 67,1,2},
 {"Terra Resonans", {223,190,194,202,240,206,215,264,214,266,199,237,195,226,265,247},
  {74,88,101,121,125,130,137,145,152,158,162,186,209,228,231,269}, 85,2,0},
 {"Aer Silens", {253,178,176,222,185,228,207,257,244,260,271,254,208,242,211,267},
  {70,85,94,98,103,117,130,133,145,152,158,173,181,199,217,249}, 56,0,2}
};
static md_patch itinera_undo;
static constexpr unsigned itinera_fade_frames=12u*MD_RATE;
static struct {
 uint32_t start,length,seed;
 uint16_t section_steps,part;
 uint16_t macros[4];
 uint8_t active,preset,sections,hall,saved_clock;
 /* K-1: each slot's gain at the last quantum (a scene's staggered end and
  * its rhythm's soft arrival) */
 float fade[MD_TRACKS];
 /* K-3: room, return, echo feedback and tempo glide from the last scene's
  * values to this one's over its first bar */
 int16_t glide_from[4],glide_to[4];
 /* K-5: the dream's level: short (8 s) and long (120 s) mean power before the
  * gain, and the gain itself (1 = none, +-4 dB at most, 30 s to move) */
 float level_short,level_long,gain;
 /* K-8: with VARIED changes, a scene's resting bars (steps from its start)
  * in which only pads, bass, room and tape sound */
 uint16_t rest_at,rest_len;
 uint8_t pace;
} itinera;
static unsigned itinera_remaining(){
 const unsigned elapsed=stats.frames-itinera.start;
 return elapsed<itinera.length?itinera.length-elapsed:0;
}
/* K-1: what a slot is in a scene: 0 rhythm, 1 a lead or keys, 2 a pad, bass or
 * drone. Rhythm leaves first and comes back last; pads and leads carry the
 * change (a scene's sparse pads alone left gaps when the leads also waited). */
static unsigned itinera_kind(const md_patch &p,unsigned i){
 if(p.kit.enabled&&i>=3)return 0;
 switch(p.track[i].model){
 case MD_KICK:case MD_SNARE:case MD_HAT:case MD_SKIN:return 0;
 case MD_CHOIR:case MD_SATIN:case MD_SPECTRAL:case MD_BREATH:case MD_MELLOW:case MD_SILK:
 case MD_VOWEL:case MD_BASS:case MD_REED:case MD_BOWED:case MD_AERO:return 2;
 default:return 1;
 }
}
// A scene that is mostly rhythm (nothing else audible) keeps its rhythm whole.
static unsigned itinera_role(const md_patch &p,unsigned i){
 const unsigned kind=itinera_kind(p,i);
 if(kind)return kind;
 for(unsigned j=0;j<(p.kit.enabled?3u:6u);j++)
  if(itinera_kind(p,j)&&!p.track[j].mute&&p.track[j].level>0)return 0;
 return 2;
}
/* K-1: a slot's gain in an Itinera scene (elapsed, remaining: steps,
 * continuous; windows scaled down for short scenes). Its end: rhythm fades out
 * over the remaining 32..16 steps, leads ease back to a third over the last
 * 16..4, pads and bass hold to the last step (its sixteenth's fade as before);
 * the dream's last scene keeps everything. Its start: rhythm comes in at a
 * third and grows over the first bar; everything else at once. */
static float itinera_fade_gain(unsigned i,float elapsed,float remaining){
 const unsigned role=itinera_role(patch,i);
 if(role==2)return 1.f;
 float arrive=1.f;
 if(role==0&&itinera.part>1&&elapsed<16.f){const float t=elapsed<0?0:elapsed/16.f;arrive=.35f+.65f*t*t*(3-2*t);}
 if(itinera.part>=itinera.sections)return arrive;
 static const float start[2]={32,16},end[2]={16,4},floor_gain[2]={0,.35f};
 const float scale=itinera.section_steps<64?itinera.section_steps/64.f:1.f;
 const float a=start[role]*scale,b=end[role]*scale;
 float t=(remaining-b)/(a-b);t=t<0?0:t>1?1:t;
 return arrive*(floor_gain[role]+(1-floor_gain[role])*t*t*(3-2*t));
}
/* K-8: is slot i resting now (no new notes)? Pads and bass never rest. */
static bool itinera_resting(unsigned i){
 if(!itinera.active||!itinera.rest_len||journey_ticks<itinera.rest_at||journey_ticks>=itinera.rest_at+itinera.rest_len)return false;
 return itinera_role(patch,i)!=2;
}
/* K-2: the next scene's key, moved by at most a tritone so that its root is
 * the last one's, or a fifth or fourth from it. Root and transpose move
 * together: every note keeps its place in the scene's own scale. */
static void itinera_bridge(md_patch &next,int sounding_root){
 static const int steps[3]={0,7,5};
 int best=0,dist=99;
 for(int k=0;k<3;k++){
  int d=((sounding_root+steps[k]-next.root)%12+12)%12;if(d>6)d-=12;
  if((d<0?-d:d)<dist){dist=d<0?-d:d;best=d;}
 }
 if(next.transpose+best< -24||next.transpose+best>24)return;
 next.root=(next.root+best+12)%12;next.transpose+=best;
}
static void itinera_stage(unsigned at){
 const auto &preset=itinera_presets[itinera.preset];
 const unsigned order=at*(itinera_scenes-1)/(itinera.sections-1);
 const int sounding_root=patch.root;
 factory_scene(previous,itinera_biome_base+preset.biomes[order]);
 if(at)itinera_bridge(previous,sounding_root);
 previous.seed=(previous.seed^itinera.seed^((itinera.part+1)*0x9e3779b9u))|1u;
 previous.locked=0;
 previous.freeze=0;
 previous.tempo=bound((preset.tempo*3+previous.tempo)/4+(int)(itinera.part%3)*2-2,48,100);
 previous.reverb_time=bound(previous.reverb_time+50+itinera.hall*75,0,820);
 previous.fx_level=bound(previous.fx_level+35+itinera.hall*60,0,740);
 for(unsigned i=0;i<MD_TRACKS;i++){
  previous.ext[i].reverb_send=bound(previous.ext[i].reverb_send+itinera.hall*65,0,650);
 }
}
static void itinera_enter(){
 const unsigned left=itinera.part<itinera.sections?itinera.sections-itinera.part:1;
 const int seconds=(itinera_remaining()+MD_RATE-1)/MD_RATE/left;
 const int drift=(int)((itinera.seed>>(itinera.part%23))%17)-8;
 // Budget the remaining time against the remaining sections. Leave the final
 // patch playing until the session fades out, even when the bar rounds down.
 const unsigned time=left>1?seconds+seconds*drift/100:seconds+8;
 itinera.section_steps=bound((time*patch.tempo+120)/240,1,4095)*16;
 for(unsigned i=0;i<4;i++)itinera.macros[i]=patch.macro[i];
 for(unsigned i=0;i<MD_TRACKS;i++)itinera.fade[i]=itinera.part&&itinera_role(patch,i)==0?.35f:1.f;
 // K-8: VARIED dreams rest for one or two bars in about every other scene,
 // between 45 and 60 % of it (never in the last scene, never in short ones).
 itinera.rest_at=itinera.rest_len=0;
 if(itinera.pace==2&&itinera.section_steps>=128&&itinera.part+1<itinera.sections){
  unsigned r=itinera.seed^((itinera.part+7)*0x9e3779b9u);r=random_word(r);
  if(r&1u){
   const unsigned bars=itinera.section_steps/16u;
   itinera.rest_at=(uint16_t)(bars*(45u+(r>>1)%15u)/100u*16u);
   itinera.rest_len=(r>>8)&1u?32:16;
  }
 }
 itinera.part++;
}
// K-3: the scene's room, return, echo feedback and tempo, eased in over a bar.
static void itinera_glide_begin(const int16_t from[4],bool first){
 const int16_t to[4]={(int16_t)patch.reverb_time,(int16_t)patch.fx_level,(int16_t)patch.delay_fb,(int16_t)patch.tempo};
 for(unsigned k=0;k<4;k++){itinera.glide_to[k]=to[k];itinera.glide_from[k]=first?to[k]:from[k];}
}
static void itinera_glide(){
 const float t=journey_ticks>=16u?1.f:journey_ticks/16.f,s=t*t*(3-2*t);
 int32_t *field[4]={&patch.reverb_time,&patch.fx_level,&patch.delay_fb,&patch.tempo};
 // (all four are positive: round to nearest; the glide ends on the scene's own value)
 for(unsigned k=0;k<4;k++)*field[k]=t>=1.f?itinera.glide_to[k]:(int32_t)(itinera.glide_from[k]+(itinera.glide_to[k]-itinera.glide_from[k])*s+.5f);
}
static void itinera_breathe(){
 if(journey_ticks%8)return;
 const unsigned phase=bound(journey_ticks*64u/itinera.section_steps,0u,64u);
 const unsigned chapter=(itinera.length-itinera_remaining())/(itinera.length/4);
 static constexpr int arc[]={-9,3,10,-7,-12};
 // Tone, motion and space have different envelopes in each world.
 static constexpr int8_t contour[MD_ITINERA_PRESETS][3]={
  { 6, 4, 5}, { 3,-6, 7}, { 5, 8, 2}, {-4, 3, 8},
  { 8, 4, 4}, { 3, 8, 3}, {-5,-7, 8}, {-4, 2, 4},
  { 5,-7, 3}, { 6, 2, 6}, { 4, 9, 4}, {-5, 7, 6},
  { 5,11, 2}, {-7,-3, 8}
 };
 const auto &shape=contour[itinera.preset];
 const int swell=(phase<32?phase:64-phase)-12+arc[chapter];
 const unsigned pulse=(phase+itinera.part*9)%64;
 const int tide=(int)(pulse<32?pulse:64-pulse)-16;
 patch.macro[0]=bound(itinera.macros[0]+swell*shape[0],0,1000);
 patch.macro[1]=bound(itinera.macros[1]+tide*shape[1],0,1000);
 // K-7: one slow arc over the whole dream: colour lower at its start and
 // end, brightest in the middle ("morning, noon, evening"); the grain's share
 // of the texture rises towards the end.
 const float dream=(float)(itinera.length-itinera_remaining())/itinera.length;
 const int day=(int)(-40.f+100.f*plaits::Sine(.5f*dream));
 const int dusk=(int)(120.f*dream*dream*(3-2*dream));
 patch.macro[2]=bound(itinera.macros[2]+swell*3-tide*2+day,0,1000);
 patch.macro[3]=bound(itinera.macros[3]+swell*shape[2]+dusk,0,1000);
}
static void itinera_begin(int preset,int settings){
 if(playing||pending_biome>=0||preset<0||preset>=MD_ITINERA_PRESETS||settings<0||settings>=36)return;
 static constexpr uint16_t minutes[4]={30,60,120,240};
 static constexpr uint8_t sections[4][3]={{8,12,16},{12,16,20},{16,20,24},{16,24,32}};
 itinera={};
 itinera.start=stats.frames;
 itinera.length=(unsigned)minutes[settings%4]*60u*MD_RATE;
 itinera.seed=(stats.frames^patch.seed^0x4d445241u)|1u;
 itinera.preset=preset;
 itinera.sections=sections[settings%4][settings/4%3];
 itinera.hall=settings/12;
 itinera.pace=settings/4%3;
 itinera.gain=1.f;
 itinera.saved_clock=clock_source;
 itinera.active=1;
 clock_source=0;
 journey_home=patch;
 itinera_undo=previous;
 journey_active=playing=1;
 step=0;untilstep=0;pending_biome=-1;
 sequence_rewind();
 memset(journey_origin,0,sizeof journey_origin);
 record_mode=0;record_note=record_step=-1;
 itinera_enter_first();
}
static void itinera_end(){
 clock_source=itinera.saved_clock;
 clock_age=clock_last=clock_pulse=0;
 clock_interval=0;
 itinera.active=0;
}
static void itinera_release_audio(float *lr,float *grain,float *delay,float *room,unsigned n){
 const unsigned left=itinera_remaining();
 if(left>=itinera_fade_frames+n)return;
 for(unsigned k=0;k<n;k++){
  const unsigned frames=left>k?left-k:0;
  const float gain=fminf(1.f,frames*(1.f/itinera_fade_frames));
  lr[k*2]*=gain;
  lr[k*2+1]*=gain;
  grain[k]*=gain;
  delay[k]*=gain;
  room[k]*=gain;
 }
}
