import fs from 'node:fs';import vm from 'node:vm';import assert from 'node:assert/strict';
const html=fs.readFileSync(process.argv[2],'utf8');
assert(!/\{\{(?:NAME|PORTS_JS|PANEL_JS)\}\}/.test(html));assert(!/fonts\.googleapis|fonts\.gstatic/.test(html));
for(const m of html.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g))new vm.Script(m[1]);
const pick=html.match(/function pickPorts\(\) \{[\s\S]*?\n\}/)[0];
function run(ins,outs){let attached=0,notice='',fills=[];const ctx=vm.createContext({access:{inputs:new Map(ins.map((p,i)=>[i,p])),outputs:new Map(outs.map((p,i)=>[i,p]))},inSel:{},outSel:{},fill:(...a)=>fills.push(a),attach:()=>attached++,say:t=>notice=t});vm.runInContext(pick+';pickPorts()',ctx);return{attached,notice,fills};}
const p=name=>({name,state:'connected',send(){throw Error('No port should be probed');}});
assert.equal(run([p('Syntherella FM-1')],[p('Syntherella FM-1')]).attached,0);
assert.equal(run([p('Mutable Dreams FM-1'),p('Syntherella FM-1')],[p('Mutable Dreams FM-1'),p('Syntherella FM-1')]).attached,1);
assert.equal(run([p('Mutable Dreams FM-1'),p('Mutable Dreams FM-1')],[p('Mutable Dreams FM-1')]).attached,0);
assert(!html.includes('if (!c) seekPorts()'));
assert(html.includes('Screen transmission is off.'));assert(html.includes('document.body.classList.add("capture")'));assert(html.includes('e.key==="Escape"'));
assert(html.includes(' updates/s · '));assert(!html.includes(' fps · '));
console.log('PASS self-contained viewer syntax, no unresolved assets/fonts; exact unique target, unrelated-device exclusion, duplicate refusal, explicit disconnect and clean-capture exit.');
