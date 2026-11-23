#!/usr/bin/env python3
"""Measuring tree only (build with MD_PROFILE): reads the stage profile of the render callback over USB MIDI.
   python3 tools/prof_read.py [--reset] [--every SECONDS] [--json FILE]
   Command 23 (page 0, 1): 6 header words, then two records of 20 words each. A record is the worst callback of a scene
   (one entry per scene, the four worst scenes): times in ticks of 24 a microsecond (divided here), stages measured outside the RAM functions:
     total  the whole callback, ctl everything outside quantum() (commands, MIDI, the loop), tick = tick() (the dream's
     journey_tick -> journey_stage -> journey_enter chain), pre = before the voices (admission guard, smoothing),
     vox = all voices (engines + mix_voice), vox0..5 per track, misc = kit, dream arrive/release, level, fx = render_effects
     (tape / echo / room), fin = finish_stages (WIDTH, CHORUS, AIR, trim).
   Context: scene (Biome ID at the last section entry), q = full quality, fx = finish flags (1 chorus, 2 air, 4 vintage), flags
   (itinera, prepared, fading, runtime ECO), frames of the callback, active-track mask, quanta, step, part, tempo, timer reads.
   A record is one whole app_render (256 frames = the 5804 us block; app.c's two 128-frame DSP calls summed); total ends before the BSP/USB mix.
   Overhead: `overhead_ticks` is the lower bound of one timer read (an estimate, not a full compensation); reads * it is inside `total`.
   --reset clears the peaks (command 3 also resets the CPU peak). Peaks are since the last reset."""
import sys,os,struct,time,json
HERE=os.path.dirname(os.path.abspath(__file__));sys.path[:0]=[HERE,os.path.join(HERE,'..','base','tools')]
def decode(w,page):
 recs=[]
 for r in range(2):
  v=w[6+r*21:27+r*21]
  if not v[0]:continue
  us=lambda x:round(x/24,1)
  recs.append(dict(rank=page*2+r,total_us=us(v[0]),ctl_us=us(v[1]),tick_us=us(v[2]),pre_us=us(v[3]),vox_us=us(v[4]),misc_us=us(v[5]),fx_us=us(v[6]),fin_us=us(v[7]),
   voice_us=[us(x) for x in v[8:14]],scene=v[14]&0xffff,quality_full=(v[14]>>16)&1,finish_flags=(v[14]>>17)&7,
   itinera=(v[14]>>20)&1,prepared=(v[14]>>21)&1,fading=(v[14]>>22)&1,eco=(v[14]>>23)&1,frames=v[15]&511,active_mask=(v[15]>>9)&63,quanta=(v[15]>>15)&255,
   step=v[16],part=v[17],tempo=v[18]/10,timer_reads=v[19],models=[(v[20]>>(5*i))&31 for i in range(6)]))
 return recs
def averages(d,mask=None):
 """Command 24: sets the ablation mask (measuring only: 1 grains, 2 echo, 4 room, 16 width, 32 chorus, 64 air, 128 trim; 0 = the normal sound)
 if mask is given, returns the mean us per callback since the last reset (command 3)."""
 w=struct.unpack('<31I',bytes(d.call(24,[mask&127,mask>>7]) if mask is not None else bytes(d.call(24))))
 n=w[1] or 1;g=lambda i:(w[3+2*i]|w[4+2*i]<<32)/24/n
 return dict(mask=w[0],blocks=w[1],total_us=g(0),ctl_us=g(1),pre_us=g(2),vox_us=g(3),misc_us=g(4),fx_us=g(5),fin_us=g(6),tick_us=g(7),
  voice_us=[(w[19+2*i]|w[20+2*i]<<32)/24/n for i in range(6)])
def read(d):
 out=dict(records=[]);
 for page in (0,1):
  w=struct.unpack('<48I',bytes(d.call(23,[page])))
  if page==0:out.update(blocks=w[1],over90=w[2],overhead_ticks=w[3],ranks=w[4])
  out['records']+=decode(w,page)
 return out
if __name__=='__main__':
 a=sys.argv[1:];every=float(a[a.index('--every')+1]) if '--every' in a else 0;jf=a[a.index('--json')+1] if '--json' in a else None
 from creative_bench import Bench   # needs mido (python-deps of the Codex tree)
 d=Bench('Mutable Dreams FM-1');log=[]
 try:
  if '--reset' in a:d.call(3)
  while True:
   r=read(d);r['t']=time.time();log.append(r);print(json.dumps(r))
   if jf:json.dump(log,open(jf,'w'))
   if not every:break
   time.sleep(every)
 finally:d.close()
