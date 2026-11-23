/* Host-only Elements body lifetime/complete-state regression. */
#define MD_ENGINE_STATE_NO_MAIN
#include "engine_union_probe.cpp"
static void command(int c,int t,int value){assert(md_command_valid(c));assert(md_command_allowed(c));assert(md_command(c,t,value));}
int main(){
 const int forms[]={0,333,334,666,667,1000};unsigned cases=0;
 for(unsigned quality=0;quality<2;quality++)for(unsigned count=1;count<=6;count++)for(int form:forms)for(unsigned fx=0;fx<2;fx++){
  md_init();pos=509;stem_rng=0x2545f491u;
  md_patch p=*md_state();p.biome_id=-1;p.seed=0xfadecafeu;p.tempo=220;p.quality=quality;p.journey.mode=0;p.kit.enabled=0;
  p.fx_level=fx?1000:0;p.delay_fb=800;p.reverb_time=850;p.macro[1]=p.macro[3]=fx?700:0;
  for(unsigned i=0;i<6;i++){
   auto &t=p.track[i];t.model=MD_SATIN;t.mute=0;t.level=200+i*30;t.note=36+i*7;t.attack=50;t.decay=1000;t.filter=i%4;t.cutoff=640;t.resonance=710;t.send=fx?600:0;t.shape=form;t.hits=t.length;t.ratchet=4;
   p.ext[i].delay_send=p.ext[i].reverb_send=fx?600:0;p.ext[i].warps=210;
  }
  assert(md_load(&p));
#ifdef MD_ENGINE_UNION
  // SATIN owns no engine member. Poison only inactive storage, before all
  // six slots are rebuilt through public md_load/reset/placement construction.
  for(unsigned i=0;i<6;i++){assert(!(voice_engine_live&(1u<<i)));memset(static_cast<void*>(&voices[i].engine),0xa5,sizeof voices[i].engine);}
#endif
  for(unsigned i=0;i<6;i++){p.track[i].model=MD_ELEMENTS;p.track[i].mute=i>=count;}
  assert(md_load(&p));md_set_air(fx?3:0);md_set_chorus(fx);md_set_width(fx?3:0);md_set_vintage(fx?2:0);
  command(MD_PLAY,0,1);
  for(unsigned b=0;b<384;b++){
   if(b==96){command(MD_QUALITY,0,1-quality);for(unsigned i=0;i<6;i++)command(MD_FILTER,i,(i+1)%4);}
   if(b==160)for(unsigned i=0;i<6;i++)command(MD_MUTE,i,i%2);
   if(b==224)for(unsigned i=0;i<6;i++){command(MD_MUTE,i,0);command(MD_TRIGGER,i,48+i);}
   if(b==256)command(MD_PLAY,0,0);
   if(b%53==0&&b<256)for(unsigned i=0;i<6;i++)md_midi(0x90|i,48+i,70+i*5,0);
   if(b%53==9&&b<256)for(unsigned i=0;i<6;i++)md_midi(0x80|i,48+i,0,0);
   render(b);
  }
  assert(!md_playing());
  command(MD_KIT_MODE,0,1);render(384);command(MD_KIT_MODE,0,0);render(385);
  command(MD_PANIC,0,1);render(386);
  // md_load resets all six; Return resets each genuinely changed model.
  assert(md_load(&p));for(unsigned i=0;i<6;i++)command(MD_MODEL,i,MD_SATIN);render(387);
  command(MD_RETURN,0,1);render(388);for(unsigned i=0;i<6;i++)assert(voices[i].model==MD_ELEMENTS);
  assert(md_load(&p));render(389);
  // The next iteration's md_init destroys the live Elements objects. Also
  // repeat it here while they are live, before a second all-Elements load.
  md_init();assert(md_load(&p));render(390);cases++;
 }
 const int models[]={MD_ELEMENTS,MD_RINGS,MD_VA,MD_ELEMENTS,MD_SATIN,MD_FM};
 for(unsigned quality=0;quality<2;quality++)for(unsigned rotation=0;rotation<6;rotation++)for(int form:forms){
  md_init();pos=509;stem_rng=0x2545f491u;
  md_patch p=*md_state();p.seed=0x123abcdeu;p.biome_id=-1;p.quality=quality;p.tempo=220;p.journey.mode=0;p.kit.enabled=0;p.fx_level=700;
  for(unsigned i=0;i<6;i++){auto &t=p.track[i];t.model=MD_SATIN;t.mute=0;t.level=300+i*35;t.note=42+i*5;t.filter=i%4;t.cutoff=780;t.resonance=320;t.shape=form;t.harm=90+i*150;t.decay=900;t.attack=130;}
  assert(md_load(&p));
#ifdef MD_ENGINE_UNION
  for(unsigned i=0;i<6;i++){assert(!(voice_engine_live&(1u<<i)));memset(static_cast<void*>(&voices[i].engine),0xa5,sizeof voices[i].engine);}
#endif
  for(unsigned i=0;i<6;i++)p.track[i].model=models[(i+rotation)%6];assert(md_load(&p));
  for(unsigned i=0;i<6;i++)command(MD_TRIGGER,i,48+i*3);
  for(unsigned b=0;b<192;b++){
   if(b==48)for(unsigned i=0;i<6;i++)command(MD_MODEL,i,models[(i+rotation+1)%6]);
   if(b==96)for(unsigned i=0;i<6;i++)command(MD_MODEL,i,p.track[i].model);
   if(b==112)for(unsigned i=0;i<6;i++)command(MD_TRIGGER,i,60+i);
   if(b==144)command(MD_PANIC,0,1);
   render(b);
  }
  md_init();assert(md_load(&p));render(192);cases++;
 }
 std::fprintf(stderr,"PASS %u Elements cases; uniform/mixed models, six slots, form boundaries0/333/334/666/667/1000, both qualities, FX, variable mode budgets, all filters, four ratchets, MIDI/gates/tails, inactive poison, Kit/Panic/Return/load/reinit; complete active Body/Exciter/RNG/float/stems/state traces\n",cases);assert(cases==216);
}
