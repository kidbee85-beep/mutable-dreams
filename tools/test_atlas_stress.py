#!/usr/bin/env python3
"""Private 1.4 stress check; restores the exact pre-test live patch.

Requires a prior .mddb backup and a live-before.json export. No bank write.
Only the uniquely named Mutable Dreams FM-1 MIDI and audio ports are opened.
"""
import argparse
import hashlib
import json
import struct
import time
from pathlib import Path

from device_editor import Editor
from hardware_bench import Device, Meter, configure


def sample_case(device, meter, name, seconds):
    device.call(3)  # reset telemetry counters for this case
    before = device.status()
    audio_before = meter.snapshot()
    device.command(16, 0, 1)
    readings = []
    try:
        for _ in range(seconds):
            time.sleep(1)
            readings.append(device.status())
    finally:
        device.command(27, 0, 0)
    audio_after = meter.snapshot()
    after = device.status()
    frames = audio_after['frames'] - audio_before['frames']
    energy = audio_after['energy'] - audio_before['energy']
    issues = [key for key in ('xruns', 'overloads', 'nonfinite', 'dropped')
              if after[key] != before[key]]
    if audio_after['host_input_overflows'] != audio_before['host_input_overflows']:
        issues.append('host USB overflow')
    if audio_after['nonfinite'] != audio_before['nonfinite']:
        issues.append('USB nonfinite')
    if frames < 44100 * seconds * .75:
        issues.append('USB capture incomplete')
    if energy <= 1e-9:
        issues.append('USB capture silent')
    peak = max([after['callback_peak_us']] + [x['callback_peak_us'] for x in readings])
    budget = after['callback_budget_us']
    if peak > budget * .88:
        issues.append('audio deadline under 12% margin')
    if any(not x['playing'] for x in readings):
        issues.append('transport stopped')
    return dict(name=name, seconds=seconds, models=after['models'], kit=after.get('kit'),
                quality=after['quality'], callback_peak_us=peak, callback_budget_us=budget,
                callback_pct=round(100 * peak / budget, 2), rms=round((energy/(frames*2))**.5, 7)
                if frames else 0, issues=issues, before=before, after=after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', default='Mutable Dreams FM-1')
    parser.add_argument('--original', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--seconds', type=int, default=12)
    args = parser.parse_args()
    assert args.port == 'Mutable Dreams FM-1' and 5 <= args.seconds <= 30
    original = json.loads(Path(args.original).read_text())
    original_bytes = bytes(original['patch'])
    assert original['format'] == 'mutable-dreams-patch' and len(original_bytes) == 2288
    result = dict(build='1.4.0-dev atlas r2', original_sha256=hashlib.sha256(original_bytes).hexdigest(),
                  cases=[], errors=[], restored=False)
    target = Path(args.output)
    device = meter = None
    try:
        device = Device(args.port)
        meter = Meter()
        meter.stream.start()
        assert device.status()['patch_version'] == 9
        device.remote([4, 0, 8])  # temporary test MASTER=1024, then release
        assert device.status()['master'] == 1024

        configure(device, [7] * 6, 0, 2, stress=True, active=6)
        device.remote([4, 0, 8])  # configure() briefly raises test MASTER
        device.command(14, 3, 600)  # granular mix
        device.command(23, 0, 700)  # long grains
        device.command(25, 0, 0)  # ECO quality
        device.command(27, 0, 0)
        row = sample_case(device, meter, 'six-choir-eco-grain', args.seconds)
        result['cases'].append(row)
        target.write_text(json.dumps(result, indent=2) + '\n')
        print('CASE', row['name'], row['callback_pct'], row['issues'], flush=True)

        device.command(20, 0, 113)  # one of the appended Lo-Fi kit Biomes
        if struct.unpack_from('<i', device.patch(), 155 * 4)[0] != 113:
            device.command(78, 0, 1)  # confirm replacement of unsaved stress patch
        assert struct.unpack_from('<i', device.patch(), 155 * 4)[0] == 113
        assert device.patch()[2100] == 1
        device.command(14, 3, 600)
        device.command(23, 0, 700)
        device.command(25, 0, 0)
        row = sample_case(device, meter, 'lofi-kit-three-synth-grain', args.seconds)
        result['cases'].append(row)
        target.write_text(json.dumps(result, indent=2) + '\n')
        print('CASE', row['name'], row['callback_pct'], row['issues'], flush=True)
    except BaseException as exc:
        result['errors'].append(repr(exc))
        raise
    finally:
        if device:
            try:
                device.command(27, 0, 0)
                device.remote([4, 1, 32])
            except BaseException as exc:
                result['errors'].append('stop/master release: ' + repr(exc))
            device.close()
        if meter:
            try:
                meter.stream.stop()
                meter.stream.close()
            except BaseException as exc:
                result['errors'].append('audio close: ' + repr(exc))
        try:
            editor = Editor(args.port)
            try:
                editor.upload(original_bytes)
                result['restored'] = editor.snapshot() == original_bytes
                if not result['restored']:
                    result['errors'].append('live patch differs after restore')
            finally:
                editor.close()
        except BaseException as exc:
            result['errors'].append('live restore: ' + repr(exc))
        target.write_text(json.dumps(result, indent=2) + '\n')
    if result['errors'] or any(x['issues'] for x in result['cases']):
        raise SystemExit(2)


if __name__ == '__main__':
    main()
