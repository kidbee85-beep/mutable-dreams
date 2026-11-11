"""Private FM-1 qualification: six voices, FX, external clock, CC and USB mirror.

No saved-bank write. Requires the matching local safety backup. The live patch,
clock mode and physical master are restored even when a playback check fails.
"""
from pathlib import Path
import json, struct, sys, threading, time
import mido
import numpy as np
import sounddevice as sd
from creative_bench import Bench
from mirror_watch_20 import MirrorWatch
from dreams_backup import B, KIND

R = Path(__file__).resolve().parents[1]
O = (R / '../../outputs/Mutable-Dreams-2.0-Stability-Lab').resolve()
P = O / 'Mirror-Soak'
P.mkdir(exist_ok=True)
seconds = int(sys.argv[1]) if len(sys.argv) > 1 else 900
assert 60 <= seconds <= 7200
home = (R / 'backups/Before-Stability-2026-11-10/live-before-flash.raw').read_bytes()
patch = bytearray(json.loads((O / 'Examples/Biome-196.json').read_text())['patch'])
for offset, value in ((28, 108), (40, 1000), (48, 850)):
    struct.pack_into('<i', patch, offset, value)
for track, model in enumerate((17, 15, 16, 9, 8, 14)):
    at = 56 + track * 88
    struct.pack_into('<i', patch, at, model)
    for offset, value in ((16, 500), (20, 750), (24, 16), (28, 16), (32, 0),
                          (36, 1000), (48, 0), (52, 1), (60, 4), (76, 1000), (80, 2)):
        struct.pack_into('<i', patch, at + offset, value)

report = {'private': True, 'requested_seconds': seconds, 'pass': False,
          'configuration': '6 mixed voices / repeats / long granular / FX / 108 BPM USB clock / 14-bit CC / mirror',
          'readings': [], 'errors': []}
meter = {'frames': 0, 'energy': 0.0, 'peak': 0.0, 'overflows': 0, 'nonfinite': 0}
running = threading.Event()
d = watch = stream = clock = None
old_macro = None

def flush():
    (P / 'Qualification.json').write_text(json.dumps(report, indent=2) + '\n')

def stats():
    w = struct.unpack('<49I', d.call(1))
    return dict(ms=w[1], xruns=w[4], overloads=w[5], nonfinite=w[7], drops=w[8],
                playing=w[10], peak_us=w[37], budget_us=w[38], ui_peak_ms=w[48],
                events=list(w[11:17]))

def capture(x, n, timing, status):
    meter['frames'] += n
    meter['energy'] += float(np.sum(x * x))
    meter['peak'] = max(meter['peak'], float(np.max(np.abs(x))))
    meter['overflows'] += int(bool(status.input_overflow))
    meter['nonfinite'] += int(not np.isfinite(x).all())

def send_clock():
    at = time.monotonic()
    while running.is_set():
        d.o.send(mido.Message('clock'))
        at += 60 / (108 * 24)
        time.sleep(max(0, at - time.monotonic()))

try:
    d = Bench('Mutable Dreams FM-1')
    assert struct.unpack('<8I', d.call(7)) == (1, 11, 2604, 40, 16, 64, 420, 197)
    assert d.snapshot() == home, 'Owner live state changed: take a new backup first'
    saved = dict(B.parse((R / 'backups/Before-Stability-2026-11-10/Saved-After-Final.mddb').read_bytes(), KIND)[1])
    old_macro = saved[2][26]  # Backup object 2 is preferences; not live-editor stored slot 2.
    d.command(16, 0, 0)
    d.command(61, 0, 0)
    d.upload(patch)  # The editor must be idle/internal before a live upload.
    d.command(126, 0, 1)
    d.command(61, 0, 1)
    watch = MirrorWatch('Mutable Dreams FM-1', P)
    assert watch.full(), 'Incomplete initial picture'
    report['mirror_initial'] = watch.summary()
    inputs = [i for i, v in enumerate(sd.query_devices())
              if v['name'] == 'Mutable Dreams FM-1' and v['max_input_channels'] == 2]
    assert len(inputs) == 1
    stream = sd.InputStream(device=inputs[0], samplerate=44100, channels=2,
                            dtype='float32', blocksize=1024, callback=capture)
    stream.start()
    d.remote([4, 96, 18])
    running.set()
    clock = threading.Thread(target=send_clock, daemon=True)
    clock.start()
    report['before'] = stats()
    d.command(16, 0, 1)
    start = time.monotonic()
    j = 0
    while time.monotonic() - start < seconds:
        time.sleep(.5)
        for k in range(4):
            value = (j * 173 + k * 877) % 16384
            for number, v in ((16 + k, value >> 7), (48 + k, value & 127)):
                d.o.send(mido.Message('control_change', channel=15, control=number, value=v))
        if j % 6 == 0:
            d.ui(100, 0, [0, 1, 3, 6, 12, 19, 36, 38, 39][(j // 6) % 9])
        if j % 4 == 0:
            state = stats()
            report['readings'].append(state)
            for key in ('xruns', 'overloads', 'nonfinite', 'drops'):
                assert state[key] == report['before'][key], (key, state)
            assert state['playing'] and state['peak_us'] / state['budget_us'] < .85, state
            assert meter['overflows'] == meter['nonfinite'] == 0, meter
        if j % 120 == 0:
            actual_macros = struct.unpack_from('<4i', d.snapshot(), 12)
            expected_macros = tuple(((j * 173 + k * 877) % 16384) * 1000 // 16383
                                    for k in range(4))
            assert actual_macros == expected_macros, (actual_macros, expected_macros)
            report['elapsed_seconds'] = round(time.monotonic() - start, 2)
            report['mirror'] = watch.summary()
            flush()
            print('PASS progress', report['elapsed_seconds'], 'seconds', flush=True)
        j += 1
    report['elapsed_seconds'] = round(time.monotonic() - start, 2)
    report['after'] = stats()
    report['mirror'] = watch.summary()
    report['audio'] = dict(meter)
    report['audio']['rms'] = (meter['energy'] / (meter['frames'] * 2)) ** .5
    report['callback_peak_percent'] = round(max(x['peak_us'] / x['budget_us'] * 100
                                                for x in report['readings']), 2)
    assert report['mirror']['bad_frames'] == report['mirror']['sequence_gaps'] == 0
    assert report['mirror']['pixels_received'] == 57600 and meter['peak'] > 1e-6
    assert all(a > b for a, b in zip(report['after']['events'], report['before']['events']))
    report['pass'] = True
except BaseException as error:
    report['errors'].append(repr(error))
    raise
finally:
    running.clear()
    if clock is not None:
        clock.join(timeout=3)
    if stream is not None:
        stream.stop()
        stream.close()
    if watch is not None:
        watch.close()
    if d is not None:
        try:
            d.command(16, 0, 0)
            d.command(61, 0, 0)
            if old_macro is not None:
                d.command(126, 0, old_macro)
            d.upload(home)
            d.ui(100, 0, 0)
            d.remote([4, 1, 32])
            report['owner_live_exact'] = d.snapshot() == home
            report['final'] = stats()
        except Exception as error:
            report['cleanup_error'] = repr(error)
        d.close()
    flush()
    print(json.dumps({k: v for k, v in report.items() if k != 'readings'}, indent=2), flush=True)
