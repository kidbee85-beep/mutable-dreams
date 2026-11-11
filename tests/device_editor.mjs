import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';import path from 'node:path';
const root=path.resolve(path.dirname(new URL(import.meta.url).pathname),'..');
vm.runInThisContext(fs.readFileSync(root+'/web/device-backup.js','utf8'));
vm.runInThisContext(fs.readFileSync(root+'/web/device-editor.js','utf8'));
const {PB,Link}=DreamsBackup,{Device,pack,unpack}=DreamsDevice;
for(let n=1;n<224;n++){const b=Array.from({length:n},(_,i)=>(i*157+n)%256);assert.deepEqual([...unpack(pack(b))],b);}
const image=fs.readFileSync(root+'/app/build/san/dreams-map-test.mddb');assert.equal(PB.parse(image).objs.length,22);
const bad=Buffer.from(image);bad[35]^=1;assert.throws(()=>PB.parse(bad));assert.throws(()=>PB.parse(image.slice(0,-1)));
const wrong=Buffer.from(image);wrong.writeUInt16LE(1,16);wrong.writeUInt32LE(PB.crc32(wrong,0,wrong.length-4),wrong.length-4);assert.throws(()=>PB.parse(wrong));
const le=n=>[n&255,n>>>8&255,n>>>16&255,n>>>24&255];
const patch=Array.from({length:2604},(_,i)=>(i*17)&255);
const input={},filter=new Link(input,{});
const mirrorPackets=[0x50,0x51,0x52,0x53,0x54].map(c=>PB.frame(c,[0,1,2,3]));
for(let i=0;i<4000;i++)input.onmidimessage({data:Uint8Array.from(mirrorPackets[i%5])});
assert.equal(filter.q.length,0);
const otherPackets=[...Array.from({length:128},(_,i)=>PB.frame(i,[0,1,2,3])).filter((_,i)=>i<0x50||i>0x54),
 [0xf0,0x7d,0x4d,0x44,12,1,0,0xf7],[0xf0,0x7d,0x4c,0x43,5,0xf7]];
