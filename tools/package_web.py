#!/usr/bin/env python3
from pathlib import Path
import base64,json,sys
R=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True)
s=(R/'web/index.html').read_text();w=(R/'web/app_worklet.js').read_text()+(R/'web/worklet.js').read_text().replace('import "./app_worklet.js";','')
s=s.replace('__RANGE_CONTROLS__',(R/'web/range-controls.js').read_text())
s=s.replace('__DEVICE_BACKUP__',(R/'web/device-backup.js').read_text()).replace('__DEVICE_EDITOR__',(R/'web/device-editor.js').read_text()).replace('__DEVICE_UI__',(R/'web/device-ui.js').read_text())
s=s.replace('__DEV_PATCHES__',(R/'presets/development-patches.json').read_text()).replace('__CATALOG__',(R/'presets/catalog.json').read_text()).replace('__WASM__',base64.b64encode((R/'app/build/wasm/dreams.wasm').read_bytes()).decode()).replace('__WORKLET__',json.dumps(w)).replace('__SCREEN__','data:image/png;base64,'+base64.b64encode((R/'assets/live.png').read_bytes()).decode());captions=out.parent/'Mutable-Dreams-Tutorial.vtt'
if captions.exists():s=s.replace('src="Mutable-Dreams-Tutorial.vtt"','src="data:text/vtt;base64,'+base64.b64encode(captions.read_bytes()).decode()+'"')
out.write_text(s);print(out)
