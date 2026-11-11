// Export a real-engine Journey example without opening an audio device.
import fs from 'node:fs';import vm from 'node:vm';import path from 'node:path';
const root=path.resolve(path.dirname(new URL(import.meta.url).pathname),'..');process.chdir(root);
let Processor;globalThis.currentTime=0;const messages=[];
globalThis.AudioWorkletProcessor=class{constructor(){this.port={postMessage:m=>messages.push(m)}}};globalThis.registerProcessor=(_,p)=>Processor=p;
vm.runInThisContext(fs.readFileSync('web/app_worklet.js','utf8'));vm.runInThisContext(fs.readFileSync('web/worklet.js','utf8').replace('import "./app_worklet.js";',''));
const f=new Processor();await f.onMessage({type:'load',wasm:fs.readFileSync('app/build/wasm/dreams.wasm'),master:0,store:{}});const out=[[new Float32Array(128),new Float32Array(128)]];
function c(id,t,v){f.ex.dream_command(id,t,v);for(let i=0;i<2;i++){globalThis.currentTime+=128/44100;f.process([],out);}}
c(20,0,169);c(15,0,90);c(128,0,1);c(129,0,4);
for(const [i,id] of [-1,171,174,176].entries()){c(130,i,id);c(131,i,4);c(132,i,1);c(133,i,0);c(134,i,i===1?2:0);}
const p={format:'mutable-dreams-patch',version:1,firmware:'2.0-dev',name:'Four Worlds / Journey',patch:[...new Uint8Array(f.mem.buffer,f.ex.dream_patch_data(),f.ex.dream_patch_size())]};
const file=process.argv[2];if(!file)throw Error('Pass an output JSON file.');fs.writeFileSync(file,JSON.stringify(p)+'\n');
if(process.argv.includes('--catalog')){const a=JSON.parse(fs.readFileSync('presets/development-patches.json','utf8')).filter(x=>x.name!==p.name);a.push(p);fs.writeFileSync('presets/development-patches.json',JSON.stringify(a)+'\n');}
console.log('Exported '+p.patch.length+' bytes; no audio device or FM-1 opened.');
