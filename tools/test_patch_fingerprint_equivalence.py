#!/usr/bin/env python3
"""Compare canonical fingerprints using actual source from independent DSP trees.

First build sanitized midiout objects in each tree with tools/build_local.py.
The probe includes each tree's real dreams.cpp to reach its private fingerprint;
only the other DSP objects are linked. It never touches a device or user data.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tomllib


def probe(tree, label, output, source):
    app, base = tree / 'app', tree / 'base'
    conf = tomllib.loads((app / 'fm1.toml').read_text())
    flags = ['-O2', '-ffp-contract=off', '-DPLAT_HOST', '-DTEST', '-g',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    for folder in [app, base / 'host', base / 'device', base / 'include',
                   base / 'lib', app / 'build/gen', app / 'third_party']:
        flags.append('-I' + str(folder))
    if conf.get('usb_audio_channels') == 8:
        flags.append('-DFM1B_UAC_CH=8')
    sdk = Path(os.environ.get('SDKROOT', '/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk'))
    if sys.platform == 'darwin' and sdk.is_dir():
        flags.extend(['-isysroot', str(sdk)])
    objects = [app / 'build/san' / (s.replace('/', '_').replace('.', '_') + '.o')
               for s in conf['o2'] if s != 'dsp/dreams.cpp']
    if not all(p.is_file() for p in objects):
        raise SystemExit('Build the sanitized DSP objects in both independent trees first')
    exe = output / (label + '-fingerprint-probe')
    subprocess.run(['clang++', *flags, *conf.get('o2_cflags', []), '-std=c++11',
                    str(source), *map(str, objects), '-o', str(exe)], check=True, timeout=120)
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=120)
    (output / (label + '.log')).write_text(run.stdout + run.stderr)
    run.check_returncode()
    lines = run.stdout.splitlines()
    kind, count, size = lines[0].split(',')
    count, size = int(count), int(size)
    assert kind == 'counts' and count > 0 and size > 0
    rows = lines[1:]
    assert len(rows) == count + size
    for index, row in enumerate(rows):
        prefix, number, first, second = row.split(',')
        assert prefix == ('factory' if index < count else 'byte')
        assert int(number) == (index if index < count else index - count)
        assert len(first) == len(second) == 8
        int(first, 16); int(second, 16)
    return dict(factory_biomes=count, patch_bytes=size, rows=rows,
                dsp_source_sha256=hashlib.sha256((app / 'dsp/dreams.cpp').read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    baseline, candidate, output = (p.resolve() for p in
                                   (args.baseline, args.candidate, args.output))
    if baseline == candidate:
        parser.error('Use independent baseline and candidate trees')
    for tree in (baseline, candidate):
        if output == tree or tree in output.parents:
            parser.error('Keep output outside both source/build trees')
    output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parents[1] / 'tests/patch_fingerprint_equivalence.cpp'
    result = dict(baseline=probe(baseline, 'baseline', output, source),
                  candidate=probe(candidate, 'candidate', output, source))
    result['matching_fingerprints'] = (result['baseline']['rows'] == result['candidate']['rows'])
    result['cases'] = len(result['baseline']['rows'])
    (output / 'Equivalence.json').write_text(json.dumps(result, indent=2) + '\n')
    if not result['matching_fingerprints']:
        raise SystemExit('FAIL: canonical fingerprints differ')
    print(f"PASS: {result['cases']} canonical fingerprints match (factories and every patch-byte mutation).")


if __name__ == '__main__':
    main()
