/* SPDX-License-Identifier: MIT OR GPL-3.0-only
 * Saved banks and volatile live patches are deliberately separate operations. */
static uint32_t dream_crc(const void *data,unsigned n){
 const uint8_t *p=data;uint32_t c=0xffffffffu;
 while(n--){c^=*p++;for(int k=0;k<8;k++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}
 return ~c;
}
static int dream_name_valid(const char *s){
 int ended=0;for(int i=0;i<24;i++){unsigned c=(uint8_t)s[i];if(!c)ended=1;else if(ended||c<32||c>126)return 0;}return ended;
}
int app_backup_playing(void){return md_playing()||md_clock_source();}
void app_backup_flush(void){} /* Export saved objects; never overwrite a live edit implicitly. */
void app_backup_hold(int on){dream_store_hold=on;bank_armed=-1;}
static int dream_install_mode(const void *data,unsigned n,int edit){
 if(dream_store_hold||md_playing()||md_clock_source()||md_command_space()!=64)return 0;
 uint32_t f=plat_lock();dream_reloading=1;plat_unlock(f);
 /* Keep the audio callback punctual while reinitialising voices and effect memory. */
 int ok=edit?md_import_edit(data,n):md_import(data,n);
 f=plat_lock();dream_reloading=0;if(ok){bank_pending=-1;bank_armed=-1;}plat_unlock(f);
 return ok;
}
static int dream_install(const void *data,unsigned n){return dream_install_mode(data,n,0);}
static int dream_install_edit(const void *data,unsigned n){return dream_install_mode(data,n,1);}
int app_backup_validate(unsigned obj,const void *data,unsigned n){
 if(obj==OBJ_PANEL||obj==OBJ_BLE||obj>=OBJ_NOBJ)return 0;
 if(!n)return 1; /* An empty record intentionally clears this saved object. */
 if(obj==OBJ_PREFS){
  md_preferences p;return dream_prefs_decode(&p,data,n);
 }
 if(obj==OBJ_PATCH){
  return md_decode(&bank_patch_io,data,n);
 }
 if(obj>=OBJ_USER&&obj<OBJ_TONES){
  if(n<28||n>sizeof(UserBiome))return 0;memcpy(&bank_io.biome,data,n);
  if(bank_io.biome.magic!=0x4d445542||!dream_name_valid(bank_io.biome.name))return 0;
  return md_decode(&bank_patch_io,&bank_io.biome.patch,n-28);
 }
 if(obj>=OBJ_TONES&&obj<OBJ_BLE){
  if(n!=sizeof(ToneBank))return 0;memcpy(&bank_io.tones,data,n);
  if(bank_io.tones.magic!=0x4d445442||(bank_io.tones.used&0xffff0000u))return 0;
  uint32_t f=plat_lock();bank_patch_io=*md_state();plat_unlock(f);
  for(int i=0;i<16;i++)if(bank_io.tones.used&(1u<<i)){
   if(!dream_name_valid(bank_io.tones.tone[i].name))return 0;
   bank_patch_io.track[0]=bank_io.tones.tone[i].track;bank_patch_io.ext[0]=bank_io.tones.tone[i].ext;
   if(!md_validate(&bank_patch_io))return 0;
  }
  return 1;
 }
 return 0;
}
void app_backup_restored(void){
 uint32_t f=plat_lock();dream_reloading=1;plat_unlock(f);
 app_init();plat_ble_enable(md_ble_at_boot());intro_pending=0;dream_dismiss_intro();bank_armed=-1;dream_store_hold=0;
 bank_kind=bank_slot=bank_char=0;memset(bank_name,0,sizeof bank_name);memcpy(bank_name,"MY BIOME",8);
 dream_ui.btn=plat_buttons();dream_ui.keys=plat_keys();dream_ui.held=dream_ui.btn;
 f=plat_lock();dream_reloading=0;plat_unlock(f);
}
