#!/usr/bin/env python3
"""Screen-reader coverage matrix (GitHub #10): every control action on the host simulator with a listening
reader, one fresh run each, and what the firmware said (SAY frames). Offline evidence, not a device test.
  tools/screen_reader_matrix.py HOST_BINARY OUT.tsv [--check]
--check: exit 1 when an action that should speak said nothing (the rows marked must)."""
import subprocess, sys, tempfile, os
from pathlib import Path

BOOT = "wait 3400\nlisten\nwait 400\n"
# (label, script, must speak)
A = []
for k in ('K1', 'K2', 'K3', 'K4'):
    A.append((f'HOME {k} +1', f'spin {k} 1\nwait 1500\n', True))
A += [('HOME SELECT +1 (track)', 'spin SELECT 1\nwait 1500\n', True),
      ('HOME ALGO +1', 'spin ALGO 1\nwait 1500\n', True),
      ('HOME PRESET +1 (Biome)', 'spin PRESET 1\nwait 1500\n', True)]
for b in ('HOME', 'EDIT', 'SEQ', 'FX', 'GLO', 'SEL', 'REC', 'ENV', 'ARP', 'SAVE', 'PLAY'):
    A.append((f'tap {b}', f'tap {b}\nwait 1500\n', True))
for b in ('HOME', 'EDIT', 'SEQ', 'FX', 'GLO', 'SEL', 'REC', 'ENV', 'ARP', 'SAVE', 'PLAY'):
    A.append((f'hold {b} (help)', f'press {b}\nwait 1100\nrelease {b}\nwait 1500\n', True))
for menu in ('EDIT', 'SEQ', 'FX', 'GLO'):
    for k in ('K1', 'K2', 'K3', 'K4'):
        A.append((f'{menu} page {k} +1', f'tap {menu}\nwait 600\nlisten\nwait 300\nspin {k} 1\nwait 1500\n', True))
    A.append((f'{menu} page SELECT +1', f'tap {menu}\nwait 600\nlisten\nwait 300\nspin SELECT 1\nwait 1500\n', True))
A += [('page wheel EDIT + SELECT', 'press EDIT\nwait 150\nspin SELECT 1\nwait 300\nrelease EDIT\nwait 1500\n', True),
      ('reader WHERE on HOME', 'where\nwait 1500\n', True),
      ('reader WHERE on EDIT page', 'tap EDIT\nwait 600\nlisten\nwait 300\nwhere\nwait 1500\n', True),
      ('new reader (HELLO) on HOME', '', True)]

def run(host, body, tmp):
    p = Path(tmp) / 's.txt'; p.write_text(BOOT + body)
    r = subprocess.run([host, str(p), tmp], capture_output=True, text=True, timeout=120)
    out = r.stdout.split('\n')
    # what was said after the last 'listen' (a fresh listen re-arms: the action's own words follow it)
    said = [l.split(':', 1)[1].strip() for l in out if l.startswith('said')]
    return said

def main():
    host, out = sys.argv[1], sys.argv[2]; check = '--check' in sys.argv
    rows, silent = [], []
    with tempfile.TemporaryDirectory() as tmp:
        base = run(host, '', tmp)          # what boot + listen alone says
        for label, body, must in A:
            said = run(host, body, tmp)
            new = said[len(base):] if body and said[:len(base)] == base else said
            if label.startswith('new reader'): new = base
            rows.append((label, ' | '.join(new)))
            if must and not new: silent.append(label)
    Path(out).write_text('action\tsaid\n' + ''.join(f'{a}\t{s}\n' for a, s in rows))
    n = len(rows); print(f'{n - len(silent)}/{n} actions spoke; silent: {len(silent)}')
    for s in silent: print('  silent:', s)
    sys.exit(1 if check and silent else 0)

if __name__ == '__main__': main()