for(const p of otherPackets)input.onmidimessage({data:Uint8Array.from(p)});
assert.deepEqual(filter.q,otherPackets);filter.close();
console.log('PASS live editor ignores 4,000 mirror packets and retains editor, panel, updater and backup replies.');
const fullIdentity=Array(27).fill(0),textId='FM-1_710020000';
for(let i=0;i<textId.length;i++)fullIdentity[i]=textId.charCodeAt(i);
fullIdentity.splice(24,3,0xc2,0x28,0x14);
assert.equal(PB.parseIdentity(PB.frame(0x11,fullIdentity)),textId);
let queryBody=null;
const fakePort={name:'Mutable Dreams FM-1',state:'connected',open:async()=>{}};
const found=await PB.find({inputs:new Map([[1,fakePort]]),outputs:new Map([[1,fakePort]])},()=>({
 drain(){},send(packet){queryBody=PB.parseFrame(packet).body;return true;},async read(){return PB.frame(0x11,fullIdentity);},
}));
assert.deepEqual(queryBody,[1]);assert.equal(found.id,textId);
console.log('PASS requests the full USB identity and ignores the appended flash chip ID.');
class Mock{
 constructor(){this.q=[];this.log=[];this.lost=false;this.data=[];this.token=7;this.parallel=0;this.max=0;}
 send(p){this.log.push(p);this.parallel++;this.max=Math.max(this.max,this.parallel);const c=p[4];let status=0,data=[];
  if(p[2]===0x4c){this.q.push([0xf0,0x7d,0x4c,0x43,c,0xf7]);return true;}
  if(c===7)data=(this.capability||[1,11,2604,40,16,64,420,197]).flatMap(le);
  if(c===21){if(this.features===undefined)status=1;else data=le(this.features);}
  if(c===8)data=[2604,PB.crc32(patch),4].flatMap(le);
  if(c===5){assert.equal(p[8],4);const off=p[6]|p[7]<<7;if(this.dropRead===off){this.dropRead=-1;this.parallel--;return true;}data=patch.slice(off,off+224);}
  if(c===11){const h=unpack(p.slice(6,-1));assert.equal(new DataView(h.buffer).getUint32(0,true),2604);this.data=[];data=[this.token];}
  if(c===12){
   assert.equal(p[6],this.token);const off=p[7]|p[8]<<7,block=[...unpack(p.slice(9,-1))];
   if(off===this.data.length)this.data.push(...block);
   else{assert(this.features&2);assert.deepEqual(this.data.slice(off,off+block.length),block);}
   if(this.dropChunk===off){this.dropChunk=-1;this.parallel--;return true;}
  }
  if(c===13){assert.deepEqual(this.data,patch);if(this.dropCommit){this.parallel--;return true;}}
  if(c===9){
   const extended=p.length===8&&p[6]===1;
   assert.equal(extended,!!(this.features&4));
   data=Array(224).fill(0);data[220]=2;data[221]=Number(extended);
  }
  this.q.push([...p.slice(0,6),status,...pack(data),0xf7]);return true;
 }
 async read(){await new Promise(r=>setTimeout(r,1));this.parallel--;return this.q.shift();}
 close(){this.closed=true;}
}
const link=new Mock(),d=new Device(link,'FM-1_710010100');
await Promise.all([d.command(14,0,300),d.command(41,2,500),d.ui(100,0,10)]);assert.equal(link.max,1);
await d.command(130,7,176);assert.deepEqual(link.log.at(-1).slice(4,-1),[16,link.log.at(-1)[5],2,1,7,48,65]);await d.command(120,0,2);await d.command(123,0,740);assert.throws(()=>d.command(99,0,0));assert.throws(()=>d.command(124,0,0));assert.throws(()=>d.command(127,0,0));
link.dropRead=224;
assert.deepEqual(await d.snapshot(),patch);
assert.equal(link.log.filter(p=>p[4]===5&&(p[6]|p[7]<<7)===224).length,2);
await d.upload(patch);assert.deepEqual(link.data,patch);
await assert.rejects(()=>d.upload([1,2,3]));const state=await d.state();assert.equal(state.models.length,6);assert.equal(state.knobs.length,4);
const saves=link.log.length;await d.bank(0,15,3,3,'MY OWN BIOME');assert.equal(link.log.length,saves+1);assert.equal(link.log.at(-1).length,35);await d.close();assert(link.closed);
await assert.rejects(()=>d.call(7));
// 1.0 has no new editor/backup protocol. Reject before probing capabilities.
const find=PB.find,legacy=new Mock();PB.find=async()=>({link:legacy,id:'FM-1_710010000'});
try{await assert.rejects(()=>Device.connect({}),/1\.0 detected.*live editor requires a later firmware/);assert(legacy.closed);assert.equal(legacy.log.length,0);}
finally{PB.find=find;}
// A released 1.1 endpoint receives only the read-only capability request.
const oldEditor=new Mock();oldEditor.capability=[1,6,1292,22,16,64,224,77];
PB.find=async()=>({link:oldEditor,id:'FM-1_710010100'});
try{await assert.rejects(()=>Device.connect({}),/matching private Mutable Dreams 2\.0 firmware/);assert(oldEditor.closed);assert.equal(oldEditor.log.length,1);assert.equal(oldEditor.log[0][4],7);}
finally{PB.find=find;}
for(const features of [undefined,1,3,7]){
 const bench=new Mock();bench.features=features;
 bench.capability=[1,12,2604,43,16,64,588,261];
 PB.find=async()=>({link:bench,id:'FM-1_710020000'});
 try{
  const editor=await Device.connect({});
  assert.equal(editor.phraseExpression,!!(features&1));
  assert.equal(editor.replayUploadChunk,!!(features&2));
  assert.equal(editor.displayPolarity,!!(features&4));
  if(features&1){await editor.command(161,0,65);assert.equal(bench.log.at(-1)[4],16);}
  else assert.throws(()=>editor.command(161,0,65));
  if(features===1||features===3){
   const raw=patch.slice();raw[4]=12;
   const realCall=editor._call.bind(editor);
   editor._call=(c,p,t)=>realCall(c,p,c===12||c===13?100:t);
   bench.dropChunk=196;
   if(features===1)await assert.rejects(()=>editor.upload(raw),e=>e.code==='device-timeout');
   else{
    // Use the same commit fixture as the normal path, then simulate its lost ACK.
    bench.dropCommit=true;
    await assert.rejects(()=>editor.upload(patch),e=>e.code==='device-timeout');
    assert.equal(bench.log.filter(p=>p[4]===13).length,1);
   }
   const chunks=bench.log.filter(p=>p[4]===12&&(p[7]|p[8]<<7)===196);
   assert.equal(chunks.length,features===3?2:1);
   if(features===3)assert.deepEqual(chunks[0].slice(6),chunks[1].slice(6));
   assert.equal(bench.log.filter(p=>p[4]===14).length,1);
  }
  const look=await editor.state();
  assert.equal(look.buttonGlow,2);
  assert.equal(look.displayInverted,!!(features&4));
  await editor.close();
 }finally{PB.find=find;}
}
console.log('PASS Phrase Tools are negotiated with the device; older editors remain usable.');
console.log('PASS lost staging ACK retries only identical blocks on capable firmware; lost commit ACK is never replayed.');
// Identical device names must fail before opening a port or creating a link.
let opened=0;const port=()=>({name:'Mutable Dreams FM-1',state:'connected',open:async()=>opened++});
await assert.rejects(()=>find({inputs:new Map([[1,port()],[2,port()]]),outputs:new Map([[1,port()]])},()=>{throw Error('Must not link');}),/identical names/);
assert.equal(opened,0);
// Parse the complete packaged page and every inline script without executing UI or audio.
const html=fs.readFileSync(process.argv[2],'utf8');assert(!/__DEVICE_(?:UI|EDITOR|BACKUP)__|__RANGE_CONTROLS__|__WORKLET__|__WASM__/.test(html));
let count=0;for(const m of html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g)){if(m[0].slice(0,m[0].indexOf('>')).includes('application/octet-stream'))continue;new vm.Script(m[1]);count++;}
assert.equal(count,2);for(const id of ['device-connect','device-disconnect','device-backup','device-restore','device-status','device-bank-file'])assert(html.includes(`id="${id}"`));
// Two rapid backup clicks must not start two transfers while pending knobs drain.
const elements=new Map();let releaseFlush,transfers=0;
const flushWait=new Promise(resolve=>releaseFlush=resolve);
const ui=vm.createContext({window:{addEventListener(){}},$:id=>{if(!elements.has(id))elements.set(id,{});return elements.get(id);},setTimeout,clearTimeout,setInterval,clearInterval,flushWait,countTransfer:()=>transfers++});
vm.runInContext(fs.readFileSync(root+'/web/device-ui.js','utf8'),ui);
vm.runInContext('hardwareDevice={remote:async()=>{}}; flushHardwareParams=()=>flushWait; pollHardware=async()=>{};',ui);
const transfer=vm.runInContext('deviceTransfer(async()=>countTransfer())',ui);
assert.equal(vm.runInContext('hardwareBusy',ui),true);
await vm.runInContext('deviceTransfer(async()=>countTransfer())',ui);
releaseFlush();await transfer;assert.equal(transfers,1);assert.equal(vm.runInContext('hardwareBusy',ui),false);
vm.runInContext('hardwareDevice.remote=async()=>{throw Error("Disconnected during release")};',ui);
await vm.runInContext('deviceTransfer(async()=>countTransfer())',ui);
assert.equal(transfers,1);assert.equal(vm.runInContext('hardwareBusy',ui),false);assert.equal(elements.get('device-backup').disabled,false);
console.log('PASS 223 binary packing cases, all 22 backup objects, corrupt/truncated/wrong-ID rejection, serialized commands, immutable snapshot CRC, chunk upload, bank naming/save, disconnect, complete packaged HTML/JS syntax.');
console.log('PASS UI transfer lock before pending controls drain; repeated-click exclusion and disconnect cleanup (isolated JS, not a browser GUI test).');

