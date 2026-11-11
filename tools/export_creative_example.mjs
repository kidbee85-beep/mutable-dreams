import fs from 'node:fs';import vm from 'node:vm';
let P;globalThis.currentTime=0;globalThis.AudioWorkletProcessor=class{constructor(){this.port={postMessage(){}}}};globalThis.registerProcessor=(_,p)=>P=p;
vm.runInThisContext(fs.readFileSync('web/app_worklet.js','utf8'));vm.runInThisContext(fs.readFileSync('web/worklet.js','utf8').replace('import "./app_worklet.js";',''));
const f=new P();await f.onMessage({type:'load',wasm:fs.readFileSync('app/build/wasm/dreams.wasm'),master:0,store:{}});const out=[[new Float32Array(128),new Float32Array(128)]];
const c=(id,t,v)=>{f.ex.dream_command(id,t,v);currentTime+=128/44100;f.process([],out)};c(20,0,176);c(15,0,76);for(let k=0;k<4;k++)c(14,k,[460,340,420,400][k]);
for(let i=0;i<6;i++){c(4,i,Math.min(530,f.ex.dream_raw_knob(12,i,0)));c(96,i,0);c(56,i,0);c(122,i,0);c(57,i,500+i*55);c(123,i,400+i*55)}
c(153,0,0);
for(let i=0;i<6;i++){let tone=f.ex.dream_raw_knob(1,i,0),pan=f.ex.dream_raw_knob(4,i,2),cut=f.ex.dream_raw_knob(10,i,0);c(1,i,Math.max(120,Math.min(800,tone+(i%2?65:-60))));c(41,i,Math.max(170,Math.min(800,cut+(i%2?-90:70))));c(11,i,Math.max(80,Math.min(920,pan+(i%2?100:-100))))}
c(14,2,470);c(14,3,470);c(153,0,1);
for(let j=0;j<4;j++){for(const [id,v] of [[144,[0,3,4,1][j]],[146,[2,5,3,4][j]],[145,[0,2,3,1][j]],[147,[0,2,3,4][j]],[148,[80,140,120,80][j]],[149,500]])c(id,j,v)}
c(152,0,80);c(154,0,650);c(150,0,2);
const bytes=[...new Uint8Array(f.mem.buffer,f.ex.dream_patch_data(),f.ex.dream_patch_size())];fs.writeFileSync(process.argv[2],JSON.stringify({format:'mutable-dreams-patch',version:1,firmware:'2.0-dev-v11',name:'LIVING TIDELANDS',patch:bytes},null,2)+'\n');console.log('LIVING TIDELANDS:',bytes.length,'bytes; four gentle links and a 33-second Drift.');
