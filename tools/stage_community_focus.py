from pathlib import Path
import difflib, json, shutil, subprocess, sys
R=Path(__file__).resolve().parents[1]
O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Collection/Community-Next').resolve()
T=R.parent/'mutable-dreams-2.0-community-focus'
assert not T.exists(),T
for name in ('app','base','tools','tests'):
    shutil.copytree(R/name,T/name,ignore=shutil.ignore_patterns('build','__pycache__','.git'))
shutil.copytree(R/'app/build/gen',T/'app/build/gen')
p=T/'app/app.c';before=p.read_text();after=before
def replace(a,b):
    global after
    assert after.count(a)==1,a
    after=after.replace(a,b)
replace('char msg[32]; uint32_t msg_start;','char msg[32]; uint32_t msg_start; uint8_t live_tracks;')
replace(' md_organic();\n cv_rect(0,0,240,39,MD_BG);', ''' if(dream_ui.page==LIVE&&dream_ui.live_tracks&&dream_ui.help<0&&
    !intro_active&&md_pending_biome()<0&&bank_pending<0){
  cv_text(12,7,&FONT_M,"TRACKS",MD_INK);
  cv_text(163,14,&FONT_T,md_playing()?"PLAYING":"STOPPED",col);
  cv_rect(9,37,222,1,col);
  for(i=0;i<md_active_tracks();i++){
   int y=44+i*28,off=p->track[i].mute;
   uint16_t ink=off?RGB(154,171,174):MD_INK;
   const char *name=p->kit.enabled&&i==3?"LO-FI KIT":md_model_name(p->track[i].model);
   if(i==dream_ui.track){cv_rect(8,y,224,26,MD_CARD);cv_rect(8,y,3,26,col);}
   md_num(i+1,s);cv_text(15,y+2,&FONT_B,s,dream_colors[i]);
   cv_text(37,y+2,text_w(&FONT_B,name)>150?&FONT_H:&FONT_B,name,ink);
   cv_text(193,y+2,&FONT_B,off?"OFF":"ON",off?ink:dream_colors[i]);
  }
  cv_text(11,219,&FONT_H,"HOME Atlas  /  GLO User Banks",MD_INK);return;
 }
 md_organic();
 cv_rect(0,0,240,39,MD_BG);''')
replace('  md_text(36,199,"212 BIOMES / 452 TONES",MD_DIM);', '''  md_num(MD_BIOMES-1,s);char *caption=s+strlen(s);
  memcpy(caption," BIOMES / ",10);md_num(MD_TONE_PRESETS,caption+10);
  memcpy(caption+10+strlen(caption+10)," TONES",7);
  md_text((240-text_w(&FONT_H,s))/2,199,s,MD_DIM);''')
replace('  for(i=0;i<5;i++){\n   const felucca_font_t *font=', '''  static const char *trackshelp[]={"HOME: Atlas / Large Tracks","SELECT knob: choose track","SEL: mute or unmute","GLO from Tracks: User Banks","Hold any function for help"};
  if(dream_ui.page==LIVE&&(dream_ui.help==B_HOME||(dream_ui.live_tracks&&dream_ui.help==B_GLO)))lines=trackshelp;
  for(i=0;i<5;i++){
   const felucca_font_t *font=''')
replace(' case B_HOME:dream_ui.page=LIVE;break;', ''' case B_HOME:
  dream_ui.live_tracks=dream_ui.page==LIVE&&!dream_ui.live_tracks;
  dream_ui.page=LIVE;app_say(dream_ui.live_tracks?"LARGE TRACKS":"LIVE ATLAS","");break;''')
replace(' case B_GLO:dream_ui.page=dream_ui.page==HARMONY?', ''' case B_GLO:
  if(dream_ui.page==LIVE&&dream_ui.live_tracks){
   bank_kind=0;bank_slot=clamp(bank_slot,0,15);bank_armed=-1;dream_ui.page=BANKS;break;
  }
  dream_ui.page=dream_ui.page==HARMONY?''')
replace('int app_expect(const char *what,int *got) {', '''int app_expect(const char *what,int *got) {
 if(!strcmp(what,"large_tracks")){*got=dream_ui.live_tracks;return 1;}
 if(!strcmp(what,"selected_muted")){*got=md_state()->track[dream_ui.track].mute;return 1;}''')
p.write_text(after)
diff=''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),'a/app/app.c','b/app/app.c'))
(O/'0002-large-tracks-and-bank-shortcut.patch').write_text(diff)
s='''wait 100
shot Startup-Counter.png
wait 3400
page 1
tap HOME
expect large_tracks 0
scene 219
wait 150
shot Botanical-Home.png
tap HOME
expect large_tracks 1
expect page 0
wait 150
shot Large-Tracks.png
tap SEL
wait 150
expect selected_muted 1
shot Large-Tracks-Mute.png
press HOME
wait 900
expect help 6
release HOME
wait 120
expect large_tracks 1
expect page 0
shot Large-Tracks-Help.png
tap HOME
expect help -1
expect large_tracks 1
press GLO
wait 900
expect help 5
release GLO
wait 120
expect page 0
tap HOME
expect help -1
tap GLO
expect page 18
shot User-Banks-Shortcut.png
tap HOME
expect page 0
expect large_tracks 0
expect patch_valid 1
expect invalid 0
expect queue_drops 0
'''
(O/'Focus-Checks.txt').write_text(s)
subprocess.run([sys.executable,str(T/'tools/build_local.py'),'host','--sanitize','--no-gen'],check=True)
dest=O/'Focus-Screens';dest.mkdir(exist_ok=True)
subprocess.run([str(T/'app/build/san/mutable-dreams-host'),str(O/'Focus-Checks.txt'),str(dest)],check=True)
(O/'Focus-Status.json').write_text(json.dumps(dict(host_sanitizer_pass=True,target_link_not_measured=True,device_installed=False,device_build_unchanged=True,save_schema_unchanged=True),indent=2)+'\n')
print('PASS staged Large Tracks, User Banks shortcut, contextual holds and startup count. No device flash.')
