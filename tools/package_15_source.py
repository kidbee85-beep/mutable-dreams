#!/usr/bin/env python3
"""Prepare a private 1.5 source snapshot without generated or personal files."""
from pathlib import Path
from hashlib import sha256
import zipfile
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT.parents[1] / 'outputs/Mutable-Dreams-1.5-Private-Preview/Mutable-Dreams-1.5-Source.zip'
TOP = ['.gitignore','BUILDING.md','DEVELOPMENT-NOTES-BLE.md','LICENSE','LICENSE-MIT.txt','LICENSING-GPL.txt','README.md','SOURCE-PUBLICATION.md','firmware.json']
DIRS = ['app','base','web','presets','tools','tests','assets','docs/release-1.4','docs/release-1.5','docs/img']
EXTRA = ['docs/development/BUILD-ENVIRONMENT-FIX.txt']
EXCLUDE_PARTS = {'.git','__pycache__','node_modules','build','.venv','backups'}
EXCLUDE_SUFFIXES = {'.fwsc','.mddb','.wav','.elf','.bin','.o','.pyc','.dis'}
EXCLUDE_FILES = {'package_release_installer.py','package_development_installer.py','probe'}
files = []
for name in TOP + EXTRA:
    p = ROOT / name
    if not p.is_file(): raise SystemExit(f'Missing: {name}')
    files.append(p)
for name in DIRS:
    for p in (ROOT / name).rglob('*'):
        if not p.is_file(): continue
        rel = p.relative_to(ROOT)
        if any(part in EXCLUDE_PARTS for part in rel.parts): continue
        if p.suffix in EXCLUDE_SUFFIXES or p.name in EXCLUDE_FILES: continue
        if p.name == 'SOURCE-SHA256.txt': continue
        files.append(p)
files = sorted(set(files), key=lambda p: p.relative_to(ROOT).as_posix())
manifest = ''.join(f'{sha256(p.read_bytes()).hexdigest()}  {p.relative_to(ROOT).as_posix()}\n' for p in files)
(ROOT / 'SOURCE-SHA256.txt').write_text(manifest)
with zipfile.ZipFile(OUT,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for p in files + [ROOT / 'SOURCE-SHA256.txt']:
        z.write(p, 'Mutable-Dreams-1.5-Source/' + p.relative_to(ROOT).as_posix())
print(f'{len(files)} source files; {OUT.stat().st_size} zip bytes; sha256 {sha256(OUT.read_bytes()).hexdigest()}')
