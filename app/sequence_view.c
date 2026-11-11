/* SPDX-License-Identifier: MIT OR GPL-3.0-only */
enum { SEQ_COPY, SEQ_PASTE, SEQ_TRANSPOSE, SEQ_MIRROR, SEQ_ACCENT, SEQ_ARTICULATE };
static const char *const sequence_actions[]={"COPY","PASTE","TRANSPOSE","REVERSE","ACCENT","ARTICULATE"};
static const char *const sequence_rules[]={"ALWAYS","FIRST CYCLE","EVERY 2ND","FILL ONLY"};
static int8_t sequence_view_head[MD_TRACKS];
static uint8_t sequence_view_hits,seq_scope,seq_action,seq_amount;
static int8_t seq_interval;
static uint32_t sequence_view_notice;

static void sequence_view_update(void){
 int page=dream_ui.page;
 if(page!=SEQUENCE&&page!=STEP_DETAIL&&page!=SEQ_GRID&&page!=SEQ_TOOLS)return;
 uint32_t lock=plat_lock();
 sequence_view_hits=0;
 for(unsigned i=0;i<MD_TRACKS;i++){
  int hit;
  sequence_view_head[i]=md_sequence_playhead(i,&hit);
  sequence_view_hits|=(unsigned)hit<<i;
 }
 uint32_t notice=md_sequence_notice();
 plat_unlock(lock);
 if(sequence_view_notice==notice)return;
 sequence_view_notice=notice;
 if(page!=SEQ_TOOLS)return;
 static const char *const notices[]={"","COPIED TO CLIPBOARD","PASTED / ENV UNDO",
  "TRANSPOSED / ENV UNDO","REVERSED / ENV UNDO","COPY THE SAME SCOPE FIRST",
  "NOTE LIMIT / NO CHANGE","NO NOTES IN SELECTION","ENABLE A SYNTH SEQUENCE",
  "ACCENTED / ENV UNDO","GATES SET / ENV UNDO","NO CHANGE AT THIS AMOUNT"};
 unsigned status=notice&15;
 if(status>0&&status<sizeof notices/sizeof *notices)app_say(notices[status],"");
}

static void sequence_scope_set(int value){
 seq_scope=clamp(value,0,1);
 if(!seq_scope&&seq_action>SEQ_TRANSPOSE)seq_action=SEQ_TRANSPOSE;
}
static void sequence_action_set(int value){seq_action=clamp(value,0,seq_scope?SEQ_ARTICULATE:SEQ_TRANSPOSE);}
static void sequence_apply(void){
 unsigned track=dream_ui.track;
 if(md_state()->kit.enabled&&track>=3){app_say("SELECT A SYNTH TRACK","");return;}
 if(seq_action!=SEQ_COPY&&md_record_mode()){app_say("TURN RECORDING OFF","");return;}
 if(seq_action==SEQ_TRANSPOSE&&!seq_interval){app_say("SET A TRANSPOSE INTERVAL","");return;}
 if(seq_action>=SEQ_ACCENT){
  dream_cmd(MD_SEQ_EXPRESSION,track,seq_amount+(seq_action==SEQ_ARTICULATE?MD_EXPRESSION_GATE_OFFSET:0));
  return;
 }
 static const int commands[]={MD_SEQ_COPY,MD_SEQ_PASTE,MD_SEQ_TRANSPOSE,MD_SEQ_MIRROR};
 int value=seq_action==SEQ_TRANSPOSE?seq_interval+24+64*seq_scope:seq_scope;
 dream_cmd(commands[seq_action],track,value);
}

