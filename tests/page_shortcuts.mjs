// Real shipped page-button handler: labelled pages are direct shortcuts;
// release after a long pointer/keyboard hold must not navigate.
import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';
const src=fs.readFileSync(new URL('../web/index.html',import.meta.url),'utf8');let now=0;
const events=[],pages=[];const context=vm.createContext({page:6,performance:{now:()=>now},window:{addEventListener(){}},hardware:(...x)=>events.push(x),setPage:(...x)=>pages.push(x)});
vm.runInContext(src.slice(src.indexOf('function bindHold('),src.indexOf('function trackUI(')),context);
class Button extends EventTarget{setPointerCapture(){}}
const b=new Button();context.b=b;vm.runInContext('bindHold(b,4,1)',context);
function emit(type,props={}){const e=new Event(type,{cancelable:true});Object.assign(e,props);b.dispatchEvent(e)}
emit('pointerdown',{pointerId:1});now=100;emit('pointerup');assert.deepEqual(events,[[4,true],[4,false]]);assert.deepEqual(pages,[[1,true]]);
events.length=pages.length=0;now=200;emit('pointerdown',{pointerId:1});now=1200;emit('pointerup');assert.deepEqual(events,[[4,true],[4,false]]);assert.equal(pages.length,0);
events.length=0;now=1300;emit('pointerdown',{pointerId:1});now=1400;emit('pointercancel');assert.deepEqual(pages,[[6,true]]);
pages.length=events.length=0;now=1500;emit('keydown',{key:'Enter',repeat:false});now=2500;emit('keyup',{key:'Enter'});assert.equal(pages.length,0);assert.deepEqual(events,[[4,true],[4,false]]);
events.length=0;now=2600;emit('keydown',{key:' ',repeat:false});now=2650;emit('keyup',{key:' '});assert.deepEqual(pages,[[1,true]]);
console.log('PASS page shortcuts: direct labelled page, native pointer/keyboard hold-help, no short action after long release, pointer cancellation restores its starting page.');
