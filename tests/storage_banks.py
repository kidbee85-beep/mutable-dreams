"""Real storage algorithm with this app's map and simulated NOR interruption."""
from pathlib import Path
import re,subprocess,tempfile,os
r=Path(__file__).resolve().parents[1]
s=(r/'base/tests/storage_test.c').read_text()
h=(r/'app/app.h').read_text()
s=re.sub(r'enum \{ OBJ_PROJ.*?\n#define APP_STORE_MAP[^\n]+',re.search(r'enum \{ OBJ_PATCH[^\n]+',h)[0]+'\n#include "'+str(r/'app/target_storage.h')+'"\n'+re.search(r'#define APP_STORE_MAP[^\n]+',h)[0],s,flags=re.S)
s=s.replace('OBJ_PROJ','OBJ_PATCH')
s=s.replace('"../include/plat.h"','"'+str(r/'base/include/plat.h')+'"').replace('"../device/storage.c"','"'+str(r/'base/device/storage.c')+'"')
s=s.replace('(a0 >= 0x97000 && a1 <= 0xA0000) || (a0 >= 0xDC000 && a1 <= 0xE0000)', '(a0 >= 0x97000 && a1 <= 0xE0000)').replace('objects in data regions, clear of the sample slots','Mutable Dreams map in data regions; no overlapping objects')
s=s.replace('    printf("%s\\n", bad ?', '''    for(unsigned obj=OBJ_USER;obj<OBJ_NOBJ;obj++){
        static unsigned char x[3464],y[3464],readback[3464];
        memset(x,0x31,sizeof x);memset(y,0x72,sizeof y);
        for(int fail=0;fail<15;fail++){
            fail_after=-1;st_save(obj,x,sizeof x);
            fail_after=fail;st_save(obj,y,sizeof y);fail_after=-1;
            int len=st_load(obj,readback,sizeof readback);
            if(len!=sizeof x || (memcmp(readback,x,sizeof x)&&memcmp(readback,y,sizeof y)))bad++;
        }
    }
    printf("%u bank objects: interrupted writes preserve a committed payload\\n", (unsigned)(OBJ_NOBJ-OBJ_USER));
    printf("%s\\n", bad ?''')
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'test.c';p.write_text(s);out=Path(td)/'test'
 flags=['-fsanitize=address,undefined','-g'];sdk=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')
 if sdk.exists():flags+=['-isysroot',str(sdk)]
 subprocess.run(['clang',*flags,str(p),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
