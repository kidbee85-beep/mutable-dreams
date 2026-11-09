"""Prepare the private browser edition from the working 1.4 page."""
from pathlib import Path

p=Path(__file__).resolve().parents[1]/'web/index.html'
s=p.read_text()
def once(old,new):
    global s
    if s.count(old)!=1: raise RuntimeError(f'Expected one occurrence: {old[:70]!r}, got {s.count(old)}')
    s=s.replace(old,new)

once('<title>Mutable Dreams 1.4 · A wider atlas</title>',
     '<title>Mutable Dreams 1.5 · Private browser preview</title>')
once('<strong>Mutable Dreams 1.4 is here.</strong> Your ideas shaped a wider, more contrasting Biome atlas, five new scales, clearer mute states and more flexible MIDI.',
     '<strong>Mutable Dreams 1.5 is in private testing.</strong> Twelve further Biomes and voices, a brighter living display and Bluetooth MIDI reception are ready to explore.')
once('48 new Biomes · 16 new tones · 5 new scales · independent track MIDI channels',
     '12 further Biomes · 12 further tones · 169 total Biomes · Bluetooth MIDI on the FM-1')
once('VERSION 1.4</div>', 'VERSION 1.5 PREVIEW</div>')
once('Explore 156 musical Biomes, shape 352 sounds, or begin with a blank canvas.',
     'Explore 168 musical Biomes, shape 364 sounds, or begin with a blank canvas.')
once('<a href="#install">Install 1.4 ↓</a>', '<a href="#install">Testing status ↓</a>')
once('<a href="Mutable-Dreams-1.4-Guide.pdf">Illustrated guide ↓</a>', '<a href="Mutable-Dreams-1.5-Guide.pdf">Illustrated 1.5 guide ↓</a>')
once('<a href="Mutable-Dreams-1.3.html">Previous 1.3 browser ↗</a>',
     '<a href="https://mutable-dreams.sugarypixie10.chatgpt.site/">Current public release ↗</a>')
once('<span class="atlas-count">108 Biomes + Blank ↗</span>',
     '<span class="atlas-count">168 Biomes + Blank ↗</span>')
once('Hardware editing needs the matching Mutable Dreams 1.3 firmware',
     'Hardware editing needs the matching Mutable Dreams 1.5 test firmware')
once('Five Motion Sketches</h2>', 'Five Motion Sketches</h2>') if False else None
once('A QUIET LOOK AT THE UPDATE', 'THE SOUND OF THE UPDATE')
once('A wider atlas. Clearer control.', 'New worlds. A living signal.')
once('Several contrasting sounds recorded directly from the FM-1, with a calm English introduction to what changed. The screen views are native firmware renders.',
     'Four contrasting sound worlds from the 1.5 native engine, with no narration or third-party music. The display views are captured from the firmware renderer.')
once('Mutable-Dreams-1.4-Update.mp4', 'Mutable-Dreams-1.5-Update.mp4')
once('<track kind="captions" src="Mutable-Dreams-1.4.vtt" srclang="en" label="English">', '')
once('<p><a href="Transcript.txt">Read the transcript</a></p>', '')
once("localStorage.setItem('mutable-dreams-1.4',", "localStorage.setItem('mutable-dreams-1.5',")
once("localStorage.getItem('mutable-dreams-1.4')??", "localStorage.getItem('mutable-dreams-1.5')??localStorage.getItem('mutable-dreams-1.4')??")
once("firmware:'1.4'", "firmware:'1.5'")
once("[17,'GLO 4',null,'MIDI']", "[17,'GLO 4',null,'MIDI'],[30,'GLO 6',null,'Wireless MIDI']")
once('defs.push(defs[28].map(([n,h])=>[n,h.replace(\'Two independent LFOs per synth\',\'Second independent LFO per synth\')]));',
     "defs.push(defs[28].map(([n,h])=>[n,h.replace('Two independent LFOs per synth','Second independent LFO per synth')]));\n"
     "defs.push([['RADIO','FM-1 only: turn Bluetooth MIDI on or off while stopped'],['DEVICE','FM-1 only: turn to search for or choose a BLE MIDI device'],['LINK','FM-1 only: connect to or release that device'],['MIDI THRU','FM-1 only: route incoming MIDI to another port']]);")
once('[[61,0,2],[62,0,1],[125,0,16],[126,0,1]]',
     '[[61,0,3],[62,0,1],[125,0,16],[126,0,1]]')
once('controlDefs.push([[120,0,6],[121,0,3],[122,0,1000],[123,0,1000]]);',
     'controlDefs.push([[120,0,6],[121,0,3],[122,0,1000],[123,0,1000]]);\ncontrolDefs.push([[-3,0,1],[-3,0,7],[-3,0,1],[-3,0,1]]);')
once('4,4,3,3][p]??0]', '4,4,3,3,2][p]??0]')
once('if(p===27)return', "if(p===30)return k===0?(v?'On':'Off'):k===1?'Device '+(v+1):k===2?(v===3?'Connected':v===2?'Connecting':'Turn right'):(v?'On':'Off');if(p===27)return")
once('if(p===17)return k===0?["Internal","USB","TRS"][v]',
     'if(p===17)return k===0?["Internal","USB","TRS","BLE"][v]')
once("r.disabled=c===-2||((page===28||page===29)&&values.kitMode&&active===3);r.oninput=()=>{if(r.disabled)return;else if(c<0)",
     "r.disabled=c===-2||(page===30&&!hardwareDevice)||((page===28||page===29)&&values.kitMode&&active===3);r.dataset.last=r.value;r.oninput=()=>{if(r.disabled)return;if(page===30){const delta=+r.value-(+r.dataset.last);r.dataset.last=r.value;if(delta)post({type:'enc',role:3+i,n:delta});return;}else if(c<0)")
start=s.index('<section id="preview-notes"')
end=s.index('<div class="tiny">',start)
s=s[:start]+'''<section id="preview-notes" class="install-section"><span class="eyebrow">VERSION 1.5 / PRIVATE CANDIDATE</span><h2>Twelve new places to get lost.</h2><p>The atlas now holds 168 musical Biomes plus Blank and 364 tone starting points. Four stronger organic palettes follow the music on the FM-1 display. The original Biome IDs and User Banks remain in place.</p><p>Bluetooth MIDI reception has been verified on one physical FM-1 for notes, CC74 and transport clock. USB and TRS input remain available. A separate BLE-MIDI sender that the FM-1 connects to, external TRS hardware and the newer UC25HQ80 board still need physical verification.</p></section><section id="install" class="install-section"><span class="eyebrow">PRIVATE TEST BUILD</span><h2>Before the public update.</h2><p>Keep the current public release until final browser and device checks are complete. This preview is for listening and editing in the browser. Back up your user banks and live Biome before installing any firmware.</p></section>''' +s[end:]
# A live hardware connection exposes native knob movement on the radio page.
s=s.replace("deviceMessage('Connected to '+d.id+'. Listen through the FM-1. Controls edit its live patch; Save is explicit.');",
            "deviceMessage('Connected to '+d.id+'. Listen through the FM-1. Controls edit its live patch; Save is explicit.');if(page===30)macroUI();")
# The device disconnect helper lives in a separate script and calls macroUI after switching target.
p.write_text(s)
print(p)
