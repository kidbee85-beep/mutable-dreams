#!/usr/bin/env python3
"""Package the existing fm1-base viewer for the private Mutable Dreams 2.0 build."""
from pathlib import Path
import re,sys,shutil
R=Path(__file__).resolve().parents[1]
O=Path(sys.argv[1]).resolve();O.mkdir(parents=True,exist_ok=True)
s=(R/'base/web/mirror/index.html').read_text()
s=s.replace('{{NAME}}','Mutable Dreams')
s=s.replace('{{PORTS_JS}}',re.sub(r'^export\s+','',(R/'base/web/fm1ports.js').read_text(),flags=re.M))
s=s.replace('{{PANEL_JS}}',re.sub(r'^export\s+','',(R/'base/web/fm1panel.js').read_text(),flags=re.M))
s=re.sub(r'<link[^>]+(?:fonts\.googleapis|fonts\.gstatic)[^>]*>\n?','',s)
s=s.replace('<title>Mirror · Mutable Dreams</title>','<title>Mutable Dreams 2.0 · USB Mirror</title>')
s=s.replace(' fps · ', ' updates/s · ')
s=s.replace('</style>', '''
:root[data-theme="dark"],:root{--ground:#071013;--ink:#e5efdf;--muted:#a5b7b2;--rule:#284249;--accent:#9acaab;--accent-ink:#b2ddc4;--teal:#94cdd1;--card:#102126;color-scheme:dark}
body{font-family:system-ui,-apple-system,Arial,sans-serif}
body.capture{padding:0;background:#000;overflow:hidden}
.capture header,.capture .bar,.capture .needs,.capture .help,.capture .device{display:none}
.capture main{max-width:none}.capture .stage{height:100vh;align-items:center}
.capture .big canvas{width:min(100vh,100vw);height:min(100vh,100vw);max-width:none;margin:0;border-radius:0;box-shadow:none}
</style>''')
s=s.replace('<button id="connect" class="primary" type="button">Connect</button>','''<button id="connect" class="primary" type="button">Connect</button>
  <button id="disconnect" type="button">Disconnect</button>
  <button id="capture" type="button">Clean capture</button>''')
s=s.replace('See the FM-1\'s screen and panel live on your computer.','Private 2.0 · real display and panel over USB · no firmware installer.')
s=s.replace('<section class="help">','''<p class="needs">For OBS: choose Clean capture, capture this Chrome/Edge window, and record FM-1 USB audio separately at 44.1 kHz. Escape leaves clean view. Only the unique “Mutable Dreams FM-1” USB port is accepted; other instruments are never probed.</p>
<section class="help">''')
a=s.index('function pickPorts() {');b=s.index('// no port to take',a)
s=s[:a]+'''function pickPorts() {
  const valid=p=>p.state!=="disconnected" && p.name==="Mutable Dreams FM-1";
  const ins=[...access.inputs.values()].filter(valid),outs=[...access.outputs.values()].filter(valid);
  const one=ins.length===1&&outs.length===1;
  fill(inSel,ins,one&&ins[0]);fill(outSel,outs,one&&outs[0]);
  if(!one){say(ins.length>1||outs.length>1?"Two identical Mutable Dreams ports: disconnect one before capture.":"Waiting for the unique Mutable Dreams FM-1 USB port. Other instruments are not probed.","wait");return;}
  attach();
}
''' +s[b:]
a=s.index('async function seekPorts() {');b=s.index('function attach()',a)
s=s[:a]+'''// Intentionally no broad probing: Syntherella and other devices stay untouched.
''' +s[b:]
s=s.replace('  if (i !== inPort) {','  if ((i && i.name!=="Mutable Dreams FM-1") || (o && o.name!=="Mutable Dreams FM-1")) return;\n  if (i !== inPort) {',1)
s=s.replace('connectBtn.addEventListener("click", connect);','''connectBtn.addEventListener("click", connect);
document.getElementById("disconnect").addEventListener("click",()=>{
 if(control)setControl(false);send(0);if(inPort)inPort.onmidimessage=null;
 if(access)access.onstatechange=null;inPort=outPort=null;connectBtn.hidden=false;controlBtn.disabled=true;nums.textContent="";
 say("Disconnected. Screen transmission is off.","");
});
document.getElementById("capture").addEventListener("click",()=>document.body.classList.add("capture"));
addEventListener("keydown",e=>{if(e.key==="Escape")document.body.classList.remove("capture");});''')
s=s.replace('if (p.state === "granted") connect();','if (p.state === "granted" && !new URLSearchParams(location.search).has("manual")) connect();')
s=s.replace('sends the screen after the notes and the clock, so the music always comes first.','sends screen rectangles at lower priority than MIDI notes, clock, backup and updater messages.')
assert '{{NAME}}' not in s and '{{PORTS_JS}}' not in s and '{{PANEL_JS}}' not in s
(O/'index.html').write_text(s)
shutil.copy2(R/'base/LICENSE',O/'MPL-2.0.txt')
print(O/'index.html')
