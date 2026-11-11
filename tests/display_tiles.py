"""Compare the actual glyphs of a tiled system box against a one-tile reference."""
from pathlib import Path
import hashlib,subprocess,tempfile
R=Path(__file__).resolve().parents[1]
base=R/'base/device';gen=R/'app/build/gen'
with tempfile.TemporaryDirectory() as tmp:
 t=Path(tmp);source=(base/'gfx.c').read_text()
 # The device's ELF section annotation is not used by host linkers.
 (t/'md_gfx_portable.h').write_text(source.replace('__attribute__((section(".pool")))',''))
 flags=['clang','-O2','-g','-fsanitize=address,undefined','-I'+str(t),'-I'+str(base),'-I'+str(gen)]
 sdk=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')
 if sdk.exists():flags+=['-isysroot',str(sdk)]
 images=[]
 for rows in (30,64):
  out=t/f'display-{rows}'
  subprocess.run([*flags,f'-DTEST_ROWS={rows}',str(R/'tests/display_tiles.c'),'-o',str(out)],check=True)
  images.append(subprocess.check_output([str(out)]))
 assert images[0]==images[1] and len(images[0])==27*240*90*2
 print('PASS 27 full system-text images match exactly, including tall recovery glyphs.',hashlib.sha256(images[0]).hexdigest())
