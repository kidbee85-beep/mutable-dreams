"""Stream complete native stereo renders into SHA256 without writing GBs of PCM."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import subprocess,hashlib,json,sys
frames=44100*12;count=177

def digest(executable,label):
 hashes=[]
 with open('/private/tmp/md20-fullstereo-'+label+'.log','w') as log:
  p=subprocess.Popen([executable],stdout=subprocess.PIPE,stderr=log)
  try:
   for biome in range(count):
    remaining=frames*8;h=hashlib.sha256()
    while remaining:
     b=p.stdout.read(min(65536,remaining))
     if not b:raise RuntimeError(f'{label}: early EOF at Biome{biome}')
     h.update(b);remaining-=len(b)
    hashes.append(h.hexdigest())
   assert p.stdout.read(1)==b'';assert p.wait()==0
  finally:
   if p.poll() is None:p.terminate();p.wait()
 return hashes
with ThreadPoolExecutor(2) as e:
 a=e.submit(digest,sys.argv[1],'baseline');b=e.submit(digest,sys.argv[2],'final');old,new=a.result(),b.result()
changed=[i for i in range(count) if old[i]!=new[i]]
out={'sample_rate':44100,'channels':2,'seconds_per_biome':12,'biomes':177,'full_stereo_sample_frames_compared':frames*count,'different_ids':changed,'bit_identical':not changed,'reference':'pre-Journey private2.0 palette checkpoint','sha256_per_biome':new}
Path(sys.argv[3]).write_text(json.dumps(out,indent=2)+'\n');print('Full-stereo SHA256 audit:',count,'Biomes, differences',changed);assert not changed
