/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Additive live-editor protocol v1. No arbitrary flash addresses or writes.
 * The only live-patch upload is bounded, CRC-checked, stopped and volatile. */
static unsigned editor_received,editor_length;static uint32_t editor_crc,editor_started;
static uint8_t editor_token;
static void editor_expire(uint32_t now){if(editor_length&&(uint32_t)(now-editor_started)>5000)editor_length=editor_received=0;}
static unsigned editor_unpack(const uint8_t *p,unsigned n,uint8_t *out,unsigned cap){
 unsigned at=0;while(n){unsigned mask=*p++;n--;unsigned k=n>7?7:n;if(!k||at+k>cap)return 0;
 for(unsigned j=0;j<k;j++)out[at++]=(*p++)|(((mask>>j)&1u)<<7);n-=k;}return at;
}
static int editor_reply(const uint8_t *p,unsigned n,uint8_t *raw,uint32_t *len){
 unsigned c=p[3];
 if(c==7&&n==5){uint32_t w[]={1,9,sizeof(md_patch),29,16,64,MD_TONE_PRESETS,MD_BIOMES};memcpy(raw,w,sizeof w);*len=sizeof w;return 0;}
 if(c==8&&n==5){
  uint32_t f=plat_lock();memcpy(md_stored_snapshot,md_state(),sizeof(md_patch));plat_unlock(f);
  md_stored_size=sizeof(md_patch);md_stored_generation=(md_stored_generation+1)&127;
  uint32_t w[]={md_stored_size,dream_crc(md_stored_snapshot,md_stored_size),md_stored_generation};
  memcpy(raw,w,sizeof w);*len=sizeof w;return 0;
 }
 if(c==9&&n==5){
  int32_t w[50]={0};unsigned i=0;uint32_t f=plat_lock();const md_patch *s=md_state();int t=dream_ui.track,pg=dream_ui.page;
  w[i++]=pg;w[i++]=t;w[i++]=md_playing();w[i++]=dream_ui.octave;w[i++]=dream_ui.help;
  w[i++]=dream_prefs.motion;w[i++]=s->biome_id;w[i++]=s->edited_mask;w[i++]=s->tempo;
  w[i++]=s->locked;w[i++]=s->freeze;w[i++]=dream_prefs.boot_sound;w[i++]=bank_slot;w[i++]=bank_kind;
  w[i++]=md_clock_source();w[i++]=md_midi_quantize();
  for(int j=0;j<6;j++)w[i++]=s->track[j].model;
  for(int j=0;j<6;j++)w[i++]=s->tone_preset[j];
  for(int j=0;j<6;j++)w[i++]=s->track[j].mute;
  for(int j=0;j<4;j++)w[i++]=dream_knob(pg,t,j);
  for(int j=0;j<4;j++)w[i++]=dream_raw_knob(pg,t,j);
  for(int j=0;j<4;j++)w[i++]=s->macro[j];
  plat_unlock(f);memcpy(raw,w,i*4);memcpy(raw+i*4,bank_name,24);int32_t extra[4]={md_pending_biome(),s->kit.enabled,bank_pending,dream_prefs.button_glow};memcpy(raw+i*4+24,extra,16);*len=i*4+40;return 0;
 }
 if(c==10&&n==9){
  int cmd=p[5],t=p[6],v=(p[7]|p[8]<<7)-8192;
  if(cmd==100){dream_page(v);return 0;}if(cmd==101){dream_track(v);return 0;}
  if(cmd==103||cmd==104||cmd==105||cmd==106||cmd==108||cmd==111||cmd==112){dream_command(cmd,t,v);return 0;}
  return 1;
 }
 if(c==11){
  uint32_t h[2];if(editor_unpack(p+5,n-5,(uint8_t*)h,sizeof h)!=sizeof h||h[0]!=sizeof(md_patch))return 1;
  if(app_backup_playing()||dream_store_hold)return 4;
  editor_length=h[0];editor_crc=h[1];editor_received=0;editor_token=(editor_token+1)%127+1;editor_started=plat_ms();
  raw[0]=editor_token;*len=1;return 0;
 }
 if(c==12&&n>=9){
  if(p[5]!=editor_token||!editor_length||(uint32_t)(plat_ms()-editor_started)>5000)return 6;
  unsigned off=p[6]|p[7]<<7;if(off!=editor_received)return 1;
  unsigned got=editor_unpack(p+8,n-8,((uint8_t*)&browser_patch)+off,editor_length-off);
  if(!got)return 1;editor_received+=got;editor_started=plat_ms();return 0;
 }
 if(c==13&&n==6){
  if(p[5]!=editor_token||!editor_length||(uint32_t)(plat_ms()-editor_started)>5000||editor_received!=editor_length)return 6;
  editor_length=0;
  if(dream_crc(&browser_patch,editor_received)!=editor_crc||!md_validate(&browser_patch))return 6;
  if(!dream_install(&browser_patch,editor_received))return 4;
  dream_dismiss_intro();dream_ui.help=-1;dream_ui.dirty=1;return 0;
 }
 if(c==14&&n==6){if(p[5]!=editor_token)return 6;editor_length=editor_received=0;return 0;}
 if(c==15&&n>=9){
  int kind=p[5],slot=p[6],track=p[7],action=p[8];
  if(kind>1||slot>=(kind?64:16)||track>=6||action>3)return 1;
  if(dream_store_hold||app_backup_playing())return 4;
  if((action>=2&&n!=33)||(action<2&&n!=9))return 1;
  if(action>=2&&!dream_name_valid((const char*)p+9))return 1;
  bank_kind=kind;bank_slot=slot;dream_ui.track=track;bank_armed=-1;
  if(action==0){
   if(kind){int ok=bank_read_tones();if(ok<0)return 6;if(!ok||!(tone_bank_io.used&(1u<<(slot%16))))return 3;memcpy(raw,tone_bank_io.tone[slot%16].name,24);}
   else{if(plat_store_size(bank_object())<=0)return 3;if(!bank_read_biome(bank_object()))return 6;memcpy(raw,user_biome_io.name,24);}
   *len=24;return 0;
  }
  if(action>=2)memcpy(bank_name,p+9,24);
  return bank_action(action==1?0:action==2?1:2);
 }
 return 1;
}
