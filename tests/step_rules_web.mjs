// Current WASM and worklet, including persisted v12 conditions and old imports.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
let Processor;
globalThis.currentTime=0;
globalThis.AudioWorkletProcessor=class {constructor(){this.port={postMessage(){}}}};
globalThis.registerProcessor=(_,p)=>Processor=p;
vm.runInThisContext(fs.readFileSync('web/app_worklet.js','utf8'));
vm.runInThisContext(fs.readFileSync('web/worklet.js','utf8').replace('import "./app_worklet.js";',''));
const wasm=fs.readFileSync('app/build/wasm/dreams.wasm');
const f=new Processor();
await f.onMessage({type:'load',wasm,master:2000,store:{}});
const out=[[new Float32Array(128),new Float32Array(128)]];
function run(n=2){while(n--){currentTime+=128/44100;f.process([],out);for(const a of out[0])for(const x of a)assert(Number.isFinite(x)&&Math.abs(x)<1)}}
function command(c,t,v){f.ex.dream_command(c,t,v);run()}
function patch(x=f){return [...new Uint8Array(x.mem.buffer,x.ex.dream_patch_data(),x.ex.dream_patch_size())]}
command(20,0,64);command(64,0,1);command(66,0,60);
f.ex.dream_page(42);
command(127,2,3);
assert.deepEqual([0,1,2,3].map(k=>f.ex.dream_raw_knob(42,0,k)),[0,0,3,60]);
assert.equal(patch()[1298]&128,128);
assert.equal(patch()[1299]&128,128);
f.ex.dream_page(20);
command(68,0,41);command(69,0,73);
assert.equal(patch()[1298],128+41);assert.equal(patch()[1299],128+73);
f.ex.dream_page(42);assert.equal(f.ex.dream_raw_knob(42,0,2),3);
const untouched=patch();
f.ex.dream_page(41);
command(127,3,4);
command(127,2,100);
assert.deepEqual([0,1,2,3].map(k=>f.ex.dream_raw_knob(41,0,k)),[1,0,100,4]);
async function tap(mask){
 await f.onMessage({type:'buttons',mask});run(12);
 await f.onMessage({type:'buttons',mask:0});run(12);
}
await tap(2);
assert.equal(patch()[1297],Math.min(100,untouched[1297]+20));
assert.equal(patch()[1298],untouched[1298]);
assert.equal(patch()[1299],untouched[1299]);
command(127,3,5);
await tap(2);
assert.equal(patch()[1299],128+85);
assert.equal(patch()[1298],untouched[1298]);
await tap(4);
assert.equal(patch()[1299],untouched[1299]);
console.log('PASS Phrase Tools Accent and Articulate use the native WASM panel paths and preserve rule flags.');
command(111,0,15);command(112,0,0);command(114,0,0);
const saved=patch(),stores={};
for(let i=0;i<f.ex.web_nobj();i++){const n=f.ex.web_store_len(i);if(n>0)stores[i]=[...new Uint8Array(f.mem.buffer,f.ex.web_store(i),n)]}
const g=new Processor();await g.onMessage({type:'load',wasm,master:2000,store:stores});
g.ex.dream_command(111,0,15);g.ex.dream_command(113,0,0);
assert.deepEqual(patch(g),saved);
const old=JSON.parse(fs.readFileSync('presets/development-patches.json','utf8'))[0].patch;
await f.onMessage({type:'app',cmd:'patch-import',bytes:old,id:10});run();
assert.equal(patch()[4],12);assert.equal(patch().length,2604);
console.log('PASS current WASM rule editing, masked percentages, User Biome save/load/restart, and legacy import.');
