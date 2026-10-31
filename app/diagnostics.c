/* SPDX-License-Identifier: MIT
 * USB-only bench protocol: F0 7D 4D 44 cmd seq payload F7.
 * Replies: same prefix, status, group-mask packed bytes. No flash writes.
 * cmd 1 telemetry; 2 up to 16 queued commands (c,t,value biased by 8192, two 7-bit bytes);
 * cmd 3 reset observed CPU peak; cmd 4 read patch chunk (offset in 2 x 7 bits).
 * cmd 6 snapshots the saved project (stopped only), returns size/magic/version;
 * optional one-byte object ID selects a named user Biome or tone bank for backup.
 * cmd 5 reads chunks of that immutable snapshot. This can preserve a foreign saved object.
 * ACK for cmd 2 means queued, not applied: verify with subsequent snapshot.
 */
static uint8_t md_stored_snapshot[3840];
static uint32_t md_stored_size;
static uint8_t md_stored_generation;
#include "editor.c"
int app_sysex(const uint8_t *p,uint32_t n){
 if(n<3||p[0]!=0x7d||p[1]!=0x4d||p[2]!=0x44)return 0;
 if(n<5)return 1;
 uint8_t tx[280]={0xf0,0x7d,0x4d,0x44,p[3],p[4],0},raw[224];uint32_t len=0,at=7;
 if(p[3]==1&&n==5){
  uint32_t words[49]={0},f=plat_lock();const md_stats *s=md_status();const md_patch *v=md_state();
  words[0]=2;words[1]=plat_ms();words[2]=plat_cpu_pct();words[3]=cpu_peak;words[4]=plat_xruns();words[5]=overloads;
  words[6]=s->frames;words[7]=s->nonfinite;words[8]=s->dropped_commands;words[9]=v->quality;words[10]=md_playing();
  for(int i=0;i<6;i++){words[11+i]=s->events[i];words[17+i]=v->track[i].model;words[23+i]=v->track[i].filter;}
  words[29]=md_memory_bytes();words[30]=v->version;words[31]=s->grain_writes;words[32]=v->freeze;
  memcpy(&words[33],&s->peak,4);words[34]=s->voice_quanta;words[35]=s->skipped_quanta;words[36]=v->tempo;words[37]=plat_audio_peak_us();words[38]=APP_BLOCK*1000000u/MD_RATE;words[39]=plat_master();words[40]=s->quality_reductions;
  words[41]=dream_prefs.motion;words[42]=dream_prefs.boot_sound;words[43]=dream_ui.page;words[44]=(uint32_t)dream_ui.help;words[45]=intro_active&&(uint32_t)(plat_ms()-intro_start)<3200;
  words[46]=s->element_modes;words[47]=ui_frames;words[48]=ui_frame_peak_ms;plat_unlock(f);len=49*4;memcpy(raw,words,len);
 }else if(p[3]==2&&n>=9&&n<=69&&(n-5)%4==0){
  uint32_t count=(n-5)/4,f=plat_lock();
  for(unsigned k=5;k<n;k+=4)if(p[k]>=MD_COMMAND_COUNT||(p[k]>=99&&p[k]<120))tx[6]=1;
  if(md_command_space()<count)tx[6]=2;
  if(!tx[6])for(unsigned k=5;k<n;k+=4){int c=p[k],t=p[k+1],v=(int)(p[k+2]|p[k+3]<<7)-8192;
   if(c==MD_MIDI_ROUTE)v=clamp(v,0,16);
   if(c==MD_MIDI_MACRO)v=clamp(v,0,1);
   md_command(c,t,v);
   if(c==MD_MIDI_ROUTE&&t>=0&&t<6)dream_prefs.midi_route[t]=v;
   if(c==MD_MIDI_MACRO)dream_prefs.midi_macro=v;
  }
  plat_unlock(f);
 }
 else if(p[3]==3&&n==5){uint32_t f=plat_lock();cpu_peak=ui_frame_peak_ms=0;plat_audio_peak_reset();plat_unlock(f);}
 else if(p[3]==4&&n==7){unsigned off=p[5]|p[6]<<7;if(off>=sizeof(md_patch))tx[6]=1;else{len=sizeof(md_patch)-off;if(len>224)len=224;uint32_t f=plat_lock();memcpy(raw,((const uint8_t*)md_state())+off,len);plat_unlock(f);}}
 else if(p[3]==6&&(n==5||n==6)){
  md_stored_size=0;md_stored_generation=(md_stored_generation+1)&127;
  unsigned obj=n==6?p[5]:OBJ_PATCH;
  if(obj!=OBJ_PATCH&&(obj<OBJ_USER||obj>=OBJ_NOBJ))tx[6]=1;
  else if(md_playing()||md_clock_source())tx[6]=4;else{int size=plat_store_size(obj);if(size<12||size>(int)sizeof md_stored_snapshot)tx[6]=3;
   else if(plat_store_load(obj,md_stored_snapshot,(uint32_t)size)!=size)tx[6]=3;
   else{md_stored_size=(uint32_t)size;memcpy(raw,&md_stored_size,4);memcpy(raw+4,md_stored_snapshot,8);len=12;}}
 }
 else if(p[3]==5&&(n==7||n==8)){unsigned off=p[5]|p[6]<<7;if((n==8&&p[7]!=md_stored_generation)||off>=md_stored_size)tx[6]=3;else{len=md_stored_size-off;if(len>224)len=224;memcpy(raw,md_stored_snapshot+off,len);}}
 else if(p[3]>=7&&p[3]<=15)tx[6]=editor_reply(p,n,raw,&len);
 else tx[6]=1;
 for(unsigned i=0;i<len;){unsigned k=len-i;if(k>7)k=7;uint8_t mask=0;for(unsigned j=0;j<k;j++)mask|=(raw[i+j]>>7)<<j;tx[at++]=mask;for(unsigned j=0;j<k;j++)tx[at++]=raw[i+j]&127;i+=k;}
 tx[at++]=0xf7;plat_sysex_send(tx,at);return 1;
}
