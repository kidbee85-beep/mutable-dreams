// SPDX-License-Identifier: MIT OR GPL-3.0-only
// Firmware commands only. All sound and all pixels come from the C/C++ firmware.
globalThis.fm1App={message(f,m){const e=f.ex;if(m.cmd==='patch-export'||m.cmd==='patch-import'){if(!f.patchRequests)f.patchRequests=[];if(f.patchRequests.length<8)f.patchRequests.push(m);else f.port.postMessage({type:'patch-result',id:m.id,error:'Too many pending requests.'});return;}if(m.cmd==='bank-action'){
 if(m.action===1||m.action===2){
  const name=String(m.name).replace(/[^ -~]/g,'').trim().slice(0,23);
  for(let i=0;i<23;i++){e.dream_command(115,0,i);e.dream_command(116,0,name.charCodeAt(i)||0);}
 }
 const status=e.dream_bank_browser(m.action);
 const name=String.fromCharCode(...new Uint8Array(f.mem.buffer,e.dream_bank_name(),24)).split('\0')[0];
 f.port.postMessage({type:'bank-result',action:m.action,status,name});return;
}if(m.cmd==='midi')e.web_midi_in(0,m.packet>>>0);if(m.cmd==='command')e.dream_command(m.c,m.t,m.v);if(m.cmd==='page')e.dream_page(m.v);if(m.cmd==='track')e.dream_track(m.v);},afterRender(f){
 const m=f.patchRequests?.shift();if(!m)return;const e=f.ex,n=e.dream_patch_size();
 try{
  if(m.cmd==='patch-export'){
   const bytes=Array.from(new Uint8Array(f.mem.buffer,e.dream_patch_data(),n));
   f.port.postMessage({type:'patch-result',id:m.id,bytes});
  }else{
   if(e.dream_value(16,0))throw Error('Stop playback before importing a patch.');
   if(!Array.isArray(m.bytes)||![652,1292,2100,2220,2288,2356,n].includes(m.bytes.length)||m.bytes.some(v=>!Number.isInteger(v)||v<0||v>255))throw Error('This patch file is not valid.');
   new Uint8Array(f.mem.buffer,e.dream_patch_buffer(),n).set(m.bytes);
   if(!e.dream_patch_apply(m.bytes.length))throw Error('This patch contains invalid or unsupported values.');
   f.port.postMessage({type:'patch-result',id:m.id,loaded:true});
  }
 }catch(err){f.port.postMessage({type:'patch-result',id:m.id,error:err.message});}
},process(f){if(!f.dreamFrames)f.dreamFrames=0;if(++f.dreamFrames%16===0){let e=f.ex;f.port.postMessage({type:'state',learnSlot:e.dream_value(206,0),linkSlot:e.dream_value(207,0),learnArmed:e.dream_value(202,0),learnPicked:e.dream_value(203,e.dream_value(206,0)),driftPosition:e.dream_value(204,0),fill:e.dream_value(208,0),displayInverted:!!e.dream_value(209,0),pending:e.dream_value(117,0),kitMode:e.dream_value(118,0),bankPending:e.dream_value(119,0),buttonGlow:e.dream_value(124,0),bankName:String.fromCharCode(...new Uint8Array(f.mem.buffer,e.dream_bank_name(),24)).split('\0')[0],octave:e.dream_value(107,0),help:e.dream_value(108,0),lastNote:e.dream_value(109,e.dream_value(101,0)),events:e.dream_value(110,e.dream_value(101,0)),motion:e.dream_value(103,0),biome:e.dream_value(20,0),edited:e.dream_value(102,0),tones:Array.from({length:6},(_,i)=>e.dream_value(45,i)),page:e.dream_value(100,0),track:e.dream_value(101,0),playing:e.dream_value(16,0),knobs:Array.from({length:4},(_,k)=>e.dream_knob(e.dream_value(100,0),e.dream_value(101,0),k)),tempo:e.dream_value(15,0),locked:e.dream_value(17,0),freeze:e.dream_value(21,0),raw:Array.from({length:4},(_,k)=>e.dream_raw_knob(e.dream_value(100,0),e.dream_value(101,0),k)),muted:Array.from({length:6},(_,i)=>e.dream_value(12,i)),models:Array.from({length:6},(_,i)=>e.dream_value(0,i)),macro:Array.from({length:4},(_,i)=>e.dream_value(14,i))});}}};
