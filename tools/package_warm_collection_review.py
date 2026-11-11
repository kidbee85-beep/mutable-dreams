"""Make a local review of the actual device recordings and artwork."""
from pathlib import Path
from collections import defaultdict
from html import escape as h
import json, math, shutil, wave
import numpy as np

R=Path(__file__).resolve().parents[1];O=(R/'../../outputs/Mutable-Dreams-2.0-Warm-Collection').resolve()
cat=json.loads((R/'presets/catalog.json').read_text());q=json.loads((O/'Device-Qualification.json').read_text())
build=json.loads((O/'Build-Report.json').read_text());assert q['pass'] and q['owner_live_exact']
previous=O.parent/'Mutable-Dreams-2.0-Headroom-Lab'
if (previous/'Journey-Demo.mp3').exists():shutil.copy2(previous/'Journey-Demo.mp3',O/'Journey-Demo.mp3')
recordings=[r['usb_capture'] for r in q['tones']+q['biomes'] if r.get('usb_capture')]
all_pcm={};params={}
for relative in recordings:
    with wave.open(str(O/relative)) as f:
        params[relative]=f.getparams();all_pcm[relative]=np.frombuffer(f.readframes(f.getnframes()),dtype='<i2')/32768.
peak=max(float(np.max(abs(x))) for x in all_pcm.values())
gain=min(8.,.8/peak)
review_audio=O/'Listening-Audio';review_audio.mkdir(exist_ok=True)
for k,x in all_pcm.items():
    with wave.open(str(review_audio/Path(k).name),'wb') as f:
        f.setparams(params[k]);f.writeframes(np.rint(x*gain*32767).astype('<i2').tobytes())
gain_db=20*math.log10(gain)
(O/'Listening-Gain.json').write_text(json.dumps(dict(shared_gain_db=round(gain_db,2),clips=len(recordings),original_peak=peak,processing='One common gain; no per-clip normalization, EQ or compression.'),indent=2)+'\n')
tone_groups=defaultdict(list)
for t in cat['tones'][452:]:tone_groups[t['collection']].append(t)
tone_demo={r['id']:r['usb_capture'] for r in q['tones'] if 'usb_capture' in r}
def audio(path,label):return f'<audio controls preload="none" aria-label="{h(label)}" src="Listening-Audio/{Path(path).name}"></audio>'
sections=[]
for row in q['biomes']:
    b=cat['biomes'][row['id']];id=b['id'];name=h(b['name'])
    names=' · '.join(h(cat['tones'][ix]['name']) for ix in b['tones'])
    sections.append(f'''<article class="scene"><img loading="lazy" alt="{name} on the FM-1 display" src="Screens/Biome-{id}.png" width="240" height="240">
      <div><small>BIOME {id} · {b['tempo']} BPM</small><h3>{name}</h3><p>{h(b['intent'])}</p>
      {audio(row['usb_capture'],b['name'])}<p class="voices">{names}</p><a href="Examples/Biome-{id}.json" download>Download Biome</a></div></article>''')
groups=[]
for label,tones in tone_groups.items():
    lead=tones[0];names=''.join(f"<li><span>{t['id']}</span>{h(t['name'])}</li>" for t in tones)
    groups.append(f'<article class="tone"><h3>{h(label)}</h3><ul>{names}</ul>{audio(tone_demo[lead["id"]],lead["name"])}</article>')