static int sequence_panel_value(int page,int track,int knob){
 const md_patch *p=md_state();
 const md_sequence *q=&p->sequence[track];
 int kit=p->kit.enabled&&track==3;
 if(page==SEQ_TOOLS){
  int values[]={seq_scope,q->edit,seq_action>=SEQ_ACCENT?seq_amount:seq_interval,seq_action};
  return values[knob];
 }
 if(page==SEQ_RULES){
  const md_step *s=&q->steps[q->edit];
  int values[]={track,kit?0:q->edit,kit?0:md_step_rule(s),kit||!s->note?23:s->note};
  return values[knob];
 }
 int values[]={track,kit?p->kit.step:q->edit,kit?p->kit.pad:q->steps[q->edit].note,
               kit?p->kit.length:q->length};
 return values[knob];
}

static void sequence_panel_set(int page,int knob,int value){
 const md_patch *p=md_state();
 int track=dream_ui.track,kit=p->kit.enabled&&track==3;
 if(page==SEQ_TOOLS){
  if(knob==0)sequence_scope_set(value);
  else if(knob==1&&!kit)dream_cmd(MD_SEQ_STEP,track,value);
  else if(knob==2){
   if(seq_action>=SEQ_ACCENT)seq_amount=clamp(value,0,100);
   else seq_interval=clamp(value,-24,24);
  }
  else if(knob==3)sequence_action_set(value);
 }else if(knob==0)dream_ui.track=clamp(value,0,md_active_tracks()-1);
 else if(page==SEQ_RULES){
  const int commands[]={0,MD_SEQ_STEP,MD_SEQ_RULE,MD_SEQ_NOTE};
  if(!kit)dream_cmd(commands[knob],track,knob==2?clamp(value,0,3):value);
 }
 else{
  const int notes[]={0,MD_SEQ_STEP,MD_SEQ_NOTE,MD_SEQ_LENGTH};
  const int drums[]={0,MD_KIT_STEP,MD_KIT_PAD,MD_KIT_LENGTH};
  dream_cmd(kit?drums[knob]:notes[knob],track,value);
 }
 dream_ui.dirty=1;
}

static unsigned sequence_row_length(const md_patch *p,unsigned track){
 if(p->kit.enabled&&track>=4)return 0;
 if(p->kit.enabled&&track==3)return p->kit.length;
 if(p->sequence[track].enabled)return p->sequence[track].length;
 return p->ext[track].bass_mode==2?clamp(p->track[track].length,1,16):p->track[track].length;
}

