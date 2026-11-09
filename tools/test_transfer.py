#!/usr/bin/env python3
"""Run the native editor and actual storage-map tests after build_local.py host --sanitize."""
from pathlib import Path
import os,subprocess,sys
r=Path(__file__).resolve().parents[1];o=r/'app/build/san'
f=['-O1','-g','-DPLAT_HOST','-DTEST','-fsanitize=address,undefined','-fno-omit-frame-pointer']
if sys.platform=='darwin':f+=['-isysroot',os.environ.get('SDKROOT','/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk')]
f +=[f'-I{r/p}' for p in ['app','base/host','base/device','base/include','base/lib','app/build/gen','app/third_party']]
subprocess.run(['clang',*f,'-Wno-unknown-attributes','-c',str(r/'tests/editor_transfer_test.c'),'-o',str(o/'editor_transfer_test.o')],check=True)
subprocess.run(['clang++',*f,str(o/'editor_transfer_test.o'),*[str(p) for p in o.glob('*.o') if p.name.endswith(('_cc.o','_cpp.o'))],'-o',str(o/'editor-transfer-test')],check=True)
subprocess.run([str(o/'editor-transfer-test')],check=True)
subprocess.run(['clang',*f,str(r/'base/tests/dreams_backup_map_test.c'),'-o',str(o/'dreams-backup-map-test')],check=True)
subprocess.run([str(o/'dreams-backup-map-test'),str(o/'dreams-map-test.mddb')],check=True)
