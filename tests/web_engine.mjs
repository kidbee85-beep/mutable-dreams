import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';import path from 'node:path';
const root=path.resolve(path.dirname(new URL(import.meta.url).pathname),'..');let Processor;globalThis.currentTime=0;const messages=[];
globalThis.AudioWorkletProcessor=class{constructor(){this.port={postMessage:m=>messages.push(m)}}};globalThis.registerProcessor=(name,p)=>{Processor=p};
vm.runInThisContext(fs.readFileSync(root+'/web/app_worklet.js','utf8'));vm.runInThisContext(fs.readFileSync(root+'/web/worklet.js','utf8').replace('import "./app_worklet.js";',''));
const f=new Processor();await f.onMessage({type:'load',wasm:fs.readFileSync(root+'/app/build/wasm/dreams.wasm'),master:2400,store:{}});assert(messages.some(m=>m.type==='ready'));const out=[[new Float32Array(128),new Float32Array(128)]];let peak=0;
function render(n){for(let b=0;b<n;b++){globalThis.currentTime+=128/44100;f.process([],out);for(const a of out[0])for(const x of a){assert(Number.isFinite(x));assert(Math.abs(x)<=1);peak=Math.max(peak,Math.abs(x));}if(messages.length>300)messages.splice(0,250)}}
render(1200);
for(let page=0;page<30;page++){await f.onMessage({type:'app',cmd:'page',v:page});render(35);assert.equal(f.ex.dream_value(100,0),page);for(let k=0;k<4;k++)assert(Number.isFinite(f.ex.dream_raw_knob(page,0,k)))}
assert.equal(f.ex.dream_patch_size(),2288);
for(let demo=0;demo<5;demo++){const bytes=[...fs.readFileSync(root+`/docs/development/audio/${demo}.patch`)];await f.onMessage({type:'app',cmd:'patch-import',bytes,id:demo});render(2);assert(messages.some(m=>m.type==='patch-result'&&m.id===demo&&m.loaded));await f.onMessage({type:'app',cmd:'command',c:16,t:0,v:1});render(1400);await f.onMessage({type:'app',cmd:'command',c:16,t:0,v:0});render(2)}
const p=[...new Uint8Array(f.mem.buffer,f.ex.dream_patch_data(),2288)];await f.onMessage({type:'app',cmd:'patch-export',id:77});render(2);assert.deepEqual(messages.find(m=>m.id===77).bytes,p);
const bad=p.slice();bad[0]=0;await f.onMessage({type:'app',cmd:'patch-import',id:78,bytes:bad});render(2);assert(messages.find(m=>m.id===78).error);assert.deepEqual([...new Uint8Array(f.mem.buffer,f.ex.dream_patch_data(),2288)],p);
const old=p.slice(0,652);old[4]=4;await f.onMessage({type:'app',cmd:'patch-import',id:79,bytes:old});render(2);assert(messages.find(m=>m.id===79).loaded);
await f.onMessage({type:'app',cmd:'command',c:45,t:0,v:207});render(2);assert.equal(f.ex.dream_value(45,0),207);

// User Biome save/load and explicit overwrite guard, then reboot persisted objects.
const command=(c,v)=>f.ex.dream_command(c,0,v);
for(const [c,v] of [[93,4],[94,25],[95,3],[96,1],[97,2],[56,450],[57,400],[120,2],[121,3],[122,600],[123,740],[98,1]]){command(c,v);render(2);}
assert.equal(f.ex.dream_raw_knob(28,0,2),450);assert.equal(f.ex.dream_raw_knob(29,0,2),600);
command(112,0);command(111,15);command(114,0);render(2);
assert(f.ex.web_store_len(18)>0);
const saved=[...new Uint8Array(f.mem.buffer,f.ex.dream_patch_data(),2288)];
command(45,160);render(2);command(114,0);render(2); // arms overwrite, leaves old slot
command(113,0);render(2);assert.equal(f.ex.dream_value(119,0),15);f.ex.web_buttons(2);f.ex.dream_input_edge();render(2);f.ex.web_buttons(0);f.ex.dream_input_edge();render(2);assert.deepEqual([...new Uint8Array(f.mem.buffer,f.ex.dream_patch_data(),2288)],saved);
command(112,1);command(111,63);command(114,0);render(2);assert(f.ex.web_store_len(22)>0);
const stores={};for(let i=0;i<f.ex.web_nobj();i++){const n=f.ex.web_store_len(i);if(n>0)stores[i]=[...new Uint8Array(f.mem.buffer,f.ex.web_store(i),n)];}
const g=new Processor();await g.onMessage({type:'load',wasm:fs.readFileSync(root+'/app/build/wasm/dreams.wasm'),master:2400,store:stores});g.ex.dream_command(111,0,15);g.ex.dream_command(113,0,0);
assert.deepEqual([...new Uint8Array(g.mem.buffer,g.ex.dream_patch_data(),2288)],saved);
// Every new warm tone renders without invalid output.
for(let id=160;id<208;id++){command(45,id);render(2);f.ex.web_midi_in(0,(0x90<<8)|(48<<16)|(100<<24)|9);render(100);f.ex.web_midi_in(0,(0x80<<8)|(48<<16)|8);render(100);}
for(let id=208;id<352;id++){command(45,id);render(2);f.ex.web_midi_in(0,(0x90<<8)|(48<<16)|(100<<24)|9);render(100);f.ex.web_midi_in(0,(0x80<<8)|(48<<16)|8);render(100);}
console.log('PASS user Biome / sound banks, overwrite guard, restart, 192 warm and new tones (IDs 160–351)');
console.log(JSON.stringify({result:'PASS',pages:30,patchBytes:2288,legacyImport:true,invalidPatchUnchanged:true,exportRoundTrip:true,newDemos:5,tone207:true,peak,wasmMemory:f.mem.buffer.byteLength,hardwareTest:false},null,2));