static void draw_sequence_grid(uint16_t accent){
 const md_patch *p=md_state();
 const int track=dream_ui.track,kit=p->kit.enabled&&track==3;
 char value[20];
 cv_text(12,45,&FONT_T,"S STEPS   G GENERATED   K KIT",MD_DIM);
 for(unsigned row=0;row<MD_TRACKS;row++){
  const int y=61+14*row;
  if(y+11+cv_oy<=0||y+cv_oy>=(int)cv_h)continue;
  const unsigned length=sequence_row_length(p,row);
  const int selected=row==(unsigned)track,drums=p->kit.enabled&&row==3;
  const md_sequence *q=&p->sequence[row];
  const md_track *t=&p->track[row];
  const uint16_t ink=t->mute?MD_EDGE:dream_colors[row];
  if(selected){cv_rect(8,y,224,13,MD_CARD);cv_rect(8,y,2,13,accent);}
  char tag[4]={'1'+row,' ',!length?'-':drums?'K':q->enabled?'S':'G',0};
  cv_text(11,y,&FONT_T,tag,selected?MD_INK:ink);
  if(!length)continue;
  for(unsigned beat=0;beat<length;beat+=4){
   int width=clamp((int)length-(int)beat,1,4)*6-1;
   cv_rect(36+6*beat,y+1,width,9,md_tint(selected?MD_CARD:MD_BG,ink,beat%8?2:4));
  }
  uint32_t kit_steps=0;
  if(drums)for(unsigned pad=0;pad<8;pad++)kit_steps|=p->kit.gates[pad];
  for(unsigned i=0;i<MD_STEPS;i++){
   int x=36+6*i;
   if(i>=length){cv_pset(x+1,y+6,MD_EDGE);continue;}
  if(!(i%4))cv_rect(x-1,y,1,12,md_tint(MD_BG,ink,12));
   int on=drums?(kit_steps>>i)&1u:q->enabled?q->steps[i].note!=0:
    p->ext[row].bass_mode==2?p->ext[row].notes[i%16]>=0:
    md_hit(i,clamp(t->hits+(p->macro[0]-500)*t->length/1500,0,t->length),t->length,t->rotate);
   unsigned rule=!drums&&q->enabled?md_step_rule(&q->steps[i]):MD_RULE_ALWAYS;
   if(on){
    if(rule==MD_RULE_FIRST)cv_rect(x,y+4,2,4,ink);
    else if(rule==MD_RULE_SECOND){cv_rect(x,y+4,1,4,ink);cv_rect(x+3,y+4,1,4,ink);}
    else if(rule==MD_RULE_FILL){cv_rect(x+1,y+3,2,6,ink);cv_rect(x,y+5,4,2,ink);}
    else{
     cv_rect(x,y+4,4,4,ink);
     if(!drums&&q->enabled&&q->steps[i].velocity>=75)cv_pset(x+1,y+3,md_tint(ink,MD_INK,12));
    }
   }
   else cv_rect(x+1,y+6,2,1,MD_EDGE);
   if(selected&&(drums||q->enabled)&&i==(drums?p->kit.step:q->edit))cv_rect(x,y+10,4,1,accent);
   if((int)i==sequence_view_head[row]){
    uint16_t head=sequence_view_hits&(1u<<row)?MD_INK:MD_EDGE;
    cv_rect(x,y+1,4,1,head);
    if(sequence_view_hits&(1u<<row))cv_rect(x+1,y+3,2,5,head);
   }
  }
 }
 cv_text(11,146,&FONT_T,"SEL OPEN TRACK   SEQ PHRASE TOOLS",MD_DIM);
 md_value_span(8,158,"K1 TRACK",track+1,1,md_active_tracks(),accent);
 md_value_span(124,158,"K2 STEP",(kit?p->kit.step:p->sequence[track].edit)+1,1,
               kit?p->kit.length:p->sequence[track].length,accent);
 if(kit)md_named(8,194,"K3 PAD",md_pad_name(p->kit.pad),accent);
 else if(p->sequence[track].steps[p->sequence[track].edit].note){
  md_note_label(p->sequence[track].steps[p->sequence[track].edit].note,value);
  md_named(8,194,"K3 NOTE",value,accent);
 }else md_named(8,194,"K3 NOTE","REST",accent);
 md_value_span(124,194,"K4 LENGTH",kit?p->kit.length:p->sequence[track].length,1,32,accent);
}

static void draw_expression_preview(const md_sequence *q,uint16_t accent){
 if(!atlas_visible(98,37))return;
 const int mode=seq_action==SEQ_ARTICULATE?MD_EXPRESSION_GATE:MD_EXPRESSION_ACCENT;
 const unsigned first=q->edit&~15u;
 const uint16_t quiet=md_tint(MD_CARD,accent,12);
 cv_text(14,98,&FONT_T,mode==MD_EXPRESSION_GATE?"GATE LENGTH":"VELOCITY",MD_DIM);
 cv_text(122,98,&FONT_T,"BEFORE",quiet);
 cv_text(170,98,&FONT_T,"AFTER",accent);
 for(unsigned i=0;i<16&&first+i<q->length;i++){
  const md_step *step=&q->steps[first+i];
  const int x=16+13*i;
  if(!(i%4))cv_rect(x-2,108,1,23,quiet);
  if(step->note){
   const int current=mode==MD_EXPRESSION_GATE?md_step_gate(step):step->velocity;
   const int before=(current+4)/5,after=(md_phrase_value(step,first+i,mode,seq_amount)+4)/5;
   cv_rect(x,130-before,3,before,quiet);
   cv_rect(x+4,130-after,4,after,accent);
  }else cv_pset(x+3,129,MD_DIM);
  if(first+i==q->edit)cv_rect(x,132,8,1,MD_INK);
 }
}

