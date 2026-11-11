// Isolated state/event regression, without opening or rendering a browser page.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const root=new URL('../',import.meta.url);
const source=fs.readFileSync(new URL('web/index.html',root),'utf8');
class Element extends EventTarget {
 constructor(){super();this.value='500';this.textContent='';this.classList={toggle(){}};this.style={setProperty(k,v){this[k]=v;}};}
}
const ranges=Array.from({length:4},()=>new Element());
const elements=new Map([['macros',{children:ranges.map(r=>({querySelector:()=>r}))}]]);
const $=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
const instrument=new Element();
const document={activeElement:ranges[0],querySelectorAll:()=>[],querySelector:s=>s==='.instrument'?instrument:null,body:new Element()};
const window=new EventTarget();
const initial={models:[7,6,5,4,3,7],track:0,page:0};
const context=vm.createContext({CATALOG:JSON.parse(fs.readFileSync(new URL("presets/catalog.json",root),"utf8")),window,document,$,values:initial,active:0,page:0,playing:0,
 hardwareDevice:{},knobText:(_p,_k,v)=>String(v),noteNames:[],updateBiomeLabel(){},showChangeGuard(){},
 setPage(){throw Error('Unexpected page change');},trackUI(){},macroUI(){}});
const helper=new URL('web/range-controls.js',root);
if(fs.existsSync(helper))vm.runInContext(fs.readFileSync(helper,'utf8'),context);
vm.runInContext(source.slice(source.indexOf('const controlDefs='),source.indexOf('function macroUI(')),context);
vm.runInContext(source.slice(source.indexOf("let fieldKey=''"),source.indexOf('function setPage(')),context);
vm.runInContext(source.slice(source.indexOf('function receiveEngine(m)'),source.indexOf("$('power').onclick")),context);
const send=(raw=730)=>{
 context.state={...initial,type:'state',playing:0,octave:0,tempo:76,motion:1,biome:0,
  muted:[],tones:[],raw:[raw,400,500,650],knobs:[raw/10,40,50,65]};
 vm.runInContext('receiveEngine(state)',context);
};
// Exact reported failure: Density retains keyboard focus after a mouse drag;
// subsequent physical knob changes must update BOTH its number and position.
send();assert.equal($('macro-value-0').textContent,'73');
assert.equal(Number(ranges[0].value),730,'Focused but idle Density must follow incoming hardware state');
assert.equal(document.activeElement,ranges[0],'Keep keyboard focus for accessibility');
assert.equal(fs.existsSync(helper),true);
context.ranges=ranges;vm.runInContext('ranges.forEach(bindRange);bindRange($("tempo"));',context);
const emit=(target,type,props={})=>{const e=new Event(type);Object.assign(e,props);target.dispatchEvent(e);};
// Native pointer dragging is protected, even if focus is elsewhere.
document.activeElement=null;ranges[0].value='810';emit(ranges[0],'pointerdown',{pointerId:1});
send(200);assert.equal(Number(ranges[0].value),810);
emit(window,'pointerup',{pointerId:1});document.activeElement=ranges[0];send(280);
assert.equal(Number(ranges[0].value),280);
for(const event of ['pointercancel','lostpointercapture']){
 ranges[0].value='820';emit(ranges[0],'pointerdown',{pointerId:2});send(300);
 assert.equal(Number(ranges[0].value),820);
 emit(event==='pointercancel'?window:ranges[0],event,{pointerId:2});send(320);
 assert.equal(Number(ranges[0].value),320);
}
// Multiple pointers and multiple held adjustment keys end independently.
emit(ranges[0],'pointerdown',{pointerId:3});emit(ranges[0],'pointerdown',{pointerId:4});
emit(window,'pointerup',{pointerId:3});send(400);assert.equal(Number(ranges[0].value),320);
emit(window,'pointerup',{pointerId:4});send(410);assert.equal(Number(ranges[0].value),410);
for(const key of ['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End','PageUp','PageDown']){
 ranges[0].value='600';emit(ranges[0],'keydown',{key});send(420);
 assert.equal(Number(ranges[0].value),600);
 emit(window,'keyup',{key});send(430);assert.equal(Number(ranges[0].value),430);
}
emit(ranges[0],'keydown',{key:'ArrowRight'});emit(ranges[0],'keydown',{key:'End'});
emit(window,'keyup',{key:'ArrowRight'});send(440);assert.equal(Number(ranges[0].value),430);
emit(window,'keyup',{key:'End'});send(450);assert.equal(Number(ranges[0].value),450);
emit(ranges[0],'keydown',{key:'Tab'});send(460);assert.equal(Number(ranges[0].value),460);
// Lost focus/window must never leave a stale interaction lock.
emit(ranges[0],'pointerdown',{pointerId:5});emit(ranges[0],'blur');send(470);
assert.equal(Number(ranges[0].value),470);
emit(ranges[0],'keydown',{key:'ArrowRight'});emit(window,'blur');send(480);
assert.equal(Number(ranges[0].value),480);
// Every macro, including negative-valued controls, shares the fix.
for(let i=0;i<4;i++){
 document.activeElement=ranges[i];context.state.raw[i]=i===2?-12:500+i;
 vm.runInContext('receiveEngine(state)',context);
 assert.equal(Number(ranges[i].value),context.state.raw[i]);
}
// Tempo should also avoid stale incoming values during a pointer gesture.
const tempo=$('tempo');tempo.value='100';emit(tempo,'pointerdown',{pointerId:6});send();
assert.equal(Number(tempo.value),100);emit(window,'pointerup',{pointerId:6});send();
assert.equal(Number(tempo.value),76);
// Verify the actual markup creation path binds each rebuilt macro control.
assert(source.includes('bindRange(r);r.disabled='));
if(process.argv[2]){
 const packaged=fs.readFileSync(process.argv[2],'utf8');
 assert(packaged.includes(fs.readFileSync(helper,'utf8')),'Packaged helper matches tested source');
 assert(packaged.includes(source.slice(source.indexOf('function receiveEngine(m)'),source.indexOf("$('power').onclick"))),'Packaged state handler matches tested source');
 assert(!packaged.includes('__RANGE_CONTROLS__'));
}
console.log('PASS incoming Density position and number update while focus stays; all macros, pointer/multitouch/cancel/capture, adjustment keys, focus loss and tempo gesture protection (isolated JS, not rendered GUI).');
// All 30 page ranges must generate finite CSS, including negative note/pitch
// ranges and controls which do not represent a numeric parameter.
context.instrument=instrument;
vm.runInContext(`for(let p=0;p<controlDefs.length;p++){
 page=p;values.raw=controlDefs[p].map(d=>d[1]);updateLivingField();const low=instrument.style.backgroundSize;
 values.raw=controlDefs[p].map(d=>d[2]);updateLivingField();
 if(low===instrument.style.backgroundSize)throw Error('Unresponsive field '+p);
 if(Object.values(instrument.style).some(v=>/NaN|Infinity/.test(v)))throw Error('Invalid field CSS '+p);
}`,context);
console.log('PASS living browser field responds to control ranges on all 40 pages (isolated style-state test).');

// A new factory world must remain selectable inside Journey, and both new
// melodic models must expose the envelope labels instead of a generic percent.
assert.equal(vm.runInContext("controlDefs[31][2][2]",context),JSON.parse(fs.readFileSync(new URL('presets/catalog.json',root),'utf8')).biomes.length-1);
vm.runInContext(source.slice(source.indexOf("function knobText("),source.indexOf("const controlDefs=")),context);
for(const model of [15,16,18,19]){context.values.models[0]=model;assert.equal(vm.runInContext("knobText(6,0,250)",context),250);assert.equal(vm.runInContext("knobText(6,1,500)",context),"500 ms");}
console.log("PASS final Atlas world reachable from Journey; Cedar/Reed envelope labels retain milliseconds.");
