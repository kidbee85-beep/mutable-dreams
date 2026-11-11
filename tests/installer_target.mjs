// Isolated read/open selection tests. No browser or real MIDI endpoint is opened.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const root=new URL('../base/web/',import.meta.url);
const strip=s=>s.replace(/^import .*?;\n/gm,'').replace(/^export\s+/gm,'');
const ctx=vm.createContext({console,setTimeout,clearTimeout,performance,Uint8Array});
vm.runInContext(['fm1pkg.js','fm1ports.js','fm1ota.js'].map(n=>strip(fs.readFileSync(new URL(n,root),'utf8'))).join('\n'),ctx);
let opened=0;
const port=(name,state='connected')=>({name,state,open:async()=>opened++,close(){},send(){}});
const access=(ins,outs)=>({inputs:new Map(ins.map((x,i)=>[i,x])),outputs:new Map(outs.map((x,i)=>[i,x]))});
async function find(ins,outs){ctx.access=access(ins,outs);return vm.runInContext('new Updater(access).find()',ctx);}
await assert.rejects(()=>find([port('Mutable Dreams FM-1'),port('Syntherella FM-1')],[port('Mutable Dreams FM-1'),port('Syntherella FM-1')]),/More than one/);
await assert.rejects(()=>find([port('FM-1'),port('FM-1')],[port('FM-1')]),/More than one/);
await assert.rejects(()=>find([port('FM-1')],[port('FM-1'),port('FM-1')]),/Duplicate MIDI/);
assert.equal(await find([port('FM-1')],[port('Unrelated Keyboard')]),null);
assert.equal(await find([port('Unrelated Keyboard')],[port('Unrelated Keyboard')]),null);
assert.equal(await find([port('FM-1','disconnected')],[port('FM-1')]),null);
assert.equal(opened,0,'Ambiguous or mismatched devices must never be opened');
// Replace only the handshake to observe exact selection without sending bytes.
vm.runInContext('handshake=async()=>({text:"FM-1_710010200"})',ctx);
const selected=await find([port('Mutable Dreams FM-1'),port('Keyboard')],[port('Mutable Dreams FM-1'),port('Keyboard')]);
assert.equal(selected.name,'Mutable Dreams FM-1');assert.equal(opened,2);selected.link.close();
const numbered=await find([port('FM-1 1')],[port('FM-1 2'),port('Unrelated Keyboard')]);
assert.equal(numbered.name,'FM-1 1');assert.equal(opened,4);numbered.link.close();
await assert.rejects(()=>find([port('FM-1 1')],[port('FM-1 2'),port('FM-1 3')]),/Duplicate MIDI/);
assert.equal(opened,4);
console.log('PASS installer refuses multiple FM-1 units, duplicate endpoints and unrelated fallback before opening; exact unique pairing accepted.');