assert(!html.includes("localStorage.setItem('mutable-dreams-1.5'"));
assert(!html.includes("localStorage.setItem('mutable-dreams-1.4'"));
assert(html.includes("localStorage.setItem('mutable-dreams-2.0-dev'"));
assert(html.includes("localStorage.getItem('mutable-dreams-1.5')??localStorage.getItem('mutable-dreams-1.4')??localStorage.getItem('mutable-dreams-1.3')"));
console.log('PASS private 2.0 browser saves isolated from released 1.5; read-only initial migration retained.');

// New DSP command IDs 120..123 must never be dispatched as UI commands.
const routed=[],routing=vm.createContext({window:{addEventListener(){}},$:()=>({}),setTimeout,clearTimeout,setInterval,clearInterval,routed});
vm.runInContext(fs.readFileSync(root+'/web/device-ui.js','utf8'),routing);
vm.runInContext('hardwareDevice={command:async(...a)=>routed.push(["dsp",...a]),ui:async(...a)=>routed.push(["ui",...a])};',routing);
for(const c of [120,121,122,123]){vm.runInContext(`hardwarePost({type:"app",cmd:"command",c:${c},t:2,v:1})`,routing);await new Promise(r=>setTimeout(r,60));}
vm.runInContext('hardwarePost({type:"app",cmd:"command",c:103,t:0,v:1})',routing);await new Promise(r=>setTimeout(r,10));
vm.runInContext('hardwarePost({type:"app",cmd:"command",c:108,t:0,v:2})',routing);await new Promise(r=>setTimeout(r,10));
assert.deepEqual(routed.map(x=>Array.from(x)),[["dsp",120,2,1],["dsp",121,2,1],["dsp",122,2,1],["dsp",123,2,1],["ui",103,0,1],["ui",108,0,2]]);
console.log('PASS LFO B rate uses DSP protocol while LED preference uses its own UI ID.');
