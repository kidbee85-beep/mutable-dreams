"""Smoke-test the distributed Python installer ZIP without MIDI access or installed SDK files."""
from pathlib import Path
import os, subprocess, sys, tempfile, zipfile

with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp).resolve()
    with zipfile.ZipFile(sys.argv[1]) as archive:
        assert all(not Path(n).is_absolute() and '..' not in Path(n).parts for n in archive.namelist())
        archive.extractall(root)
    kit=root/'Mutable-Dreams-Installer'
    for name in ['fm1_install','fm1ports','fm1_resources','fm1_backup']:
        assert (kit/(name+'.py')).is_file(),name
    env=dict(os.environ,PYTHONPATH='')
    subprocess.run([sys.executable,str(kit/'fm1_install.py'),'--help'],cwd=kit,env=env,check=True,stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable,'-c','import fm1_install, fm1ports, fm1_resources, fm1_backup; from pathlib import Path; assert all(Path(m.__file__).resolve().parent == Path.cwd() for m in [fm1_install,fm1ports,fm1_resources,fm1_backup])'],cwd=kit,env=env,check=True)
print('PASS: distributed Python kit includes all four local dependencies; isolated --help and optional-resource imports work. No MIDI client created or device accessed.')
