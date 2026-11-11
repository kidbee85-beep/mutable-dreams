// Migrate a saved patch through the actual WASM firmware decoder, silently.
import fs from 'node:fs';import vm from 'node:vm';
let P;globalThis.currentTime=0;globalThis.AudioWorkletProcessor=class{constructor(){this.port={postMessage(){}}}};globalThis.registerProcessor=(_,p)=>P=p;
vm.runInThisContext(fs.readFileSync('web/app_worklet.js','utf8'));vm.runInThisContext(fs.readFileSync('web/worklet.js','utf8').replace('import "./app_worklet.js";',''));
const f=new P();await f.onMessage({type:'load',wasm:fs.readFileSync('app/build/wasm/dreams.wasm'),master:0,store:{}});
const p=fs.readFileSync(process.argv[2]);new Uint8Array(f.mem.buffer,f.ex.dream_patch_buffer(),f.ex.dream_patch_size()).set(p);if(!f.ex.dream_patch_apply(p.length))throw Error('Firmware rejected the patch');
const out=Buffer.from(new Uint8Array(f.mem.buffer,f.ex.dream_patch_data(),f.ex.dream_patch_size()));fs.writeFileSync(process.argv[3],out);console.log(`Migrated ${p.length} → ${out.length} bytes without audio/device access.`);
