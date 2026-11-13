#!/usr/bin/env python3
"""Private FM-1 bench: one real Itinera transition with active USB mirror and stereo capture."""
import argparse
import json
import struct
import time
from pathlib import Path

import numpy as np
import sounddevice as sd

from creative_bench import Bench
from hardware_bench import Device
from mirror_watch_20 import MirrorWatch

MD_PLAY = 16
MD_ITINERA_START = 162
ITINERA_PAGE = 44
TEST_SCENES = {2: ('Silva-Nocturna', 177), 3: ('Mare-Nebularum', 176), 5: ('Terra-Pluvia', 190),
               12: ('Terra-Resonans', 223), 13: ('Aer-Silens', 253)}
TEST_SETTINGS = 4 * 2 + 12  # 30 minutes, varied changes, Open hall
USB_ERRORS = ('underruns', 'overruns', 'missed', 'stalls')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--expected-live', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--preset', type=int, choices=TEST_SCENES, default=5)
    parser.add_argument('--preview-only', action='store_true', help='Check first scene without waiting for its transition')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    device = Bench(args.port)
    watch = stream = None
    old_page = old_motion = old_mute = None
    started = False
    title, first_expected = TEST_SCENES[args.preset]
    report = {'readings': [], 'pass': False, 'dream': title}
    meter = {'frames': 0, 'energy': 0.0, 'peak': 0.0, 'host_overflows': 0, 'nonfinite': 0}

    def capture(samples, frames, timing, status):
        stereo = samples[:, :2]
        meter['frames'] += frames
        meter['energy'] += float(np.sum(stereo * stereo))
        meter['peak'] = max(meter['peak'], float(np.max(np.abs(stereo))))
        meter['host_overflows'] += int(bool(status.input_overflow))
        meter['nonfinite'] += int(not np.isfinite(stereo).all())

    def scene_id():
        return struct.unpack_from('<i', device.call(9), 6 * 4)[0]

    def usb_status():
        try:
            payload = device.call(22)
        except TimeoutError:
            payload = device.call(22)
        return dict(zip(('packets', *USB_ERRORS, 'min_fill', 'max_fill'), struct.unpack('<7I', payload)))

    try:
        report['before'] = Device.status(device)
        assert not report['before']['playing'], 'Transport is already running'
        assert struct.unpack('<8I', device.call(7))[3] == ITINERA_PAGE + 1
        home = device.snapshot()
        assert home == args.expected_live.read_bytes(), 'Live project changed since the backup'
        old_page, old_motion = report['before']['page'], report['before']['motion']
        device.ui(100, 0, 43)
        old_mute = struct.unpack_from('<i', device.call(9), 38*4)[0]
        assert old_mute in (0,1)
        if not old_mute:
            device.remote([3, 3, 63])
        assert struct.unpack_from('<i', device.call(9), 38*4)[0] == 1
        device.remote([4, 64, 12])
        device.ui(103, 0, 1)
        device.ui(100, 0, ITINERA_PAGE)
        watch = MirrorWatch(args.port, args.output)
        assert watch.full(), 'Incomplete initial mirrored screen'
        inputs = [i for i, info in enumerate(sd.query_devices())
                  if info['name'] == args.port and info['max_input_channels'] in (2, 8)]
        assert len(inputs) == 1, 'Need exactly one Mutable Dreams audio input'
        channels = sd.query_devices(inputs[0])['max_input_channels']
        stream = sd.InputStream(device=inputs[0], channels=channels, samplerate=44100,
                                dtype='float32', blocksize=1024, callback=capture)
        stream.start()
        device.call(3)
        usb_before = usb_status()
        device.command(MD_ITINERA_START, args.preset, TEST_SETTINGS)
        started = True
        first = scene_id()
        assert first == first_expected, first
        began = time.monotonic()
        changed = None
        while time.monotonic() - began < (18 if args.preview_only else 150):
            time.sleep(2)
            state = Device.status(device)
            elapsed = time.monotonic() - began
            biome = scene_id()
            report['readings'].append(dict(elapsed=round(elapsed, 2), biome=biome, **state))
            assert state['playing'] and state['page'] == ITINERA_PAGE, state
            for counter in ('xruns', 'overloads', 'nonfinite', 'dropped', 'quality_reductions'):
                assert state[counter] == report['before'][counter], (counter, state)
            if 14 <= elapsed < 17:
                watch.screen.png(args.output / f'{title}-FM1.png', scale=4)
            if biome != first and changed is None:
                changed = elapsed
            if changed is not None and elapsed >= changed + 8:
                break
        if not args.preview_only:
            assert changed is not None, 'No real-time Biome transition'
            report['transition_seconds'] = round(changed, 2)
        start, end = report['readings'][0], report['readings'][-1]
        report['display_frames_per_second'] = round((end['ui_frames']-start['ui_frames'])/(end['elapsed']-start['elapsed']), 2)
        report['mirror'] = watch.summary()
        report['audio'] = meter.copy()
        report['usb'] = usb_status()
        assert all(report['usb'][field] == usb_before[field] for field in USB_ERRORS), report['usb']
        assert report['mirror']['bad_frames'] == report['mirror']['sequence_gaps'] == 0
        assert meter['frames'] and meter['energy'] > 0 and 0 < meter['peak'] < 1
        assert meter['nonfinite'] == meter['host_overflows'] == 0
        report['pass'] = True
    finally:
        try:
            if started:
                device.command(MD_PLAY, 0, 0)
                assert not Device.status(device)['playing']
                report['owner_live_exact'] = device.snapshot() == home
                assert report['owner_live_exact'], 'Original live project failed byte-for-byte comparison'
        finally:
            if stream is not None:
                stream.close()
            if watch is not None:
                watch.close()
            if old_motion is not None:
                device.remote([4, 1, 32])
                if old_mute == 0:
                    device.ui(100, 0, 43)
                    device.remote([3, 3, 65])
                    assert struct.unpack_from('<i', device.call(9), 38*4)[0] == 0
                device.ui(103, 0, old_motion)
                device.ui(100, 0, old_page)
            device.close()
            (args.output / 'Gerätetest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS {title}: mirror, stereo capture, and exact return to original live project')


if __name__ == '__main__':
    main()
