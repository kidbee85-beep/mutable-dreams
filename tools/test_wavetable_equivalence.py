#!/usr/bin/env python3
"""Compare actual stereo/six-stem PCM hashes across 72 wavetable cases.

Build sanitized midiout objects with tools/build_local.py in TWO separate trees
first. This runner links the same probe against each tree; it does not rebuild
firmware, regenerate fonts, access a device or modify source trees.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tomllib


def run_probe(tree, name, output, probe):
    app, base = tree / 'app', tree / 'base'
    conf = tomllib.loads((app / 'fm1.toml').read_text())
    if conf.get('usb_audio_channels') != 8:
        raise SystemExit('Probe requires stereo plus six dry stems (8 channels)')
    flags = ['-O2', '-ffp-contract=off', '-DPLAT_HOST', '-DTEST',
             '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-DFM1B_UAC_CH=8']
    includes = [app, base / 'host', base / 'device', base / 'include',
                base / 'lib', app / 'build/gen', app / 'third_party']
    flags.extend('-I' + str(p) for p in includes)
    sdk = Path(os.environ.get('SDKROOT', '/Library/Developer/CommandLineTools/SDKs/MacOSX15.4.sdk'))
    if sys.platform == 'darwin' and sdk.is_dir():
        flags.extend(['-isysroot', str(sdk)])
    for setting, define in [('mirror', 'FM1B_MIRROR'), ('backup', 'FM1B_BACKUP')]:
        if conf.get(setting):
            flags.append('-D' + define + '=1')
    objects = [app / 'build/san' / (s.replace('/', '_').replace('.', '_') + '.o')
               for s in conf['o2']]
    if not all(p.is_file() for p in objects):
        raise SystemExit('Build sanitized DSP objects in each independent tree first')
    executable = output / (name + '-wavetable-probe')
    command = ['clang++', *flags, *conf.get('o2_cflags', []), '-std=c++11',
               str(probe), *map(str, objects), '-o', str(executable)]
    subprocess.run(command, check=True, timeout=120)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
    (output / (name + '.log')).write_text(result.stdout + result.stderr)
    result.check_returncode()
    rows = []
    for line in result.stdout.splitlines():
        model, note, morph, wheel, digest = line.split(',')
        rows.append(dict(model=int(model), note=int(note), morph=int(morph),
                         wheel=int(wheel), stereo_and_six_stem_pcm_fnv64=digest))
    keys = {(r['model'], r['note'], r['morph'], r['wheel']) for r in rows}
    expected = {(m, n, h, w) for m in (11, 13) for n in (24, 48, 72, 96)
                for h in (0, 500, 1000) for w in (0, 8192, 16383)}
    if len(rows) != 72 or keys != expected:
        raise SystemExit('Incomplete wavetable case coverage')
    return dict(source_sha256=hashlib.sha256((app / 'dsp/dreams.cpp').read_bytes()).hexdigest(),
                rows=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('--candidate', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    baseline, candidate, output = (p.resolve() for p in
                                   (args.baseline, args.candidate, args.output))
    if baseline == candidate:
        parser.error('Baseline and candidate must be independent source/build trees')
    for tree in (baseline, candidate):
        if output == tree or tree in output.parents:
            parser.error('Keep probe output outside both source/build trees')
    output.mkdir(parents=True, exist_ok=True)
    probe = Path(__file__).resolve().parents[1] / 'tests/wavetable_equivalence.cpp'
    result = dict(baseline=run_probe(baseline, 'baseline', output, probe),
                  candidate=run_probe(candidate, 'candidate', output, probe))
    result['cases'] = 72
    result['matching_pcm_hashes'] = result['baseline']['rows'] == result['candidate']['rows']
    result['method'] = 'FNV64 of actual stereo float PCM and eight-channel integer PCM; not raw-file byte comparison'
    (output / 'Equivalence.json').write_text(json.dumps(result, indent=2) + '\n')
    if not result['matching_pcm_hashes']:
        raise SystemExit('FAIL: PCM hashes differ; inspect before accepting')
    print('PASS: 72 Satin/Spectral cases; matching actual stereo and dry-stem PCM hashes.')


if __name__ == '__main__':
    main()