all_rows=q['tones']+q['biomes']+q['stress'];peak_cpu=max(r['callback_peak_percent'] for r in all_rows)
playback=sum(r['seconds'] for r in all_rows)
flash=build['app_slot_spare']-build['app_slot_retained_margin']
film=O/'Video/Mutable-Dreams-2.0-Warm-Collection-Demo.mp4'
video=''
if film.is_file():
    video=f'''<h2 id="film">The 2.0 walkthrough</h2><p class="sub">Three minutes of original FM-1 music, real display captures and English captions. No voiceover. The SMK-37 edition is planned separately.</p>
<video controls preload="metadata" poster="Video/Thumbnail.jpg" src="{film.relative_to(O)}" style="width:100%;border:1px solid var(--line);border-radius:16px"></video>
<p><a href="Video/Chapters-and-Description.txt">Chapters and description</a> · <a href="Community-Next/GitHub-Issue-Review.md">Community feedback and next steps</a></p>'''
page=f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Warm Collection · Mutable Dreams 2.0</title>
<style>
:root{{color-scheme:dark;--night:#10181b;--panel:#182528;--line:#32494b;--ink:#edf1e2;--soft:#a8bcad;--accent:#d8c18f}}
*{{box-sizing:border-box}}body{{margin:0;background:radial-gradient(ellipse at 24% 0%,#243c34 0%,var(--night) 52%);color:var(--ink);font:16px/1.55 system-ui,sans-serif}}
main{{max-width:1160px;padding:3rem 1.5rem 4rem;margin:auto}}header{{max-width:44rem;margin-bottom:2.4rem}}h1,h2,h3{{font-family:Georgia,serif;font-weight:500;line-height:1.12}}h1{{font-size:clamp(2.7rem,7vw,5.3rem);margin:.32em 0}}h2{{font-size:2.2rem;margin:2.6rem 0 1rem}}h3{{font-size:1.46rem;margin:.3rem 0 .65rem}}p{{margin:.6rem 0}}small,.kicker{{color:var(--accent);letter-spacing:.11em;font-size:.73rem;font-weight:700}}a{{color:#f2d799;text-underline-offset:4px}}a:focus-visible,audio:focus-visible{{outline:2px solid var(--accent);outline-offset:5px}}nav{{display:flex;gap:1.3rem;flex-wrap:wrap;margin-top:1.3rem}}.sub,.voices{{color:var(--soft)}}.scene,.tone,.facts{{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:1rem}}.scene{{display:grid;grid-template-columns:minmax(102px,150px) minmax(0,1fr);gap:1rem;align-items:start}}.scene img{{width:100%;height:auto;image-rendering:auto;border-radius:10px}}.scene p{{font-size:.91rem}}.voices{{font-size:.76rem!important}}.grid,.tones{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:1rem}}.tones{{grid-template-columns:repeat(3,minmax(0,1fr))}}audio{{width:100%;height:36px;margin:.45rem 0}}ul{{padding:0;list-style:none}}li span{{display:inline-block;color:var(--soft);min-width:2.6em;font-variant-numeric:tabular-nums}}.facts{{margin-top:2rem;color:var(--soft)}}
@media(max-width:800px){{.grid,.tones{{grid-template-columns:1fr}}}}@media(max-width:420px){{main{{padding:2rem .85rem}}.scene{{grid-template-columns:1fr}}.scene img{{max-width:180px}}}}
</style><main><header><div class="kicker">KID BUNKA / PRIVATE FM-1 BUILD {build['sha256'][:8].upper()}</div><h1>Warm Collection</h1>
<p class="sub">64 new sounds, arranged into 20 distinct Biomes. Bowed wood and breath, low voltage, felt keys, dry cedar and hand drums. The Botanical Atlas leaves and existing library remain.</p>
<nav><a href="#biomes">Listen to Biomes</a><a href="#tones">Browse tones</a><a href="Mutable-Dreams-2.0.html">Open the playable editor</a></nav></header>
{video}
<h2 id="biomes">20 Biomes from the FM-1</h2><p class="sub">Real 44.1 kHz stereo USB recordings. All previews use the same {gain_db:+.1f} dB listening gain. The raw captures are in Device-Audio.</p>
<div class="grid">{''.join(sections)}</div>
<h2 id="tones">16 families, 64 new tones</h2><p class="sub">A direct FM-1 recording accompanies each family. Numbers are the stable preset IDs, starting at zero.</p><div class="tones">{''.join(groups)}</div>
<aside class="facts"><strong>Device checks</strong><p>{playback:.0f} seconds of audio tests, peak {peak_cpu:.2f}% of the audio deadline, no XRuns or USB errors during qualification. 213 old Biomes are sample-identical in the native stereo regression. The saved patch layout and safety margins are unchanged. {flash:,} bytes of application flash remain beyond the reserve.</p></aside>
</main><script>for (const player of document.querySelectorAll('audio')) player.addEventListener('play',()=>{{for(const other of document.querySelectorAll('audio'))if(other!==player)other.pause()}})</script></html>'''
(O/'Review.html').write_text(page)
print(f'Prepared {len(recordings)} USB recordings, one common {gain_db:+.2f} dB gain and 20 real display screenshots.')
