import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';import path from 'node:path';
const root=path.resolve(path.dirname(new URL(import.meta.url).pathname),'..');
vm.runInThisContext(fs.readFileSync(root+'/web/device-backup.js','utf8'));
vm.runInThisContext(fs.readFileSync(root+'/web/device-editor.js','utf8'));
const {PB}=DreamsBackup,{Device,pack,unpack}=DreamsDevice;
for(let n=1;n<224;n++){const b=Array.from({length:n},(_,i)=>(i*157+n)%256);assert.deepEqual([...unpack(pack(b))],b);}
const image=fs.readFileSync(root+'/app/build/san/dreams-map-test.mddb');assert.equal(PB.parse(image).objs.length,22);
const bad=Buffer.from(image);bad[35]^=1;assert.throws(()=>PB.parse(bad));assert.throws(()=>PB.parse(image.slice(0,-1)));
const wrong=Buffer.from(image);wrong.writeUInt16LE(1,16);wrong.writeUInt32LE(PB.crc32(wrong,0,wrong.length-4),wrong.length-4);assert.throws(()=>PB.parse(wrong));
const le=n=>[n&255,n>>>8&255,n>>>16&255,n>>>24&255];
const patch=Array.from({length:2288},(_,i)=>(i*17)&255);
class Mock{
 constructor(){this.q=[];this.log=[];this.lost=false;this.data=[];this.token=7;this.parallel=0;this.max=0;}
 send(p){this.log.push(p);this.parallel++;this.max=Math.max(this.max,this.parallel);const c=p[4];let status=0,data=[];
  if(p[2]===0x4c){this.q.push([0xf0,0x7d,0x4c,0x43,c,0xf7]);return true;}
  if(c===7)data=(this.capability||[1,9,2288,29,16,64,352,157]).flatMap(le);
  if(c===8)data=[2288,PB.crc32(patch),4].flatMap(le);
  if(c===5){assert.equal(p[8],4);const off=p[6]|p[7]<<7;data=patch.slice(off,off+224);}
  if(c===11){const h=unpack(p.slice(6,-1));assert.equal(new DataView(h.buffer).getUint32(0,true),2288);this.data=[];data=[this.token];}
  if(c===12){assert.equal(p[6],this.token);const off=p[7]|p[8]<<7;assert.equal(off,this.data.length);this.data.push(...unpack(p.slice(9,-1)));}
  if(c===13){assert.deepEqual(this.data,patch);}
  if(c===9){data=Array(220).fill(0);}
  this.q.push([...p.slice(0,6),status,...pack(data),0xf7]);return true;
 }
 async read(){await new Promise(r=>setTimeout(r,1));this.parallel--;return this.q.shift();}
 close(){this.closed=true;}
}
const link=new Mock(),d=new Device(link,'FM-1_710010100');
await Promise.all([d.command(14,0,300),d.command(41,2,500),d.ui(100,0,10)]);assert.equal(link.max,1);
await d.command(120,0,2);await d.command(123,0,740);assert.throws(()=>d.command(99,0,0));assert.throws(()=>d.command(124,0,0));
assert.deepEqual(await d.snapshot(),patch);await d.upload(patch);assert.deepEqual(link.data,patch);
await assert.rejects(()=>d.upload([1,2,3]));const state=await d.state();assert.equal(state.models.length,6);assert.equal(state.knobs.length,4);
const saves=link.log.length;await d.bank(0,15,3,3,'MY OWN BIOME');assert.equal(link.log.length,saves+1);assert.equal(link.log.at(-1).length,35);await d.close();assert(link.closed);
await assert.rejects(()=>d.call(7));
// 1.0 has no new editor/backup protocol. Reject before probing capabilities.
const find=PB.find,legacy=new Mock();PB.find=async()=>({link:legacy,id:'FM-1_710010000'});
try{await assert.rejects(()=>Device.connect({}),/1\.0 detected.*matching 1\.3 development firmware/);assert(legacy.closed);assert.equal(legacy.log.length,0);}
finally{PB.find=find;}
// A released 1.1 endpoint receives only the read-only capability request.
const oldEditor=new Mock();oldEditor.capability=[1,6,1292,22,16,64,224,77];
PB.find=async()=>({link:oldEditor,id:'FM-1_710010100'});
try{await assert.rejects(()=>Device.connect({}),/matching 1\.3 development/);assert(oldEditor.closed);assert.equal(oldEditor.log.length,1);assert.equal(oldEditor.log[0][4],7);}
finally{PB.find=find;}
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

assert(!html.includes("localStorage.setItem('mutable-dreams-1.2'"));
assert(!html.includes("localStorage.setItem('mutable-dreams-1.3'"));
assert(html.includes("localStorage.setItem('mutable-dreams-1.4'"));
assert(html.includes("localStorage.getItem('mutable-dreams-1.4')??localStorage.getItem('mutable-dreams-1.3')??localStorage.getItem('mutable-dreams-1.2')"));
console.log('PASS private 1.4 browser saves isolated from released 1.3; read-only initial migration retained.');

// New DSP command IDs 120..123 must never be dispatched as UI commands.
const routed=[],routing=vm.createContext({window:{addEventListener(){}},$:()=>({}),setTimeout,clearTimeout,setInterval,clearInterval,routed});
vm.runInContext(fs.readFileSync(root+'/web/device-ui.js','utf8'),routing);
vm.runInContext('hardwareDevice={command:async(...a)=>routed.push(["dsp",...a]),ui:async(...a)=>routed.push(["ui",...a])};',routing);
for(const c of [120,121,122,123]){vm.runInContext(`hardwarePost({type:"app",cmd:"command",c:${c},t:2,v:1})`,routing);await new Promise(r=>setTimeout(r,60));}
vm.runInContext('hardwarePost({type:"app",cmd:"command",c:103,t:0,v:1})',routing);await new Promise(r=>setTimeout(r,10));
vm.runInContext('hardwarePost({type:"app",cmd:"command",c:124,t:0,v:2})',routing);await new Promise(r=>setTimeout(r,10));
assert.deepEqual(routed.map(x=>Array.from(x)),[["dsp",120,2,1],["dsp",121,2,1],["dsp",122,2,1],["dsp",123,2,1],["ui",103,0,1],["ui",124,0,2]]);
console.log('PASS LFO B hardware editor uses DSP protocol, coalesced depth/rate preserved; LED preference uses UI protocol.');
