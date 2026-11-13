#!/usr/bin/env python3
from pathlib import Path
from hashlib import sha256
root=Path(__file__).resolve().parents[1]
manifest=root/'SOURCE-SHA256.txt'
count=0
for line in manifest.read_text().splitlines():
    expected,name=line.split('  ',1)
    rel=Path(name)
    assert not rel.is_absolute() and '..' not in rel.parts, name
    assert sha256((root/rel).read_bytes()).hexdigest()==expected, name
    count+=1
print(f'Verified {count} source files for Mutable Dreams 2.1.')
