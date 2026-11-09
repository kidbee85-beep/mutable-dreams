// SPDX-License-Identifier: MIT OR GPL-3.0-only
let hardwareDevice=null,hardwareBusy=false,hardwarePoll=null,hardwareAccess=null,browserWasReady=false;
let bankInfoKey='';
let hardwareParams=new Map(),hardwareFlush=null,hardwareFlushTask=Promise.resolve();
const deviceMessage=text=>{$('device-status').textContent=text;};
const deviceError=e=>deviceMessage(e.message||String(e));
function deviceDraw(m){
 cx.fillStyle='#0b1319';cx.fillRect(0,0,240,240);
 cx.strokeStyle='#304d47';cx.lineWidth=1;
 for(let j=0;j<7;j++){cx.beginPath();for(let x=0;x<240;x+=3){const y=45+j*24+8*Math.sin(x*.022+j+m.page*.4);x?cx.lineTo(x,y):cx.moveTo(x,y);}cx.stroke();}
 cx.fillStyle='#e9d9c6';cx.font='bold 12px sans-serif';cx.fillText('MUTABLE DREAMS · FM-1',12,22);
 cx.fillStyle='#9db5a8';cx.font='10px sans-serif';cx.fillText('LIVE PARAMETER VIEW',12,37);
 cx.fillStyle='#f7eee4';cx.font='bold 13px sans-serif';cx.fillText(m.help>=0?'HOLD HELP ON THE DEVICE':$('pages')?.querySelector('.active')?.textContent?.trim().slice(0,26)||'LIVE BIOME',12,58);
 for(let i=0;i<(m.kitMode?4:6);i++){cx.fillStyle=colors[i];cx.beginPath();cx.arc(24+i*38,86,6+(i===m.track?4:0),0,Math.PI*2);cx.fill();cx.fillStyle=m.muted[i]?'#8d9697':'#ede5db';cx.font='9px sans-serif';cx.fillText(String(i+1),22+i*38,105);}
 defs[m.page].forEach(([label],i)=>{const x=12+(i%2)*118,y=132+Math.floor(i/2)*48;cx.fillStyle='#14262b';cx.fillRect(x-3,y-13,110,38);cx.fillStyle=colors[i];cx.font='9px sans-serif';cx.fillText(label.slice(0,18),x,y);cx.fillStyle='#fff3e4';cx.font='bold 14px sans-serif';cx.fillText(String(knobText(m.page,i,m.knobs[i])).slice(0,14),x,y+18);});
 cx.fillStyle='#b8c6bc';cx.font='10px sans-serif';cx.fillText(`${m.playing?'PLAYING':'STOPPED'}  ·  ${m.tempo} BPM  ·  TRACK ${m.track+1}`,12,225);
}
function flushHardwareParams(){
 clearTimeout(hardwareFlush);hardwareFlush=null;if(!hardwareDevice||!hardwareParams.size)return hardwareFlushTask;
 const d=hardwareDevice,list=[...hardwareParams.values()];hardwareParams.clear();
 hardwareFlushTask=hardwareFlushTask.catch(()=>{}).then(async()=>{for(const [c,t,v] of list)await d.command(c,t,v);});
 return hardwareFlushTask;
}
function hardwarePost(m){
 if(hardwareBusy){deviceMessage('A bank transfer is running. Wait for it to finish.');return;}
 const d=hardwareDevice;if(!d)return;
 const run=async()=>{
  if(m.type==='app'&&m.cmd==='command'){
   if(m.c===113||m.c===114){await hardwareBank(m.c===114);return;}
   // Continuous knobs are coalesced. Ordered sequence edits and actions are never coalesced.
   if([1,2,3,4,5,9,11,14,15,22,23,24,28,29,37,38,39,40,41,42,44,46,47,48,49,50,51,54,55,56,57,58,94,122,123].includes(m.c)){
    hardwareParams.set(m.c+':'+m.t,[m.c,m.t,m.v]);if(!hardwareFlush)hardwareFlush=setTimeout(()=>flushHardwareParams().catch(deviceError),45);return;
   }
   await flushHardwareParams();if(m.c>=100&&m.c<120)await d.ui(m.c,m.t,m.v);else await d.command(m.c,m.t,m.v);
  }else{
   await flushHardwareParams();
   if(m.type==='app'&&m.cmd==='page')await d.ui(100,0,m.v);
   else if(m.type==='app'&&m.cmd==='track')await d.ui(101,0,m.v);
   else if(m.type==='app'&&m.cmd==='bank-name'){/* Name sent atomically with an explicit save. */}
   else if(m.type==='buttons')await d.remote([1,m.mask&127,m.mask>>7&127]);
   else if(m.type==='keys')await d.remote([2,m.mask&127,m.mask>>7&127,m.mask>>14&127,m.mask>>21&127]);
   else if(m.type==='enc')await d.remote([3,m.role,Math.max(0,Math.min(127,m.n+64))]);
   else if(m.type==='master')await d.remote([4,m.value&127,m.value>>7&127]);
   else if(m.type==='app'&&m.cmd==='midi'){const st=m.packet>>>8&255,a=m.packet>>>16&127,b=m.packet>>>24&127;d.link.output.send(st>=248?[st]:[st,a,...([0xc0,0xd0].includes(st&240)?[]:[b])]);}
  }
 };
 run().catch(deviceError);
}
async function hardwareBank(save){
 const d=hardwareDevice;if(!d)return;await flushHardwareParams();
 const state=await d.state();if(state.playing||state.clock)throw Error('Stop the FM-1 and choose internal clock before using User Banks.');
 const k=state.bankKind,s=state.bankSlot,t=state.track,name=$('bank-name').value;
 try{await d.bank(k,s,t,save?2:1,name);}
 catch(e){if(!save||e.status!==5)throw e;
  if(!confirm(`Replace FM-1 ${k?'sound':'Biome'} slot ${s+1} with “${name}”?`))return;
  await d.bank(k,s,t,3,name);
 }
 bankInfoKey='';if(!save)$('bank-name').value=(await d.state()).bankName;
 deviceMessage(save?`Saved to FM-1 ${k?'sound':'Biome'} slot ${s+1}.`:`Loaded FM-1 user slot ${s+1}.`);
}
async function pollHardware(){
 const d=hardwareDevice;if(!d||hardwareBusy||d.pending||hardwareParams.size)return;
 try{await d.remote([5,1]);const state=await d.state();if(hardwareDevice!==d)return;receiveEngine(state);deviceDraw(state);
  const key=state.bankKind+':'+state.bankSlot+':'+state.track;
  if(state.page===18&&!state.playing&&!state.clock&&key!==bankInfoKey){
   bankInfoKey=key;
   try{const name=await d.bank(state.bankKind,state.bankSlot,state.track,0);deviceMessage(`FM-1 ${state.bankKind?'sound':'Biome'} slot ${state.bankSlot+1}: ${String.fromCharCode(...name).split('\0')[0]}. Load recalls it; Save replaces it only after confirmation.`);}
   catch(e){if(e.status===3)deviceMessage(`FM-1 slot ${state.bankSlot+1} is empty. Name your work and Save to keep it here.`);else throw e;}
  }}
 catch(e){deviceError(e);if(d.link.lost)await disconnectHardware();}
}
function downloadDeviceFile(bytes,name,type){const url=URL.createObjectURL(new Blob([bytes],{type})),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),3000);}
async function disconnectHardware(){
 const d=hardwareDevice;if(!d||hardwareBusy)return;clearInterval(hardwarePoll);clearTimeout(hardwareFlush);hardwareParams.clear();
 try{await d.close();}catch(e){deviceError(e);}hardwareDevice=null;ready=browserWasReady;playing=0;buttons=keys=0;
 $('device-connect').disabled=false;$('device-disconnect').disabled=true;$('device-backup').disabled=$('device-restore').disabled=true;
 $('power').disabled=browserWasReady;$('play').disabled=!ready;$('export-patch').disabled=$('import-patch').disabled=!ready;
 $('load-dev-example').disabled=!ready;$('midi-connect').disabled=false;$('midi-input').disabled=false;$('device-target').textContent='BROWSER';
 if(ctx&&browserWasReady)await ctx.resume();deviceMessage('Browser instrument selected. Hardware banks remain on the FM-1.');if(page===30)macroUI();
}
$('device-connect').onclick=async()=>{
 if(hardwareDevice||hardwareBusy)return;
 try{
  if(!navigator.requestMIDIAccess)throw Error('Live hardware editing needs Web MIDI with SysEx, available in Chrome or Edge on a secure page. The browser instrument works independently.');
  $('device-connect').disabled=true;
  if(midiSelected){midiSelected.onmidimessage=null;midiSelected=null;}$('midi-input').value='';
  hardwareAccess=await navigator.requestMIDIAccess({sysex:true});
  const d=await DreamsDevice.Device.connect(hardwareAccess);browserWasReady=ready;
  if(node){node.port.postMessage({type:'buttons',mask:0});node.port.postMessage({type:'keys',mask:0});node.port.postMessage({type:'app',cmd:'command',c:27,t:0,v:0});await new Promise(r=>setTimeout(r,70));}
  if(ctx)await ctx.suspend();hardwareDevice=d;ready=true;buttons=keys=0;
  $('power').disabled=true;$('device-disconnect').disabled=false;$('device-backup').disabled=$('device-restore').disabled=false;
  $('play').disabled=$('export-patch').disabled=$('import-patch').disabled=false;$('load-dev-example').disabled=false;
  $('midi-connect').disabled=$('midi-input').disabled=true;$('device-target').textContent='LIVE FM-1';
  deviceMessage('Connected to '+d.id+'. Listen through the FM-1. Controls edit its live patch; Save is explicit.');if(page===30)macroUI();
  await pollHardware();$('bank-name').value=values.bankName||'MY BIOME';hardwarePoll=setInterval(pollHardware,450);
 }catch(e){$('device-connect').disabled=false;deviceError(e);}
};
$('device-disconnect').onclick=disconnectHardware;
async function deviceTransfer(fn){
 if(!hardwareDevice||hardwareBusy)return;
 const d=hardwareDevice;
 hardwareBusy=true;$('device-backup').disabled=$('device-restore').disabled=$('device-disconnect').disabled=true;
 try{
  await flushHardwareParams();
  await d.remote([1,0,0]);await d.remote([2,0,0,0,0]);await d.remote([4,1,32]);await d.remote([5,0]);
  await fn(d);
 }catch(e){deviceError(e);}finally{hardwareBusy=false;$('device-backup').disabled=$('device-restore').disabled=$('device-disconnect').disabled=false;await pollHardware();}
}
$('device-backup').onclick=()=>deviceTransfer(async d=>{
 const {image}=await d.backup(n=>deviceMessage(`Receiving saved banks: ${n.toLocaleString('en')} bytes…`));
 downloadDeviceFile(image,'Mutable-Dreams-'+new Date().toISOString().slice(0,10)+'.mddb','application/octet-stream');
 deviceMessage('Saved project, preferences, 16 Biomes and 64 sounds backed up. Export the live patch separately if it has unsaved changes.');
});
$('device-restore').onclick=()=>{if(!hardwareBusy)$('device-bank-file').click();};
$('device-bank-file').onchange=async e=>{
 const file=e.target.files[0];e.target.value='';if(!file)return;
 try{
  if(file.size>85000)throw Error('This backup is too large.');
  const image=new Uint8Array(await file.arrayBuffer()),checked=DreamsBackup.PB.parse(image);
  if(!confirm(`Restore “${file.name}”? This replaces the saved project, preferences and every User Bank on the FM-1 (${DreamsBackup.PB.summary(checked.objs)}). Back up the device first. Physical SEL confirmation is required.`))return;
  await deviceTransfer(async d=>{const s=await d.state();if(s.playing||s.clock)throw Error('Stop the FM-1 and select internal clock before restoring.');
   await d.restore(image,(what,pct)=>deviceMessage(what==='ask'?'File checked. Press SEL on the FM-1 to restore, or HOME to cancel.':`Checking and staging backup: ${Math.round(pct)}%`));
   deviceMessage('Restore completed. Saved project and banks reloaded.');
  });
 }catch(err){deviceError(err);}
};
window.addEventListener('pagehide',()=>{const d=hardwareDevice;if(d&&!hardwareBusy){d.link.send([0xf0,0x7d,0x4c,0x43,1,0,0,0xf7]);d.link.send([0xf0,0x7d,0x4c,0x43,2,0,0,0,0,0xf7]);d.link.send([0xf0,0x7d,0x4c,0x43,4,1,32,0xf7]);}});
