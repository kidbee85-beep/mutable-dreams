#!/usr/bin/env python3
"""Original, deterministic drum/textural one-shots. No third-party sample audio.
MIT OR GPL-3.0-only. 16 kHz PCM is an intentional Lo-Fi source, mixed at 44.1 kHz.
"""
from pathlib import Path
import math,random,json
R=Path(__file__).resolve().parents[1];rate=16000
lengths=[.22,.13,.055,.10,.035,.12,.10,.24];arrays=[];meta=[]
for bank in range(5):
 for pad,sec in enumerate(lengths):
  rng=random.Random(0x13d057+bank*37+pad);N=round(sec*rate);samples=[];phase=0;lp=0
  for i in range(N):
   t=i/rate;noise=rng.uniform(-1,1);lp+=(noise-lp)*(.15+.10*bank)
   if pad==0:
    f=43+bank*8+100*math.exp(-t*80);phase+=2*math.pi*f/rate;x=(math.sin(phase)+.06*lp*math.exp(-t*90))*math.exp(-t*(18+bank*3))
   elif pad==1:x=(lp*.8+math.sin(t*2*math.pi*(165+bank*40))*.2)*math.exp(-t*(30+bank*8))
   elif pad==2:x=(noise-lp)*math.exp(-t*(80-bank*12))*.5
   elif pad==3:x=(math.sin(t*2*math.pi*(390+bank*90))+.27*math.sin(t*2*math.pi*(610+bank*200)))*math.exp(-t*(60-bank*6))
   elif pad==4:x=(lp*.5+math.sin(t*2*math.pi*(720+bank*210))*.3)*math.exp(-t*160)
   elif pad==5:x=lp*math.sin(math.pi*i/N)**2*(.9+.1*math.sin(t*81))
   elif pad==6:x=(lp*.12+(noise*.8 if rng.random()<.012+bank*.006 else 0))*math.sin(math.pi*i/N)**2
   else:x=(lp*.7+.13*math.sin(t*2*math.pi*(135+bank*47)))*math.sin(math.pi*i/N)**2
   # Found Objects: damped wooden/ceramic resonances and papery transients.
   if bank==3:
    if pad==0:x=(math.sin(t*2*math.pi*62)+.25*math.sin(t*2*math.pi*118))*math.exp(-t*25)
    elif pad==4:x=(.7*math.sin(t*2*math.pi*310)+.3*lp)*math.exp(-t*100)
    elif pad in [1,2]:x=(noise-lp)*(.6+.4*math.sin(t*2*math.pi*(39+pad*11)))*math.exp(-t*(35+pad*22))
    elif pad==3:x=(math.sin(t*2*math.pi*290)+.37*math.sin(t*2*math.pi*431)+.18*math.sin(t*2*math.pi*687))*math.exp(-t*48)
    elif pad==7:x=(math.sin(t*2*math.pi*196)+.3*math.sin(t*2*math.pi*293.66)+lp*.18)*math.sin(math.pi*i/N)**2
   # Velour Tape: softened attacks, warm pitched fragments and fluttering air.
   if bank==4:
    if pad==0:x=math.sin(t*2*math.pi*(48+45*math.exp(-t*60)))*math.exp(-t*18)
    elif pad==1:x=(lp*.8+.15*math.sin(t*2*math.pi*180))*math.exp(-t*26)
    elif pad==2:x=lp*math.exp(-t*60)
    elif pad in [3,4]:x=(math.sin(t*2*math.pi*(220 if pad==3 else 330))+.25*math.sin(t*2*math.pi*(440 if pad==3 else 495)))*math.exp(-t*(30 if pad==3 else 85))
    elif pad==7:x=(math.sin(t*2*math.pi*130.81)+.45*math.sin(t*2*math.pi*196)+.3*math.sin(t*2*math.pi*261.63))*math.sin(math.pi*i/N)**2
   samples.append(x)
  mean=sum(samples)/N;samples=[(v-mean)*min(1,i/16,(N-1-i)/64) for i,v in enumerate(samples)]
  peak=max(map(abs,samples));gain=[.82,.70,.55,.65,.5,.42,.50,.48][pad]/max(peak,1e-6)
  pcm=[round(v*gain*32767) for v in samples];arrays.append(pcm);meta.append(dict(bank=bank,pad=pad,frames=N,rate=rate))
lines=['/* Generated original PCM: tools/generate_lofi.py. MIT OR GPL-3.0-only. */']
for i,pcm in enumerate(arrays):lines+=['static const int16_t kit_pcm_'+str(i)+'[]={',','.join(map(str,pcm)),'};']
lines+=['struct KitSample {const int16_t *data;unsigned size;};','static const KitSample kit_samples[40]={'+','.join('{kit_pcm_'+str(i)+','+str(len(v))+'}' for i,v in enumerate(arrays))+'};']
(R/'app/dsp/lofi_samples.inc').write_text('\n'.join(lines)+'\n')
(R/'presets/lofi-samples.json').write_text(json.dumps({'source':'Original synthesized one-shots; no external samples','pcm_bytes':sum(len(v)*2 for v in arrays),'samples':meta},indent=2)+'\n')
print('PCM bytes',sum(len(v)*2 for v in arrays))
