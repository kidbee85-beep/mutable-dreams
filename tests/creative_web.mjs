// Actual firmware WASM and AudioWorklet; no audio device is opened.
import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';
let Processor;globalThis.currentTime=0;const messages=[];
globalThis.AudioWorkletProcessor=class{constructor(){this.port={postMessage:m=>messages.push(m)}}};globalThis.registerProcessor=(_,p)=>Processor=p;
vm.runInThisContext(fs.readFileSync('web/app_worklet.js','utf8'));vm.runInThisContext(fs.readFileSync('web/worklet.js','utf8').replace('import "./app_worklet.js";',''));
const wasm=fs.readFileSync('app/build/wasm/dreams.wasm'),f=new Processor();await f.onMessage({type:'load',wasm,master:2400,store:{}});
const out=[[new Float32Array(128),new Float32Array(128)]];
function run(n=2){while(n--){currentTime+=128/44100;f.process([],out);for(const a of out[0])for(const x of a)assert(Number.isFinite(x)&&Math.abs(x)<1);}}
const c=(id,t,v)=>{f.ex.dream_command(id,t,v);run()};
function midi(channel,cc,value){f.ex.web_midi_in(0,((0xb0+channel-1)<<8)|(cc<<16)|(value<<24)|11);run()}
function patch(x=f){return [...new Uint8Array(x.mem.buffer,x.ex.dream_patch_data(),x.ex.dream_patch_size())]}
run(1300);c(110,0,15);f.ex.dream_page(34);c(136,15,4);c(137,15,2);c(4,2,600);c(138,15,1);midi(4,74,127);assert.equal(f.ex.dream_value(202,0),-1);midi(4,74,0);assert.equal(f.ex.dream_raw_knob(12,2,0),600);midi(4,74,100);assert.equal(f.ex.dream_raw_knob(12,2,0),787);run(20);assert(messages.some(m=>m.type==='state'&&m.learnSlot===15&&m.learnPicked===1));
// Save preferences through the exact hardware button edge and emulate a reboot.
f.ex.web_buttons(1<<7);f.ex.dream_input_edge();run(30);f.ex.web_buttons(0);f.ex.dream_input_edge();run(2);
const stores={};for(let i=0;i<f.ex.web_nobj();i++){let n=f.ex.web_store_len(i);if(n>0)stores[i]=[...new Uint8Array(f.mem.buffer,f.ex.web_store(i),n)]}assert.equal(stores[2].length,156);
const g=new Processor();await g.onMessage({type:'load',wasm,master:2400,store:stores});g.ex.dream_command(110,0,15);g.ex.dream_page(34);assert.equal(g.ex.dream_raw_knob(34,0,1),4);g.ex.dream_page(35);assert.equal(g.ex.dream_raw_knob(35,0,2),74);
c(117,0,3);c(144,3,2);c(146,3,4);c(147,3,1);c(145,3,3);c(148,3,-450);c(149,3,800);f.ex.dream_page(37);assert.deepEqual([0,1,2,3].map(k=>f.ex.dream_raw_knob(37,0,k)),[3,3,-450,800]);
c(153,0,0);c(41,2,200);c(14,2,250);c(153,0,1);c(150,0,2);c(152,0,0);run(1100);assert(f.ex.dream_value(204,0)>500);assert.equal(patch().length,2604);const saved=patch();
c(111,0,15);c(112,0,0);c(114,0,0);c(150,0,0);c(113,0,0);c(78,0,1); // dirty User Biome load requires SEL rather than factory confirm
if(f.ex.dream_value(119,0)>=0){f.ex.web_buttons(2);f.ex.dream_input_edge();run();f.ex.web_buttons(0);f.ex.dream_input_edge();run()}
assert.deepEqual(patch(),saved);
// Exact old v10 import uses the migrated off-by-default creative tail.
const old=saved.slice(0,2356);old[4]=10;await f.onMessage({type:'app',cmd:'patch-import',bytes:old,id:901});run();assert(messages.some(m=>m.id===901&&m.loaded));assert.equal(patch()[4],12);assert.equal(patch()[2356+32+6],0);
console.log('PASS actual WASM: learn/pickup, slot 16 metadata, global mapping save/reboot, fourth link, autonomous Drift, user Biome round-trip, v10 migration, finite stereo; no audio output opened.');
