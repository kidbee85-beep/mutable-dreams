#!/usr/bin/env python3
"""Build an offline-capable browser installer from the credited fm1-base implementation."""
from pathlib import Path
import base64,hashlib,json,re,sys,zipfile
raise SystemExit('Development build: installer packaging disabled until hardware qualification. Use the published 1.0 installer for release 1.0.')
R=Path(__file__).resolve().parents[1]
pkg,out=map(Path,sys.argv[1:3]);raw=pkg.read_bytes()
product=''.join(chr((raw[i*48+47]-i-1)&255) for i in range(20) if raw[i*48+47]!=0x7d)
assert product=='FM-1_710010000',product
s=(R/'base/web/installer.html').read_text()
values={'NAME':'Mutable Dreams','REPO':'Mutable-Dreams-Source.zip','REPO_SHORT':'Download the complete source','LICENSE':'MIT application / MPL-2.0 platform','CREDITS':' With gratitude to Émilie Gillet and Mutable Instruments. An independent community project.'}
for k,v in values.items():s=s.replace('{{'+k+'}}',v)
def strip(x):return re.sub(r'^import .*?;\n','',re.sub(r'^export\s+','',x,flags=re.M),flags=re.M)
lib='\n'.join(strip((R/'base/web'/n).read_text()) for n in ['fm1pkg.js','fm1ota.js'])
meta={'version':'1.0','product':product,'sha256':hashlib.sha256(raw).hexdigest(),'data':base64.b64encode(raw).decode()}
s=s.replace('/*LIB*/',lib).replace('/*META*/',json.dumps(meta))
s=s.replace('const r = await fetch(meta.pkg);\n    if (!r.ok) throw new Error(`${meta.pkg}: HTTP ${r.status}`);\n    const pkg = new Uint8Array(await r.arrayBuffer());', '''const pkg = Uint8Array.from(atob(meta.data), c => c.charCodeAt(0));
    const digest = [...new Uint8Array(await crypto.subtle.digest('SHA-256', pkg))].map(b=>b.toString(16).padStart(2,'0')).join('');
    if(digest !== meta.sha256) { sayK('badpkg'); return; }''')
s=s.replace('<h1>Install Mutable Dreams</h1>','<span class="eyebrow">MUTABLE DREAMS / RELEASE 1.0</span><h1>A new world,<br>one small instrument.</h1><p class="small">Install or update Mutable Dreams. Return to the original M-VAVE firmware whenever you choose.</p><p class="small">Start from the original FM-1 V15 firmware. Save a separate backup of your own work before changing firmware. <a href="Mutable-Dreams-Recovery-Guide.txt">Read the complete guide</a>.</p>')
s=s.replace('href="../"','href="Mutable-Dreams.html"')
s=s.replace('  <button type="button" class="lang"','  <button hidden type="button" class="lang"')
s=s.replace('Install · Mutable Dreams','Mutable Dreams 1.0 · Install & return')
s=s.replace('    const raw = new Uint8Array(await f.arrayBuffer());','    if(f.size>2000000) { sayK("stockbad"); return; }\n    const raw = new Uint8Array(await f.arrayBuffer());')
s=s.replace('async function run(img, product, isStock) {\n  let midi;', '''async function run(img, product, isStock) {
  if(writing || !img)return;
  writing=true;
  $("go").disabled=$("stockgo").disabled=$("reportgo").disabled=true;
  let midi;''')
s=s.replace('catch (e) { sayK("denied"); return; }','catch (e) { writing=false; $("go").disabled=!image; $("stockgo").disabled=!stock; $("reportgo").disabled=false; sayK("denied"); return; }')
s=s.replace('sayK((await up.resume(img, step)) ? (isStock ? "stockdone" : "done") : "stopped");', '''if(!(await up.resume(img, step)))throw fail('stopped','The write did not finish.');
      sayK('reboot');
      await sleep(3000);
      const back=await up.waitFor(id=>!IS_OTA(id),40000);
      if(!back)throw fail('noreturn','The FM-1 did not restart.');
      back.link.close();
      if(back.id.text!==product)throw fail('mismatch','Unexpected firmware after restart.',back.id.text);
      $('bar').value=100;sayK(isStock?'stockdone':'done');''')
s=s.replace('if (isStock && e && e.code === "mismatch") { $("bar").value = 100; sayK("stockdone"); }   /* stock names itself its own way */\n    else if', 'if')
s=s.replace('  writing = false;\n  $("go").disabled', '  writing = false;\n  $("reportgo").disabled=false;\n  $("go").disabled')
s=s.replace('</style>', '''
:root,:root:not([data-theme="light"]){--bg:#f2eee5;--fg:#354e40;--dim:#627469;--accent:#354e40;--on-accent:#f2eee5;--bad:#a34f40}main,footer{max-width:680px}main{padding-top:64px}h1{font:italic 56px/1.08 Georgia,serif;letter-spacing:-.04em;margin:22px 0 30px}h2{font:italic 30px Georgia,serif;border-top:1px solid #cad2c4;padding-top:30px}.eyebrow{font-size:11px;letter-spacing:.19em;color:#a45e58}#lang{display:none}button{padding:12px 24px}#go{margin-top:12px}a{overflow-wrap:anywhere}pre{max-height:200px;overflow:auto}progress{height:12px}footer{border-top:1px solid #cad2c4;margin-top:30px}@media(max-width:600px){h1{font-size:42px}}
</style>''')
assert not re.search(r'\{\{[A-Z_]+\}\}',s)
out.write_text(s)
kit=out.parent/'Mutable-Dreams-Installer-Kit.zip'
with zipfile.ZipFile(kit,'w',zipfile.ZIP_DEFLATED) as z:
 for n in ['tools/fm1_install.py','LICENSE','CREDITS.md','LICENSING.md']:z.write(R/'base'/n,'Mutable-Dreams-Installer/'+Path(n).name)
 z.writestr('Mutable-Dreams-Installer/requirements.txt','mido>=1.3,<2\npython-rtmidi>=1.5,<2\n')
 z.writestr('Mutable-Dreams-Installer/Read-Me.txt','MUTABLE DREAMS 1.0 — USB INSTALLER\n\nPython 3.11 or later. Install dependencies with:\n  python3 -m pip install -r requirements.txt\n\nPlace the release firmware next to fm1_install.py, then:\n  python3 fm1_install.py --info\n  python3 fm1_install.py Mutable-Dreams.fwsc --yes\n\nExpected identity: FM-1_710010000. Read the Recovery Guide first.\nKeep USB connected until the identity is confirmed. Do not use --force.\n\nFor stock recovery, download the official V15 .fwsc and use it instead.\nThis kit contains no firmware, private settings or manufacturer images.\n')
print(out,product,meta['sha256'])
