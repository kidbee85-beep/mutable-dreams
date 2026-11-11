// Runs the actual WASM/UI/worklet without opening an audio device.
import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';
let Processor;globalThis.currentTime=0;const messages=[];
globalThis.AudioWorkletProcessor=class{constructor(){this.port={postMessage:m=>messages.push(m)}}};globalThis.registerProcessor=(_,p)=>Processor=p;
vm.runInThisContext(fs.readFileSync('web/app_worklet.js','utf8'));vm.runInThisContext(fs.readFileSync('web/worklet.js','utf8').replace('import "./app_worklet.js";',''));
const wasm=fs.readFileSync('app/build/wasm/dreams.wasm'),f=new Processor();await f.onMessage({type:'load',wasm,master:2400,store:{}});
const out=[[new Float32Array(128),new Float32Array(128)]];
function run(n){while(n--){globalThis.currentTime+=128/44100;f.process([],out);for(const a of out[0])for(const x of a)assert(Number.isFinite(x)&&Math.abs(x)<1);}}
function c(id,t,v){f.ex.dream_command(id,t,v);run(2);}
function patch(p=f){return [...new Uint8Array(p.mem.buffer,p.ex.dream_patch_data(),p.ex.dream_patch_size())];}
c(130,7,176);c(131,7,64);c(132,7,16);c(133,7,-24);assert.deepEqual(patch().slice(2348,2354),[176,0,64,16,0,232]);c(20,0,169);assert.equal(f.ex.dream_value(117,0),169);c(78,0,1);c(15,0,120);c(128,0,1);c(129,0,2);c(130,1,171);c(131,0,1);c(131,1,1);c(134,1,3);c(135,1,11); // tracks 1, 2 and 6 carry; no other bits lost
assert.equal(patch()[2288+4+8+4],35);
f.ex.dream_page(31);c(109,0,1);assert.deepEqual([0,1,2,3].map(k=>f.ex.dream_raw_knob(31,0,k)),[1,1,171,2]);
f.ex.dream_page(32);assert.deepEqual([0,1,2,3].map(k=>f.ex.dream_raw_knob(32,0,k)),[1,1,1,0]);
f.ex.dream_track(5);f.ex.dream_page(33);assert.deepEqual([0,1,2,3].map(k=>f.ex.dream_raw_knob(33,5,k)),[1,5,1,1]);
const home=patch();c(16,0,1);run(700);assert.equal(f.ex.dream_value(20,0),171);assert.deepEqual(patch(),home);c(0,0,8);

assert.deepEqual(patch(),home);c(14,3,999);assert.deepEqual(patch(),home);run(700);assert.equal(f.ex.dream_value(16,0),0);assert.deepEqual(patch(),home);
c(112,0,0);c(111,0,15);c(114,0,0);assert(f.ex.web_store_len(18)>0);const stores={};for(let i=0;i<f.ex.web_nobj();i++){const n=f.ex.web_store_len(i);if(n>0)stores[i]=[...new Uint8Array(f.mem.buffer,f.ex.web_store(i),n)];}
const g=new Processor();await g.onMessage({type:'load',wasm,master:2400,store:stores});g.ex.dream_command(111,0,15);g.ex.dream_command(113,0,0);assert.deepEqual(patch(g),home);
const old=home.slice(0,2288);old[4]=9;await f.onMessage({type:'app',cmd:'patch-import',bytes:old,id:91});run(2);assert(messages.some(m=>m.id===91&&m.loaded));assert.equal(patch()[2288],0);assert.equal(patch()[4],12);
console.log('PASS WASM Journey controls, section 8 namespace, carry bit preservation, canonical live export, protected edits, temporary macros, Once stop, User Biome save/load/reboot, v9 migration. No audio device opened.');
