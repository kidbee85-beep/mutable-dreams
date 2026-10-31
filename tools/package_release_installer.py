#!/usr/bin/env python3
"""Build an offline-capable browser installer from the credited fm1-base implementation."""
from pathlib import Path
import base64,hashlib,json,re,sys,zipfile
R=Path(__file__).resolve().parents[1]
if len(sys.argv)!=4:raise SystemExit('Usage: package_release_installer.py PACKAGE OUTPUT QUALIFICATION.json')
pkg,out=map(Path,sys.argv[1:3]);raw=pkg.read_bytes()
qualification=json.loads(Path(sys.argv[3]).read_text())
assert qualification.get('pass_') and qualification['firmware_sha256']==hashlib.sha256(raw).hexdigest(), 'The exact candidate must pass its device qualification first.'
assert qualification.get('public_release'), 'Public packaging requires release approval.'
product=''.join(chr((raw[i*48+47]-i-1)&255) for i in range(20) if raw[i*48+47]!=0x7d)
assert product=='FM-1_710010400',product
s=(R/'base/web/installer.html').read_text()
values={'NAME':'Mutable Dreams','REPO':'Mutable-Dreams-1.4-Source.zip','REPO_SHORT':'Download the complete source','LICENSE':'GPL-3.0 combined distribution; original MIT and MPL-2.0 notices retained','CREDITS':' With gratitude to Émilie Gillet and Mutable Instruments. An independent community project.'}
for k,v in values.items():s=s.replace('{{'+k+'}}',v)
def strip(x):return re.sub(r'^import .*?;\n','',re.sub(r'^export\s+','',x,flags=re.M),flags=re.M)
lib='\n'.join(strip((R/'base/web'/n).read_text()) for n in ['fm1pkg.js','fm1ota.js'])
meta={'version':'1.4','product':product,'sha256':hashlib.sha256(raw).hexdigest(),'data':base64.b64encode(raw).decode()}
s=s.replace('/*LIB*/',lib).replace('/*META*/',json.dumps(meta))
s=s.replace('const r = await fetch(meta.pkg);\n    if (!r.ok) throw new Error(`${meta.pkg}: HTTP ${r.status}`);\n    const pkg = new Uint8Array(await r.arrayBuffer());', '''const pkg = Uint8Array.from(atob(meta.data), c => c.charCodeAt(0));
    const digest = [...new Uint8Array(await crypto.subtle.digest('SHA-256', pkg))].map(b=>b.toString(16).padStart(2,'0')).join('');
    if(digest !== meta.sha256) { sayK('badpkg'); return; }''')
s=s.replace('<h1>Install Mutable Dreams</h1>','<span class="eyebrow">MUTABLE DREAMS / VERSION 1.4</span><h1>A wider atlas,<br>one small instrument.</h1><p class="small">Connect only the FM-1 you intend to update; disconnect other FM-1 units first. Install or update Mutable Dreams. Return to original M-VAVE V15 if needed.</p><p class="small">Start from original FM-1 V15 or V16, or update from an earlier Mutable Dreams version using its matching backup workflow. On 1.0, stop and SAVE on the device, then follow the legacy backup steps in the guide; its live editor is not available yet. On 1.1 or later, back up saved User Banks as .mddb and export unsaved live edits as JSON before changing firmware. Returning to V15 erases User Banks and preferences. Official M-UPGRADE rejects this custom package: use this installer. <a href="Mutable-Dreams-1.4.html#device-editor">Open the bank backup and editor</a>. <a href="Recovery-1.4.txt">Read the complete guide</a>.</p>')
s=s.replace('href="../"','href="Mutable-Dreams-1.4.html"')
s=s.replace('  <button type="button" class="lang"','  <button hidden type="button" class="lang"')
s=s.replace('Install · Mutable Dreams','Mutable Dreams 1.4 · Install & return')
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
s=s.replace(' (or its safe mode, or another firmware with the same update entry)', '')
s=s.replace('What Mutable Dreams saved may be lost.', 'Returning to V15 erases the User Banks and preferences. Back up .mddb and export any unsaved live patch first.')
assert not re.search(r'\{\{[A-Z_]+\}\}',s)
out.write_text(s)
kit=out.parent/'Mutable-Dreams-1.4-Installer-Kit.zip'
with zipfile.ZipFile(kit,'w',zipfile.ZIP_DEFLATED) as z:
 for n in ['tools/fm1_install.py','LICENSE','CREDITS.md','LICENSING.md']:z.write(R/'base'/n,'Mutable-Dreams-Installer/'+Path(n).name)
 z.writestr('Mutable-Dreams-Installer/requirements.txt','mido>=1.3,<2\npython-rtmidi>=1.5,<2\n')
 z.writestr('Mutable-Dreams-Installer/Read-Me.txt','MUTABLE DREAMS 1.4 — USB INSTALLER\n\nPython 3.11 or later. Install dependencies with:\n  python3 -m pip install -r requirements.txt\n\nPlace the release firmware next to fm1_install.py, then:\n  python3 fm1_install.py --info\n  python3 fm1_install.py Mutable-Dreams-1.4.0.fwsc --yes\n\nExpected identity: FM-1_710010400. Read the Recovery Guide first.\nKeep USB connected until the identity is confirmed. Do not use --force for Mutable Dreams.\n\nFor stock recovery, use the verified official V15 and the browser installer, then reinstall 1.4 and restore .mddb; physical SEL is required.\nThis kit contains no firmware, private settings or manufacturer images.\n')
print(out,product,meta['sha256'])
