/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Explicit stopped writes; existing patch/panel/preferences IDs stay unchanged.
 * User Biomes: 0x98000..0xB7FFF; packed tones: 0xB8000..0xBFFFF.
 * Each object uses the platform CRC + committed A/B sectors. */
static int bank_slot,bank_kind,bank_char,bank_armed=-1;
enum { BANK_LOAD, BANK_SAVE_ASK, BANK_SAVE_REPLACE, BANK_CLEAR_ASK, BANK_CLEAR };
enum { BANK_CLEAR_ARM=2048, BANK_CONFIRM_MS=12000 };
static char bank_name[24]="MY BIOME";
typedef struct { uint32_t magic; char name[24]; md_patch patch; } UserBiome;
static uint32_t bank_arm_start;
typedef struct { char name[24]; md_track track; md_extension ext; } UserTone;
typedef struct { uint32_t magic,used; UserTone tone[16]; } ToneBank;
typedef char tone_bank_must_fit[(sizeof(ToneBank)<=PLAT_STORE_MAX)?1:-1];
/* These payloads live within one main-loop bank or backup operation.
 * Editor uploads, decoded patches and snapshots have separate storage. */
static union {
 UserBiome biome;
 ToneBank tones;
} bank_io;
static md_patch bank_patch_io;
typedef char biome_must_fit[(sizeof(UserBiome)<=PLAT_STORE_MAX)?1:-1];
static int bank_read_biome(int obj){
 int n=plat_store_load(obj,&bank_io.biome,sizeof bank_io.biome);
 return n>=28&&bank_io.biome.magic==0x4d445542&&md_decode(&bank_patch_io,&bank_io.biome.patch,n-28);
}
static int bank_object_for(int kind,int slot){return kind?OBJ_TONES+slot/16:OBJ_USER+slot;}
static int bank_object(void){return bank_object_for(bank_kind,bank_slot);}
static int bank_read_tones(int obj){
 int n=plat_store_load(obj,&bank_io.tones,sizeof bank_io.tones);
 if(n<0){memset(&bank_io.tones,0,sizeof bank_io.tones);bank_io.tones.magic=0x4d445442;return 0;}
 return n==sizeof bank_io.tones&&bank_io.tones.magic==0x4d445442&&!(bank_io.tones.used&0xffff0000u)?1:-1;
}
/* 0 success; 3 empty; 4 busy; 5 overwrite confirmation; 6 format; 7 write. */
static int bank_action(int action){
 if(dream_store_hold){app_say("USB BACKUP IN PROGRESS","");return 4;}
 if(md_playing()||md_clock_source()){app_say("STOP / USE INTERNAL CLOCK","");return 4;}
 const int clear=action>=BANK_CLEAR_ASK;
 if(!clear&&bank_kind&&md_state()->kit.enabled&&dream_ui.track==3){app_say("SAVE KITS AS USER BIOMES","");return 6;}
 int obj=bank_object(),exists=plat_store_size(obj)>0;
 if(action==BANK_LOAD&&!bank_kind&&exists&&!bank_allow_load){
  // The audio owner may apply MIDI edits; read the fingerprint under the lock.
  uint32_t f=plat_lock();const int unsaved=md_unsaved();plat_unlock(f);
  if(unsaved){bank_pending=bank_slot;return 8;}
 }
 if(bank_kind){int r=bank_read_tones(obj);if(r<0){app_say("BANK FORMAT NOT SUPPORTED","");return 6;}exists=r&&(bank_io.tones.used&(1u<<(bank_slot%16)));}
 int arm=obj*64+bank_slot+(clear?BANK_CLEAR_ARM:0);
 if(exists&&(action==BANK_CLEAR_ASK||(action==BANK_SAVE_ASK&&
    (bank_armed!=arm||(uint32_t)(plat_ms()-bank_arm_start)>BANK_CONFIRM_MS)))){
  bank_armed=arm;bank_arm_start=plat_ms();dream_ui.dirty=1;return 5;
 }
 bank_armed=-1;
 if(clear){
  if(!exists){app_say("USER SLOT ALREADY EMPTY","");return 3;}
  if(bank_kind){
   unsigned slot=bank_slot%16;
   bank_io.tones.used&=~(1u<<slot);
   memset(&bank_io.tones.tone[slot],0,sizeof(UserTone));
  }
  unsigned bytes=bank_kind&&bank_io.tones.used?sizeof(ToneBank):0;
  int failed=plat_store_save(obj,&bank_io.tones,bytes);
  app_say(failed?"CLEAR FAILED":"USER SLOT CLEARED","");return failed?7:0;
 }
 uint32_t lock=plat_lock();bank_patch_io=*md_state();plat_unlock(lock);
 if(action!=BANK_LOAD){
  int failed;
  if(bank_kind){memcpy(bank_io.tones.tone[bank_slot%16].name,bank_name,24);bank_io.tones.tone[bank_slot%16].track=bank_patch_io.track[dream_ui.track];bank_io.tones.tone[bank_slot%16].ext=bank_patch_io.ext[dream_ui.track];bank_io.tones.used|=1u<<(bank_slot%16);failed=plat_store_save(obj,&bank_io.tones,sizeof bank_io.tones);}
  else {bank_io.biome.magic=0x4d445542;memcpy(bank_io.biome.name,bank_name,24);bank_io.biome.patch=bank_patch_io;failed=plat_store_save(obj,&bank_io.biome,sizeof bank_io.biome);}
  if(!failed&&!bank_kind){
   uint32_t f=plat_lock();md_mark_saved_from(&bank_patch_io);plat_unlock(f);
   bank_pending=-1;
   if(md_pending_biome()>=0)dream_cmd(MD_SCENE_CONFIRM,0,0);
  }
  app_say(failed?"BANK SAVE FAILED":"USER SLOT SAVED","");return failed?7:0;
 }else{
  if(!exists){app_say("EMPTY USER SLOT","");return 3;}
  if(bank_kind){UserTone *t=&bank_io.tones.tone[bank_slot%16];memcpy(bank_name,t->name,24);bank_name[23]=0;md_track *v=&bank_patch_io.track[dream_ui.track];
   /* Sound recall preserves rhythm, pitch, mix and sends. */
   v->model=t->track.model;v->tone=t->track.tone;v->harm=t->track.harm;v->decay=t->track.decay;v->attack=t->track.attack;v->shape=t->track.shape;v->cutoff=t->track.cutoff;v->resonance=t->track.resonance;v->filter=t->track.filter;v->breathe=t->track.breathe;
   md_extension *e=&bank_patch_io.ext[dream_ui.track];
   e->glide=t->ext.glide;e->tides=t->ext.tides;e->tides_rate=t->ext.tides_rate;e->warps=t->ext.warps;
   bank_patch_io.tone_preset[dream_ui.track]=-1;bank_patch_io.edited_mask|=1u|(2u<<dream_ui.track);
  }else {if(!bank_read_biome(obj)){app_say("INVALID USER SLOT","");return 6;}memcpy(bank_name,bank_io.biome.name,24);bank_name[23]=0;}
  int ok=bank_kind?dream_install_edit(&bank_patch_io,sizeof bank_patch_io):dream_install(&bank_patch_io,sizeof bank_patch_io);app_say(ok?"USER SLOT LOADED":"INVALID USER SLOT","");return ok?0:6;
 }
}
