#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>
static float lr[256];
static double render(unsigned n,FILE *file=0){double energy=0;while(n){unsigned z=n>128?128:n;md_render(lr,z);for(unsigned k=0;k<z*2;k++){assert(std::isfinite(lr[k])&&std::fabs(lr[k])<1);energy+=lr[k]*lr[k];}if(file)assert(fwrite(lr,8,z,file)==z);n-=z;}return energy;}
static md_patch dry(unsigned model){md_init();md_command(MD_SCENE,0,MD_BLANK_BIOME);render(1);md_patch p=*md_state();p.quality=0;p.macro[1]=p.macro[3]=0;p.fx_level=0;for(int i=0;i<6;i++){p.track[i].mute=i!=0;p.track[i].send=0;memset(&p.ext[i],0,sizeof p.ext[i]);}auto &t=p.track[0];t.model=model;t.level=700;t.attack=0;t.decay=850;t.tone=650;t.harm=400;t.shape=450;t.filter=0;t.pan=500;return p;}
int main(int argc,char**argv){
 static_assert(sizeof(md_patch)==2604,"No save format change");
 static_assert(MD_MODELS>=18&&MD_BIOMES>=197&&MD_TONE_PRESETS>=420,"Atlas dimensions");
 for(unsigned model=15;model<17;model++)for(int note: {24,36,48,60,72,84,96}){
  md_patch p=dry(model);assert(md_load(&p));md_midi(0x90,note,100,0);
  FILE *voicefile=0;if(argc>1){char path[1024];snprintf(path,sizeof path,"%s/voice-%u-note-%d.f32",argv[1],model,note);voicefile=fopen(path,"wb");assert(voicefile);}
  double energy=render(MD_RATE*2,voicefile);if(voicefile)fclose(voicefile);assert(energy>.01);md_midi(0x80,note,0,0);render(MD_RATE);
  assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
  md_command(MD_PANIC,0,0);assert(render(512)==0);
 }
 puts("PASS new voices: seven registers, finite stereo, audible, note release and exact panic silence.");
 for(int id=177;id<189;id++){
  md_init();md_command(MD_SCENE,0,id);render(1);assert(md_validate(md_state()));md_patch p=*md_state();
  md_patch restored;assert(md_decode(&restored,&p,sizeof p)&&!memcmp(&p,&restored,sizeof p));
  assert(md_active_tracks()==(id==182||id==187?4:6));md_command(MD_PLAY,0,1);
  FILE *f=0;if(argc>1){char path[1024];snprintf(path,sizeof path,"%s/biome-%d.f32",argv[1],id);f=fopen(path,"wb");assert(f);}
  assert(render(MD_RATE*16,f)>.01);if(f)fclose(f);
  assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 }
 puts("PASS twelve authored worlds: valid, audible, editable/save roundtrip, kit/synth rack, finite stereo.");
 for(int id=380;id<404;id++){
  md_init();md_command(MD_SCENE,0,MD_BLANK_BIOME);render(1);md_command(MD_TONE_PRESET,0,id);render(1);
  assert(md_state()->tone_preset[0]==id&&md_validate(md_state()));md_command(MD_TRIGGER,0,60);assert(render(MD_RATE)>.001);
 }
 for(int model=15;model<17;model++){
  md_patch p=dry(model);for(int i=0;i<6;i++){p.track[i]=p.track[0];p.track[i].mute=0;p.track[i].hits=8;p.track[i].length=16;p.track[i].note=36+i*7;p.track[i].send=500;}
  p.macro[3]=850;p.grain_size=950;p.feedback=500;assert(md_load(&p));md_command(MD_PLAY,0,1);render(MD_RATE*8);
  assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 }
 puts("PASS 24 tone presets and six-voice string/organ with dense granular bus (offline; not device CPU qualification).");
}