static void draw_sequence_tools(uint16_t accent){
 const md_patch *p=md_state();
 const int track=dream_ui.track,kit=p->kit.enabled&&track==3;
 unsigned clip=md_sequence_clipboard();
 const md_sequence *q=&p->sequence[track];
 char value[18];
 cv_rect(8,46,224,102,MD_CARD);
 md_text(14,53,"TRACK",MD_DIM);md_num(track+1,value);md_text(60,53,value,accent);
 md_text(96,53,kit?"KIT / SELECT A SYNTH":md_model_name(p->track[track].model),MD_INK);
 cv_text(14,76,&FONT_B,sequence_actions[seq_action],accent);
 if(seq_action>=SEQ_ACCENT&&q->enabled&&!kit)draw_expression_preview(q,accent);
 else md_text(14,105,seq_action>=SEQ_ACCENT?"ENABLE A SYNTH SEQUENCE":
  clip==0?"CLIPBOARD EMPTY":clip==1?"STEP IN CLIPBOARD":"PHRASE IN CLIPBOARD",MD_DIM);
 const char *hint=md_journey_active()?"STOP JOURNEY TO EDIT":md_record_mode()?"TURN RECORDING OFF TO EDIT":"SEL APPLY   ENV UNDO";
 if(seq_action>=SEQ_ACCENT)cv_text(14,137,&FONT_T,hint,MD_INK);
 else md_text(14,126,hint,MD_INK);
 md_named(8,158,"K1 SCOPE",seq_scope?"PHRASE":"STEP",accent);
 md_value_span(124,158,"K2 STEP",q->edit+1,1,q->length,accent);
 if(seq_action>=SEQ_ACCENT)md_value_span(8,194,"K3 AMOUNT %",seq_amount,0,100,accent);
 else md_value_span(8,194,"K3 SHIFT st",seq_interval,-24,24,accent);
 md_named(124,194,"K4 ACTION",sequence_actions[seq_action],accent);
}

static void draw_sequence_rules(uint16_t accent){
 const md_patch *p=md_state();
 unsigned track=dream_ui.track,fill=md_fill_status();
 int kit=p->kit.enabled&&track==3;
 const md_sequence *q=&p->sequence[track];
 const md_step *s=&q->steps[q->edit];
 static const char *const fill_text[]={"FILL OFF","FILL PLAYING","FILL NEXT BAR","FILL + NEXT BAR"};
 char value[16];
 cv_rect(8,46,224,102,MD_CARD);
 cv_text(14,53,&FONT_B,fill_text[fill],fill?accent:MD_INK);
 md_text(14,84,"SEL QUEUES OR CANCELS NEXT BAR",MD_INK);
 md_text(14,108,kit?"SELECT A SYNTH TRACK":q->enabled?"RULES FOLLOW THE TRACK CYCLE":"ENABLE PATTERN ON TRACK PAGE",MD_DIM);
 md_text(14,128,"GRID: SINGLE / DOUBLE / PLUS",MD_DIM);
 md_value_span(8,158,"K1 TRACK",track+1,1,md_active_tracks(),accent);
 if(kit){
  md_named(124,158,"K2 STEP","-",accent);
  md_named(8,194,"K3 RULE","-",accent);
  md_named(124,194,"K4 NOTE","-",accent);
 }else{
  md_value_span(124,158,"K2 STEP",q->edit+1,1,q->length,accent);
  md_named(8,194,"K3 RULE",sequence_rules[md_step_rule(s)],accent);
  if(s->note)md_note_label(s->note,value);
  md_named(124,194,"K4 NOTE",s->note?value:"REST",accent);
 }
}
