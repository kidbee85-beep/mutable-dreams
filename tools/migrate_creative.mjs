// Offline migration using the same firmware decoder as the hardware.
import fs from 'node:fs';import vm from 'node:vm';
let P;globalThis.currentTime=0;globalThis.AudioWorkletProcessor=class{constructor(){this.port={postMessage(){}}}};globalThis.registerProcessor=(_,p)=>P=p;
vm.runInThisContext(fs.readFileSync('web/app_worklet.js','utf8'));vm.runInThisContext(fs.readFileSync('web/worklet.js','utf8').replace('import "./app_worklet.js";',''));
const f=new P();await f.onMessage({type:'load',wasm:fs.readFileSync('app/build/wasm/dreams.wasm'),master:0,store:{}});
const input=fs.readFileSync(process.argv[2]),e=f.ex;
new Uint8Array(f.mem.buffer,e.dream_patch_buffer(),input.length).set(input);if(!e.dream_patch_apply(input.length))throw Error('Firmware decoder rejected input');
const output=Buffer.from(new Uint8Array(f.mem.buffer,e.dream_patch_data(),e.dream_patch_size()));if(output.length!==2604||output.readUInt32LE(4)!==12)throw Error('Unexpected migrated version');fs.writeFileSync(process.argv[3],output);console.log('Firmware migration:',input.length,'→',output.length,'bytes. No device/audio opened.');
