"""Exercise real app navigation/status/brightness with the sanitized native host."""
from pathlib import Path
import re,subprocess,sys
R=Path(__file__).resolve().parents[1];O=Path(sys.argv[1]).resolve();O.mkdir(parents=True,exist_ok=True)
body=re.search(r'enum \{([^}]*MD_COMMAND_COUNT[^}]*)\}',(R/'app/dsp/dreams.h').read_text(),re.S).group(1)
commands={};value=-1
for item in body.split(','):
 item=item.strip()
 if not item:continue
 name,sep,v=item.partition('=');value=int(v,0) if sep else value+1;commands[name.strip()]=value
lines=['wait 3400','tap HOME']
def command(name,track,value):lines.append(f'journey {commands[name]} {track} {value}')
for name in ['MD_PLAY','MD_LOCK','MD_FREEZE','MD_RECORD']:command(name,0,0)
for tr in range(6):command('MD_MUTE',tr,0)
# Independently specified navigation families, including every private 2.0 page.
groups={'HOME':[0],'EDIT':[1,4,6,10,12,24,25],'SEQ':[2,7,13,16,19,20,26,27,31,32,33],'GLO':[5,9,11,17,18,21,23,30,34,35],'FX':[3,8,14,15,22,28,29,36,37,38,39]}
bits={'FX':1,'SEL':2,'ENV':4,'LFO':8,'EDIT':16,'GLO':32,'HOME':64,'SAVE':128,'ARP':256,'SEQ':512,'PLAY':16384,'REC':2048,'OCT-':4096,'OCT+':8192}
for name,pages in groups.items():
 for page in pages:lines.extend([f'page {page}','wait 30',f'expect led_buttons {bits[name]}'])
# Actual physical navigation, no direct page helper.
lines.extend(['tap HOME','tap EDIT','expect page 1','expect led_buttons 16','tap FX','expect page 3','expect led_buttons 1','tap SEQ','expect page 2','expect led_buttons 512','tap GLO','expect page 9','expect led_buttons 32','tap HOME','tap SEL','expect led_buttons 66','spin SELECT 1','expect led_buttons 64','spin SELECT -1','expect led_buttons 66','tap SEL','tap LFO','expect led_buttons 72','tap ARP','expect led_buttons 328','tap LFO','tap ARP','tap OCT+','expect led_buttons 8256','tap OCT-','expect led_buttons 64'])
# Long hold must illuminate help/held key without invoking the short action.
lines+=['press EDIT','wait 900','expect help 4','expect led_buttons 80','release EDIT','wait 30','expect page 0','expect led_buttons 80','tap HOME','expect help -1','expect led_buttons 64']
# PLAY has a dedicated green LED. Red is reserved for actual recording.
lines+=['tap PLAY','expect led_buttons 16448','page 19','tap REC','expect record_mode 1','expect led_buttons 3584','tap REC','expect record_mode 2','expect led_buttons 3584','tap REC','expect record_mode 0','expect led_buttons 16896','tap PLAY','expect playing 0','expect led_buttons 512']
# Legacy preference values stay 0/1/2. FLOW replaces only the old soft label.
lines+=['page 11','spin K1 -10','spin K4 -10','wait 100','expect button_glow 0','expect dim_ns 0','expect led_buttons 32','spin K4 1','wait 100','expect dim_ns 1600','spin K4 1','wait 100','expect dim_ns 3200','spin K1 1','wait 3000','expect led_target_ns 1600','expect dim_ns 1600','tap PLAY','wait 700','leds','wait 800','leds','expect led_target_ns >1599','expect led_target_ns <3401','tap PLAY','wait 3500','expect dim_ns 1600']
command('MD_TRIGGER',0,60)
lines+=['wait 40','expect led_note 128','mute 0 1','wait 30','expect led_note 0','mute 0 0','wait 1900','expect led_note 0','key 0 down','wait 40','expect led_keys 1','page 11','spin K4 -2','wait 50','expect led_note 0','expect led_keys 1','key 0 up','wait 50','expect led_keys 0','expect queue_drops 0','expect invalid 0']
script=O/'Lights-Native.txt';script.write_text('\n'.join(lines)+'\n')
r=subprocess.run([str(R/'app/build/san/mutable-dreams-host'),str(script),str(O)],capture_output=True,text=True)
(O/'Lights-Native.log').write_text(r.stdout+r.stderr)
if r.returncode:print((r.stdout+r.stderr)[-6000:]);raise SystemExit(r.returncode)
pulses=[int(n) for n in re.findall(r'dim (\d+) ns',r.stdout)]
assert len(pulses)==2 and pulses[0]!=pulses[1] and all(1600<=n<=3400 for n in pulses),pulses
print('PASS all 40 navigation pages, actual button paths, mute/selection, freeze/lock, octave, long hold, green/red transport, recording, legacy preferences, motion off, gradual PWM and key-tail safety.')
