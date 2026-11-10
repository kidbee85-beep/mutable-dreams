import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {createHash, webcrypto} from 'node:crypto';

const html = fs.readFileSync(process.argv[2], 'utf8');
const firmware = fs.readFileSync(process.argv[3]);
const script = [...html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)].map(m => m[1]).find(s => s.includes('class Updater'));
assert(script, 'The shipped HTML must contain the updater');
const bundle = script.slice(0, script.indexOf('const $ ='));
const ctx = vm.createContext({console, Uint8Array, setTimeout, clearTimeout, performance});
vm.runInContext(bundle, ctx);
const access = {inputs: new Map(), outputs: new Map()};
ctx.access = access;
// This exact constructor on the shipped HTML reproduces the missing PORT_NAMES regression.
vm.runInContext('new Updater(access)', ctx);
assert.deepEqual(Array.from(vm.runInContext('new Updater(access).ports()', ctx)), []);
assert.equal(vm.runInContext('new Updater(access).askable("FM-1")', ctx), true);
assert.equal(vm.runInContext('new Updater(access).askable("FM-1 Bluetooth")', ctx), false);
assert.equal(vm.runInContext('new Updater(access).askable("Unrelated Keyboard")', ctx), false);
await vm.runInContext('new Updater(access).find()', ctx).then(x => assert.equal(x, null));
await vm.runInContext('new Updater(access).answering()', ctx).then(x => assert.equal(x.length, 0));

// Real shipped handshake and port-selection code, fake USB endpoints only. Never request real MIDI.
const sent = [];
const input = {id: 'in', name: 'FM-1', state: 'connected', open: async() => {}, close: async() => {},
  addEventListener(t, f) {this.listener = f;}, removeEventListener() {this.listener = null;}};
const output = {id: 'out', name: 'FM-1', state: 'connected', open: async() => {}, close: async() => {},
  send(bytes) {
    sent.push(Array.from(bytes));
    assert([vm.runInContext('HS_QUERY', ctx), vm.runInContext('HS_FULL', ctx)].some(q => Array.from(q).join() === Array.from(bytes).join()), 'Detection must only send identity queries');
    const raw = [0, 0x59, 0x11, 0, 0, 0, ...Buffer.from('FM-1_015'), ...new Array(20).fill(0)];
    ctx.replyRaw = raw;
    const data = vm.runInContext('Uint8Array.from([0xF0, ...pack7(replyRaw), 0xF7])', ctx);
    setTimeout(() => input.onmidimessage?.({data}), 0);
  }};
access.inputs.set(input.id, input); access.outputs.set(output.id, output);
const found = await vm.runInContext('new Updater(access).find()', ctx);
assert.equal(found.id.text, 'FM-1_015'); found.link.close();
assert(sent.length > 0);

const meta = JSON.parse(script.match(/const meta = (\{[^\n]+\});/)[1]);
assert(Buffer.from(meta.data, 'base64').equals(firmware), 'Embedded firmware must remain byte-identical');
assert.equal(meta.sha256, createHash('sha256').update(firmware).digest('hex'));
assert.equal(meta.product, 'FM-1_710010500');

// Exercise the actual page's permission-denied and retry path, using a fake DOM and MIDI access.
const elements = new Map();
const element = id => {
  if (!elements.has(id)) elements.set(id, {textContent:'', value:0, checked:false, disabled:false, hidden:false,
    addEventListener(){}, setAttribute(){}, replaceChildren(){}, append(){}, showModal(){}, close(){}, style:{}});
  return elements.get(id);
};
const page = vm.createContext({console, Uint8Array, setTimeout, clearTimeout, performance, crypto:webcrypto, atob,
  document:{getElementById:element,createElement:tag=>element(Symbol(tag)),querySelectorAll:()=>[],documentElement:{setAttribute(){},dataset:{}}},
  navigator:{}, localStorage:{getItem:()=>null,setItem(){}}, window:{addEventListener(){}}, location:{protocol:'https:'},
  matchMedia:()=>({matches:false,addEventListener(){}})});
vm.runInContext(script, page);
vm.runInContext('confirmed=async()=>true; image=new Uint8Array([1]); navigator.requestMIDIAccess=async()=>{throw Error("denied")}', page);
await vm.runInContext('run(image,meta.product,false)', page);
assert.equal(vm.runInContext('preparing', page), false, 'Permission denial must allow a retry');
assert.equal(vm.runInContext('writing', page), false);
vm.runInContext('navigator.requestMIDIAccess=async()=>({inputs:new Map(),outputs:new Map()}); Updater.prototype.find=async()=>null; Updater.prototype.install=async()=>{throw fail("notfound")}', page);
await vm.runInContext('run(image,meta.product,false)', page);
assert.equal(vm.runInContext('lastKey', page), 'notfound');
assert.equal(vm.runInContext('writing', page), false);
assert.equal(vm.runInContext('preparing', page), false);
console.log('PASS: shipped HTML constructs updater, discovers simulated stock V15, excludes Bluetooth/unrelated ports, handles no device and permission retry, preserves exact 1.5 firmware SHA-256. No hardware accessed or flashed.');
