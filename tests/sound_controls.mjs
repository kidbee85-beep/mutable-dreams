// Exercise the shipped browser event handler: Shape must never write Decay.
// This is isolated event routing, not a substitute for rendered GUI testing.
import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';
const source=fs.readFileSync(new URL('../web/index.html',import.meta.url),'utf8');
const root={children:[],querySelectorAll(){return[]},append(e){this.children.push(e)}};
const document={querySelector(){return null},createElement(){const r={value:'500',dataset:{}};return {style:{setProperty(){}},html:'',set innerHTML(s){this.html=s},querySelector(){return r}}}};
const calls=[];const context=vm.createContext({document,$:()=>root,rangeGestures:{delete(){}},bindRange(){},values:{models:[0],raw:[500,500,500,500],knobs:[50,50,50,50]},page:1,active:0,hardwareDevice:null,pagePalette:()=>['a','b','c','d'],knobText:()=>0,cmd:(...x)=>calls.push(x),post(){},CATALOG:{biomes:Array(213)}});
for(const marker of ['const shapedModels=','const defs=','const controlDefs=']){const at=source.indexOf(marker);assert(at>=0);vm.runInContext(source.slice(at,source.indexOf(';\n',at)+1),context)}
vm.runInContext(source.slice(source.indexOf('function macroUI(){'),source.indexOf('for(const el of document.querySelectorAll',source.indexOf('function macroUI(){'))),context);
for(let model=0;model<20;model++){
 context.values.models=[model];root.children=[];calls.length=0;vm.runInContext('macroUI()',context);
 const r=root.children[2].querySelector('input');r.value='733';r.oninput();
 const shape=[3,4,6,7,8,9,10,11,12,13,14,15,16,18,19].includes(model);assert.equal(calls.at(-1)[0],shape?40:3,`Model ${model}: command`);
 assert(root.children[2].html.includes(model===9?'EXCITER':model===18?'PRESSURE':model===19?'BREATH':shape?'SHAPE':'DECAY'),`Model ${model}: label`);
}
console.log('PASS all 20 model event routes and labels: Cedar/Reed Shape writes command 40, drums Decay writes 3, Elements remains Exciter.');
