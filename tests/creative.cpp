#include "dsp/dreams.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstddef>
static float audio[128];
static void run(unsigned n=64){while(n){unsigned q=n>64?64:n;md_render(audio,q);for(unsigned i=0;i<2*q;i++)assert(std::isfinite(audio[i]));n-=q;}}
static void cmd(int c,int t,int v){assert(md_command(c,t,v));run();}
static md_patch init(){md_init();auto p=*md_state();p.kit.enabled=0;p.journey.mode=0;for(int i=0;i<6;i++)p.track[i].mute=0;assert(md_load(&p));return p;}
int main(){
 static_assert(sizeof(md_patch)==2604&&offsetof(md_patch,links)==2356&&sizeof(md_drift)==216,"v11 complete v10 prefix retained");
 auto p=init();unsigned char old[2356];memcpy(old,&p,sizeof old);uint32_t ver=10;memcpy(old+4,&ver,4);md_patch migrated;assert(md_decode(&migrated,old,sizeof old));assert(migrated.version==MD_PATCH_VERSION&&migrated.drift.mode==0&&migrated.drift.captured==0);memcpy(old+4,&migrated.version,4);assert(!memcmp(old,&migrated,sizeof old));
 for(auto &l:migrated.links)assert(!l.depth);auto bad=migrated;bad.links[0].source=6;assert(!md_validate(&bad));bad=migrated;bad.drift.mode=1;assert(!md_validate(&bad));bad=migrated;bad.drift.scene[1].track[5][7]=1001;assert(!md_validate(&bad));
 puts("PASS v10 migration preserves prefix, new controls default off, malformed tails rejected.");
 cmd(MD_LEVEL,0,600);cmd(MD_LEARN_TARGET,15,4);cmd(MD_LEARN_TRACK,15,0);cmd(MD_LEARN_ARM,15,1);assert(md_learn_armed()==15);
 md_midi(0xb2,64,127,0);assert(md_learn_armed()==15);md_midi(0xb2,74,127,0);assert(md_learn_armed()<0&&md_mapping(15)->cc==74&&md_mapping(15)->channel==3&&md_state()->track[0].level==600);
 md_midi(0xb2,74,0,0);assert(md_state()->track[0].level==600&&!md_learn_picked(15));md_midi(0xb2,74,100,0);assert(md_learn_picked(15)&&md_state()->track[0].level==100*1000/127);
 cmd(MD_LEVEL,0,500);assert(!md_learn_picked(15));md_midi(0xb2,74,0,2);assert(md_state()->track[0].level==500);md_midi(0xb1,74,127,0);assert(md_state()->track[0].level==500);
 md_midi(0xb2,74,64,0);assert(md_state()->track[0].level==64*1000/127);md_midi_lost(0);assert(!md_learn_picked(15));
 cmd(MD_LEARN_ARM,0,1);run(MD_RATE*21);assert(md_learn_armed()==-1);md_midi(0xb0,75,45,0);assert(!md_mapping(0)->enabled);
 auto m=*md_mapping(15);m.cc=6;assert(!md_set_mapping(0,&m));m.cc=74;m.reserved[0]=1;assert(!md_set_mapping(0,&m));
 cmd(MD_LEARN_CLEAR,15,0);assert(!md_mapping(15)->enabled);
 puts("PASS learn slot 16, reserved CC protection, capture without jumps, pickup/crossing, manual reset, port/channel isolation, timeout and clear.");
 // Learning an MSB macro consumes both the coarse and original fine partner.
 cmd(MD_LEARN_TARGET,0,4);cmd(MD_LEVEL,0,0);cmd(MD_LEARN_ARM,0,1);int macro=md_state()->macro[0];md_midi(0xbf,16,0,0);md_midi(0xbf,16,0,0);md_midi(0xbf,16,127,0);md_midi(0xbf,48,127,0);assert(md_state()->macro[0]==macro&&md_state()->track[0].level==1000);

 // Two controllers sharing a destination must hand over through pickup again.
 md_midi_mapping shared=*md_mapping(0);shared.cc=75;assert(md_set_mapping(1,&shared));md_midi(0xbf,75,127,0);assert(md_learn_picked(1));md_midi(0xbf,16,127,0);md_midi(0xbf,16,0,0);assert(!md_learn_picked(1));md_midi(0xbf,75,120,0);assert(md_state()->track[0].level==0);md_midi(0xbf,75,0,0);assert(md_learn_picked(1));
 puts("PASS learned CC overrides built-in assignment; its LSB cannot leak into the original macro.");

 for(int port=0;port<3;port++){init();cmd(MD_CUTOFF,3,600);cmd(MD_LEARN_TARGET,0,5);cmd(MD_LEARN_TRACK,0,3);cmd(MD_LEARN_ARM,0,1);md_midi(0xb4,74,0,port);assert(md_mapping(0)->port==port&&md_mapping(0)->channel==5);md_midi(0xb4,74,127,(port+1)%3);assert(md_state()->track[3].cutoff==600);md_midi(0xb4,74,0,port);md_midi(0xb4,74,127,port);assert(md_state()->track[3].cutoff==1000);}
 init();md_midi_mapping any={1,3,0,74,5,3,{0,0}};assert(md_set_mapping(0,&any));for(int port=0;port<3;port++){cmd(MD_CUTOFF,3,600);md_midi(0xb0+port,74,0,port);md_midi(0xb0+port,74,127,port);assert(md_state()->track[3].cutoff==1000);}
 puts("PASS software learn routing on USB/BLE/TRS and Any port/channel; physical transports require separate device qualification.");
 init();cmd(MD_DRIFT_CAPTURE,0,0);int a=md_state()->track[0].cutoff;cmd(MD_CUTOFF,0,123);cmd(MD_MACRO,0,200);cmd(MD_DRIFT_CAPTURE,0,1);assert(md_drift_ready());cmd(MD_DRIFT_MODE,0,1);assert(!md_command(MD_DRIFT_CAPTURE,0,0));cmd(MD_DRIFT_POSITION,0,1000);run(MD_RATE);assert(md_drift_position()>999&&md_state()->track[0].cutoff==123&&md_state()->drift.scene[0].track[0][1]==a);
 cmd(MD_DRIFT_MODE,0,2);int start=md_drift_position();run(MD_RATE*3);assert(abs(md_drift_position()-start)>10);auto saved=*md_project_state();cmd(MD_PANIC,0,0);assert(md_import(&saved,sizeof saved));assert(!memcmp(&saved,md_project_state(),sizeof saved));
 puts("PASS Drift captures, saved endpoints, mode gate, smooth live position and autonomous motion; base controls remain canonical.");
 init();cmd(MD_LINK_SOURCE,0,0);cmd(MD_LINK_DEST,0,1);cmd(MD_LINK_TARGET,0,0);cmd(MD_LINK_DEPTH,0,600);cmd(MD_TRIGGER,0,60);assert(md_link_value(0)>0);run(MD_RATE);assert(md_link_value(0)<.8f);assert(md_state()->links[1].depth==0);cmd(MD_MUTE,0,1);run();assert(md_link_value(0)==0);
 for(int j=0;j<4;j++){cmd(MD_LINK_SOURCE,j,j);cmd(MD_LINK_DEST,j,(j+1)%6);cmd(MD_LINK_SIGNAL,j,j);cmd(MD_LINK_TARGET,j,j);cmd(MD_LINK_DEPTH,j,j%2?-1000:1000);}cmd(MD_PLAY,0,1);run(MD_RATE*12);cmd(MD_PLAY,0,0);assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 puts("PASS links are isolated, muted sources silent, hit decay bounded; all four signal types and signed depths stay finite under playback.");
 cmd(MD_DRIFT_CAPTURE,0,0);cmd(MD_CUTOFF,1,350);cmd(MD_DRIFT_CAPTURE,0,1);cmd(MD_DRIFT_MODE,0,2);cmd(MD_JOURNEY_MODE,0,2);cmd(MD_JOURNEY_LENGTH,0,2);cmd(MD_JOURNEY_BARS,0,1);cmd(MD_JOURNEY_BARS,1,1);cmd(MD_JOURNEY_BIOME,1,171);auto home=*md_project_state();cmd(MD_PLAY,0,1);run(MD_RATE*14);assert(md_journey_active());assert(!memcmp(md_state()->links,home.links,sizeof home.links));assert(!memcmp(&md_state()->drift,&home.drift,sizeof home.drift));assert(!memcmp(md_project_state(),&home,sizeof home));cmd(MD_DRIFT_POSITION,0,850);cmd(MD_DRIFT_DEPTH,0,300);assert(!memcmp(md_project_state(),&home,sizeof home));cmd(MD_PLAY,0,0);assert(!memcmp(md_project_state(),&home,sizeof home));assert(!md_status()->nonfinite&&!md_status()->dropped_commands);
 puts("PASS Journey carries home Links/Drift through factory sections, retains canonical export, permits temporary Drift performance and restores the home project on Stop.");

}
