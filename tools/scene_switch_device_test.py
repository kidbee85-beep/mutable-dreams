#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-3.0-only
"""Private FM-1 measurement (finding F-1): Biome changes while the transport plays.

Sends bursts of 1..5 Biome requests in one SysEx batch, so that they reach a
single audio callback, then confirms the last one. Reads the callback peak,
xruns, overloads, ECO reductions and dropped commands every few seconds.
The live project is read before the test and uploaded again afterwards;
it must compare byte for byte. Nothing is written to flash, banks stay
untouched. This tool never installs firmware.

Before running: export the User Banks (.mddb) with the editor and pass the
file with --backup, stop the transport and leave the device on any page.
"""
import argparse
import json
import random
import struct
import time
from pathlib import Path

from creative_bench import Bench
from hardware_bench import Device

MD_SCENE, MD_SCENE_CONFIRM, MD_PLAY = 20, 78, 16
# Dense ensembles first, then the Itinera worlds from the 30-minute device run.
BIOMES = (244, 173, 207, 217, 220, 234, 235, 242, 169, 251, 227, 253, 208, 263, 271, 168,
          85, 108, 114, 119, 127, 130, 136, 146, 149, 158, 163, 165, 222, 221, 231, 243)
COUNTERS = ('xruns', 'overloads', 'nonfinite', 'dropped', 'quality_reductions')


def batch(device, items):
    payload = []
    for c, t, v in items:
        z = v + 8192
        payload += [c, t, z & 127, z >> 7]
    device.call(2, payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--port', required=True, help='Exact unique MIDI port name of the FM-1')
    parser.add_argument('--backup', type=Path, required=True, help='User-bank export (.mddb) made before this test')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--minutes', type=float, default=60)
    parser.add_argument('--rate', type=float, default=10, help='Biome requests per second (encoder detents)')
    parser.add_argument('--seed', type=int, default=37)
    args = parser.parse_args()
    assert args.backup.is_file() and args.backup.stat().st_size > 0, 'Export the User Banks first (--backup)'
    args.output.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    device = Bench(args.port)
    report = {'pass': False}; report.update(minutes=args.minutes, rate=args.rate, seed=args.seed, readings=[], bursts={})
    home = None
    started = False
    try:
        before = Device.status(device)
        report['before'] = before
        assert not before['playing'], 'Stop the transport first'
        home = device.snapshot()
        (args.output / 'Live-Before.bin').write_bytes(home)
        device.call(3)  # reset CPU, UI and callback peaks
        batch(device, [(MD_SCENE, 0, BIOMES[0]), (MD_SCENE_CONFIRM, 0, 1)])
        time.sleep(.3)
        device.command(MD_PLAY, 0, 1)
        started = True
        began = last_read = time.monotonic()
        sent = 0
        while time.monotonic() - began < args.minutes * 60:
            size = rng.choice((1, 1, 2, 3, 5))
            ids = [rng.choice(BIOMES) for _ in range(size)]
            batch(device, [(MD_SCENE, 0, i) for i in ids] + [(MD_SCENE_CONFIRM, 0, 1)])
            report['bursts'][str(size)] = report['bursts'].get(str(size), 0) + 1
            sent += size
            time.sleep(max(.02, size / args.rate))
            if time.monotonic() - last_read >= 5:
                last_read = time.monotonic()
                state = Device.status(device)
                state['elapsed'] = round(last_read - began, 1)
                state['requests'] = sent
                report['readings'].append(state)
                assert state['playing'], 'Transport stopped during the test'
                print(f"{state['elapsed']:7.1f}s  callback peak {state['callback_peak_us']} / "
                      f"{state['callback_budget_us']} us  cpu peak {state['cpu_peak']}%  "
                      + '  '.join(f'{k} {state[k]-before[k]}' for k in COUNTERS), flush=True)
        end = Device.status(device)
        report['after'] = end
        report['requests'] = sent
        report['callback_peak_us'] = end['callback_peak_us']
        report['callback_budget_us'] = end['callback_budget_us']
        report['deltas'] = {k: end[k] - before[k] for k in COUNTERS}
        report['pass'] = (all(v == 0 for v in report['deltas'].values())
                           and end['callback_peak_us'] < end['callback_budget_us'])
    finally:
        try:
            if started:
                device.command(MD_PLAY, 0, 0)
                time.sleep(.3)
            if home is not None:
                device.upload(home)
                report['owner_live_exact'] = device.snapshot() == home
        finally:
            device.close()
            (args.output / 'Biome-Wechsel.json').write_text(json.dumps(report, indent=2) + '\n')
    assert report.get('owner_live_exact'), 'Live project failed the byte-for-byte return'
    print(('PASS' if report['pass'] else 'FAIL') +
          f" {report.get('requests', 0)} Biome requests; callback peak {report.get('callback_peak_us')} of "
          f"{report.get('callback_budget_us')} us; deltas {report.get('deltas')}")
    raise SystemExit(0 if report['pass'] else 1)


if __name__ == '__main__':
    main()
