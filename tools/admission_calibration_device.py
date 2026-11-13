#!/usr/bin/env python3
# SPDX-License-Identifier: MIT OR GPL-3.0-only
"""Private FM-1 measurement (O-7): the FULL admission weights, from the device.

dreams.cpp admits FULL quality while 12 (+10 with effects, +12 with the kit)
plus one weight per sounding slot stays at or below 85; 16 of the 24 weights
are provisional. This measures every model as hardware_bench.py does (its
stress settings, LP24, 220 BPM, ratchets, full-rate tape), FULL, with 1..6
active slots, and fits the callback peak per added slot. The weights are then
scaled so that the eight models whose weights already came from device data
(0.4.1) keep them on average; the others follow their measured cost.

Prints the proposed weight table as a C line and writes a JSON report. Nothing
is written to flash; every case leaves the device stopped (as hardware_bench).
The firmware is only changed by hand, after reading the report.

Needs: mido, python-rtmidi, sounddevice, numpy; the FM-1's USB audio input
(one "Mutable Dreams FM-1" device) and MIDI port.
"""
import argparse
import json
import time
from pathlib import Path

from hardware_bench import Device, Meter, case

CURRENT = [8, 13, 8, 18, 10, 27, 17, 11, 8, 30, 10, 12, 10, 12, 10, 10, 17, 14, 10, 10, 10, 10, 10, 10]
ANCHORS = range(8)   # weights from the 0.4.1 device matrix
NAMES = ['KICK', 'SNARE', 'HAT', 'FM', 'VA', 'RINGS', 'HARMONIC', 'CHOIR', 'BASS', 'ELEMENTS', 'MELLOW', 'SATIN',
         'VELVET KEYS', 'SPECTRAL', 'BREATH', 'CEDAR', 'REED', 'SKIN', 'BOWED', 'AERO', 'FELT', 'SILK', 'COPPER',
         'VOWEL']


def slope(points):
    """least-squares callback microseconds per active slot"""
    n = len(points)
    if n < 2:
        return None
    mx = sum(k for k, _ in points) / n
    my = sum(u for _, u in points) / n
    den = sum((k - mx) ** 2 for k, _ in points)
    return sum((k - mx) * (u - my) for k, u in points) / den if den else None


def propose(costs):
    """weights from per-slot costs; scale fitted on the anchors"""
    pairs = [(CURRENT[m], costs[m]) for m in ANCHORS if costs.get(m)]
    scale = sum(w * c for w, c in pairs) / sum(c * c for _, c in pairs)
    return [max(1, round(costs[m] * scale)) if costs.get(m) else CURRENT[m] for m in range(24)], scale


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', help='exact MIDI port name of the FM-1')
    ap.add_argument('--seconds', type=int, default=8, help='seconds per case (default 8)')
    ap.add_argument('--output', type=Path, required=True, help='JSON report')
    ap.add_argument('--models', default='0-23', help='e.g. 0-23 or 8,9,15')
    args = ap.parse_args()
    if '-' in args.models:
        a, b = args.models.split('-')
        models = list(range(int(a), int(b) + 1))
    else:
        models = [int(x) for x in args.models.split(',')]
    d = Device(args.port)
    report = {'started': time.strftime('%Y-%m-%dT%H:%M:%S%z'), 'current': CURRENT, 'cases': [], 'costs_us': {}}
    try:
        d.meter = Meter()
        d.meter.stream.start()
        for m in models:
            points = []
            for active in range(1, 7):
                row = case(d, f'admission-{NAMES[m]}-{active}', [m] * 6, 1, 2, args.seconds, active=active)
                peak_us = row['cpu_max'] / 100 * row['final']['callback_budget_us']
                report['cases'].append({k: row[k] for k in ('name', 'cpu_max', 'cpu_mean', 'pass_', 'issues',
                                                            'guarded_eco', 'actual_quality')} | {'peak_us': peak_us})
                args.output.write_text(json.dumps(report, indent=2) + '\n')
                # only FULL readings count; stop at the guard or close to the deadline
                if row['actual_quality'] != 1 or row['guarded_eco'] or not row['pass_'] or row['cpu_max'] > 70:
                    break
                points.append((active, peak_us))
            report['costs_us'][m] = slope(points)
            print(f'{NAMES[m]:<12} per slot {report["costs_us"][m]} us from {len(points)} FULL readings', flush=True)
        costs = {m: c for m, c in report['costs_us'].items() if c}
        if not any(costs.get(m) for m in ANCHORS):
            raise SystemExit('No anchor model measured (0..7): no scale for the weights.')
        weights, scale = propose(costs)
        report.update(proposed=weights, scale=scale)
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print('weight scale', round(scale, 4), 'per microsecond')
        print('static const unsigned weight[]={' + ','.join(map(str, weights)) + '};')
        for m in range(24):
            if weights[m] != CURRENT[m]:
                print(f'  {NAMES[m]:<12} {CURRENT[m]:>3} -> {weights[m]}')
    finally:
        try:
            d.command(27, 0, 0)
        finally:
            if hasattr(d, 'meter'):
                d.meter.stream.stop()
                d.meter.stream.close()
            d.close()


if __name__ == '__main__':
    main()
