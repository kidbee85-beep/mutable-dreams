import fs from 'node:fs';import vm from 'node:vm';import path from 'node:path';
let P;globalThis.currentTime=0;globalThis.AudioWorkletProcessor=class{constructor(){this.port={postMessage(){}}}};globalThis.registerProcessor=(_,p)=>P=p;
vm.runInThisContext(fs.readFileSync('web/app_worklet.js','utf8'));vm.runInThisContext(fs.readFileSync('web/worklet.js','utf8').replace('import "./app_worklet.js";',''));
const f=new P();await f.onMessage({type:'load',wasm:fs.readFileSync('app/build/wasm/dreams.wasm'),master:0,store:{}});
const out=[[new Float32Array(128),new Float32Array(128)]];
const cmd=(id,t,v)=>{f.ex.dream_command(id,t,v);currentTime+=128/44100;f.process([],out)};
fs.mkdirSync(process.argv[2],{recursive:true});
const start=Number(process.argv[3]??177),end=Number(process.argv[4]??189);
for(let i=start;i<end;i++){
 cmd(16,0,0);cmd(20,0,i);cmd(78,0,1);
 const patch=[...new Uint8Array(f.mem.buffer,f.ex.dream_patch_data(),f.ex.dream_patch_size())];
 if(new DataView(Uint8Array.from(patch).buffer).getInt32(620,true)!==i)throw Error('Wrong factory source');
 fs.writeFileSync(path.join(process.argv[2],`Biome-${i}.json`),JSON.stringify({format:'mutable-dreams-patch',version:1,firmware:'2.0-private',patch},null,2)+'\n');
}
console.log(`Exported ${end-start} exact factory projects from the shared firmware WASM.`);
