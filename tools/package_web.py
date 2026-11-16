#!/usr/bin/env python3
from pathlib import Path
import base64,json,sys,shutil
R=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True)
s=(R/'web/index.html').read_text();w=(R/'web/app_worklet.js').read_text()+(R/'web/worklet.js').read_text().replace('import "./app_worklet.js";','')
catalog=(R/'presets/catalog.json').read_text();counts=json.loads(catalog)
s=s.replace('__MUSICAL_BIOMES__',str(len(counts['biomes'])-1)).replace('__TONE_COUNT__',str(len(counts['tones'])))
s=s.replace('__RANGE_CONTROLS__',(R/'web/range-controls.js').read_text())
s=s.replace('__DEVICE_BACKUP__',(R/'web/device-backup.js').read_text()).replace('__DEVICE_EDITOR__',(R/'web/device-editor.js').read_text()).replace('__DEVICE_UI__',(R/'web/device-ui.js').read_text())
s=s.replace('__DEV_PATCHES__',(R/'presets/development-patches.json').read_text()).replace('__CATALOG__',catalog).replace('__WASM__',base64.b64encode((R/'app/build/wasm/dreams.wasm').read_bytes()).decode()).replace('__WORKLET__',json.dumps(w)).replace('__SCREEN__','data:image/png;base64,'+base64.b64encode((R/'assets/live.png').read_bytes()).decode());captions=out.parent/'Mutable-Dreams-Tutorial.vtt'
if captions.exists():s=s.replace('src="Mutable-Dreams-Tutorial.vtt"','src="data:text/vtt;base64,'+base64.b64encode(captions.read_bytes()).decode()+'"')
out.write_text(s);print(out)

# Private2.0 guides share the firmware-rendered screenshots; no network needed.
for guide in (R/'web/guides').glob('*.html') if (R/'web/guides').exists() else []:
 shutil.copy2(guide,out.parent/guide.name)
 if guide.name=="Journey-Guide.html":
  dest=out.parent/guide.name;dest.write_text(dest.read_text().replace("Mutable-Dreams-2.0-Journey.html",out.name))
 if guide.name=="Creative-Guide.html":
  dest=out.parent/guide.name;dest.write_text(dest.read_text().replace("Mutable-Dreams-2.0-Creative.html",out.name))
 if guide.name=="MIDI-Guide.html":
  dest=out.parent/guide.name;dest.write_text(dest.read_text().replace("Mutable-Dreams-2.0-Journey.html",out.name))
if (R/'assets/journey').exists():
 (out.parent/'Screenshots').mkdir(exist_ok=True)
 for shot in (R/'assets/journey').glob('*.png'):shutil.copy2(shot,out.parent/'Screenshots'/shot.name)

if (R/'assets/creative').exists():
 (out.parent/'Screenshots').mkdir(exist_ok=True)
 for shot in (R/'assets/creative').glob('*.png'):shutil.copy2(shot,out.parent/'Screenshots'/shot.name)
