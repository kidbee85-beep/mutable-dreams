#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""tools/fm1ports.py (finding FM-1s on USB-MIDI, as web/fm1ports.js) and the tools that use it (fm1_install.py's
Updater, so fm1_backup.py and fm1_mirror.py too; fm1_remote.py; fm1_debug.py) against a fake mido (no device): a port
with a usual name taken without asking; an input's output (the same name, the same but for a number, the only one);
an FM-1 under another device's name found by its answer to the screen reader's HELLO, while a loopback (an IAC bus
sending the HELLO back) is not taken and other gear gets three HELLOs at most and never the update protocol's identity
query; a port that fails to open (an error that is not OSError, as python-rtmidi's) skipped; --port alone asked.
  python3 tests/ports_test.py"""
import queue
import sys
import time
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<72} {'ok' if cond else 'FAIL'}")
    failed += not cond


# ---------------------------------------------------------------- fake mido ---

class RtError(Exception):
    """as python-rtmidi's: not an OSError"""


class Msg:
    def __init__(self, type, data=(), **kw):
        self.type, self.data = type, tuple(data)


class Dev:
    """a device on the computer: its input and output port names; answer(data) -> the SysEx it sends back"""

    def __init__(self, name, out=None, answer=None, broken=False):
        self.name, self.out, self.answer, self.broken = name, out or name, answer, broken
        self.sent, self.ins = [], []


class In:
    def __init__(self, dev, callback):
        self.dev, self.callback, self.q, self.closed = dev, callback, queue.Queue(), False

    def deliver(self, data):
        if self.closed:
            return
        m = Msg("sysex", data)
        self.callback(m) if self.callback else self.q.put(m)

    def iter_pending(self):
        while not self.q.empty():
            yield self.q.get_nowait()

    def poll(self):
        return None if self.q.empty() else self.q.get_nowait()

    def close(self):
        self.closed = True

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


class Out:
    def __init__(self, dev):
        self.dev = dev

    def send(self, msg):
        self.dev.sent.append(bytes(msg.data))
        for reply in (self.dev.answer(bytes(msg.data)) if self.dev.answer else []):
            for i in self.dev.ins:
                i.deliver(reply)

    def close(self):
        pass

    __enter__ = In.__enter__

    def __exit__(self, *a):
        pass


def fake_mido(devs):
    m = types.ModuleType("mido")
    m.Message = Msg
    m.get_input_names = lambda: [d.name for d in devs]
    m.get_output_names = lambda: [d.out for d in devs]

    def dev(name, attr):
        d = next(d for d in devs if getattr(d, attr) == name)
        if d.broken:
            raise RtError("port taken")
        return d

    def open_input(name, callback=None):
        d = dev(name, "name")
        i = In(d, callback)
        d.ins.append(i)
        return i

    m.open_input = open_input
    m.open_output = lambda name: Out(dev(name, "out"))
    return m


HELLO = bytes([0x7D, 0x53, 0x52, 1, 1])


def fm1(firmware, words="", identity=None):
    """an fm1-base firmware: answers the HELLO with its name; the identity query with identity (if any)"""
    def answer(d):
        if d == HELLO:
            return [HELLO + firmware.encode() + b"\0" + words.encode()]
        if identity and d == I.HS_QUERY[1:-1]:
            t = identity.encode()
            return [I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + t + bytes(28 - len(t)))]
        return []
    return answer


def loopback(d):
    return [d]


import fm1ports as P  # noqa: E402
import fm1_install as I  # noqa: E402

I.DELAY.update(open=0, hs=0.05, hs_full=0.05, hello=0.05)
T = 0.05

# ---------------------------------------------------------------- fm1ports ---

ok(P.pair_output("FM-1 MIDI 1", ["FM-1 MIDI 2", "KeyStep"]) == "FM-1 MIDI 2", "an output named the same but for its number")
ok(P.pair_output("FM-1", ["Other"]) == "Other" and P.pair_output("FM-1", ["A", "B"]) is None, "  else the only output; none of two")
ok(P.pair_output("X 1", ["X 2", "X 3"]) is None, "  not one of two numbered alike")
ok(P.hello_answer(HELLO + b"X0X 1.1\0https://a.b/words/1.json") == ("X0X 1.1", "https://a.b/words/1.json"),
   "a HELLO answer: its name and words link")
ok(P.hello_answer(HELLO) is None and P.hello_answer(b"\x7D\x53\x52\x10\x01hi") is None, "  the HELLO itself, a SAY: none")
ok(P.hello_answer(HELLO + b"T 1\0http://x.json")[1] == "", "  a link that is not https: dropped")

# a usual name: taken without sending anything
other, dev = Dev("Arturia KeyStep"), Dev("FM-1 MIDI 1", "FM-1 MIDI 2")
m = fake_mido([other, dev])
ok(P.find(mido=m) == [("FM-1 MIDI 1", "FM-1 MIDI 2")] and not other.sent, "a usual name: taken, nothing sent to other gear")

# an FM-1 under another device's name: found by its HELLO answer; the loopback and other gear not
iac, other = Dev("IAC Bus 1", answer=loopback), Dev("Launchkey Mini")
taken = Dev("Busy Synth", broken=True)
dev = Dev("Arturia KeyStep", answer=fm1("Template 1.0"))
m = fake_mido([iac, taken, other, dev])
t0 = time.monotonic()
got = P.find(mido=m, timeout=T)
ok(got == [("Arturia KeyStep", "Arturia KeyStep")], f"an FM-1 on an unusual port: found by its HELLO answer ({got})")
ok(iac.sent == [HELLO] and other.sent == [HELLO] and dev.sent == [HELLO], "  one HELLO each, nothing else")
ok(not taken.sent, "  a port that fails to open (not an OSError): skipped")
ok(time.monotonic() - t0 < 1.0, "  a short wait")

# nothing answering: three HELLOs, then none; with an Asking, three in all however often asked
iac, other = Dev("IAC Bus 1", answer=loopback), Dev("Launchkey Mini")
m = fake_mido([iac, other])
ok(P.find(mido=m, timeout=T) == [] and iac.sent == [HELLO] * 3 and other.sent == [HELLO] * 3,
   "only a loopback and other gear: none, after three HELLOs")
a = P.Asking()
for _ in range(5):
    P.ask(m, tries=1, timeout=T, asking=a)
ok(len(other.sent) == 6, "  asked again with an Asking: three more at most")
ok(P.find(mido=m, hello=False) == [] and len(other.sent) == 6, "  hello=False: not asked")

# --port: only that one, nothing asked; connect: the first that opens
broken, dev = Dev("FM-1 A", broken=True), Dev("FM-1 B", answer=fm1("T 1"))
m = fake_mido([broken, dev, Dev("Other")])
ok(P.find("other", mido=m) == [("Other", "Other")] and not dev.sent, "--port: only the port named, nothing asked")
inp, out, name = P.connect(mido=m)
ok(name == "FM-1 B", "connect: a port that fails to open skipped, the next taken")
try:
    P.connect("nothing", mido=m)
    ok(False, "connect: none matching raises LookupError")
except LookupError:
    ok(True, "connect: none matching raises LookupError")

# ------------------------------------------------- fm1_install.py's Updater ---

sys.modules["mido"] = fake_mido([])                   # (MidoBackend imports mido)


def updater(devs, port=None):
    sys.modules["mido"] = m = fake_mido(devs)
    up = I.Updater(I.MidoBackend(), port)
    up.backend.mido = m
    return up


iac, other = Dev("IAC Bus 1", answer=loopback), Dev("Launchkey Mini")
dev = Dev("Arturia KeyStep", answer=fm1("Template 1.0", identity="FM-1_1100"))
up = updater([iac, other, dev])
d = up.find()
ok(d is not None and d.name == "Arturia KeyStep" and d.id.text == "FM-1_1100",
   "fm1_install: an FM-1 on an unusual port found by its HELLO answer, then its identity")
hs = I.HS_QUERY[1:-1]
ok(hs not in iac.sent and hs not in other.sent, "  the loopback and other gear: never the identity query")
d.link.close()
for _ in range(3):
    up.find().link.close()
ok(dev.sent.count(HELLO) == 1 and iac.sent.count(HELLO) == 1, "  the FM-1 found: asked by its name from then on")
iac, other = Dev("IAC Bus 1", answer=loopback), Dev("Launchkey Mini")
up = updater([iac, other])
for _ in range(5):
    up.find(only_port=False)                           # (wait_for asks again and again)
ok(iac.sent == [HELLO] * 3 and other.sent == [HELLO] * 3, "  no FM-1, asked again and again: three HELLOs at most")
up2 = I.Updater(up.backend)
up2.find(only_port=False)                              # (fm1_backup.find: a new Updater each try)
ok(other.sent == [HELLO] * 3, "  a new Updater on the same backend: none more")

dev = Dev("Arturia KeyStep", answer=fm1("Template 1.0", identity="FM-1_1100"))
up = updater([dev], port="Launch")
ok(up.find() is None and not dev.sent, "fm1_install --port: only that port, no HELLO to the others")

stock = Dev("FM-1", answer=fm1("unused", identity="FM-1_015"))
stock.answer = (lambda f: lambda d: [] if d == HELLO else f(d))(stock.answer)   # M-VAVE's: no HELLO answer
other = Dev("Launchkey Mini")
up = updater([stock, other])
d = up.find()
ok(d is not None and d.id.text == "FM-1_015" and not other.sent, "fm1_install: a usual name first (M-VAVE's), nothing else asked")
d.link.close()

# ---------------------------------------------------------- fm1_remote.py ---

dev = Dev("Arturia KeyStep", answer=fm1("Template 1.0"))
sys.modules["mido"] = fake_mido([Dev("IAC Bus 1", answer=loopback), dev])
import fm1_remote as R  # noqa: E402
r = R.FM1()
ok(r.name == "Arturia KeyStep", "fm1_remote: the FM-1 found by its HELLO answer")
r.run(["tap", "REC"])
ok(any(s[:3] == bytes([0x7D, 0x4C, 0x43]) for s in dev.sent), "  and driven")
sys.modules["mido"] = fake_mido([dev])
R.mido = sys.modules["mido"]
try:
    R.FM1("nothing")
    ok(False, "fm1_remote --port: none matching, a clear exit")
except SystemExit as e:
    ok("no MIDI port matching" in str(e), "fm1_remote --port: none matching, a clear exit")

# ----------------------------------------------------------- fm1_debug.py ---

def debug_answer(d):
    if d == HELLO:
        return [HELLO + b"X0X 1.1\0"]
    if d[:4] == bytes([0x7D, 0x46, 0x4C, 40]):
        return [bytes([0x7D, 0x46, 0x4C, 40]) + bytes(5) + bytes(D.w35(0x12345678))]
    return []


dev = Dev("Some Port", answer=debug_answer)
sys.modules["mido"] = fake_mido([Dev("IAC Bus 1", answer=loopback), dev])
import fm1_debug as D  # noqa: E402
b = D.ask(D.PEEK, D.w35(0x1000) + D.w35(1))
ok(D.r35(b, 5) == 0x12345678, "fm1_debug: the FM-1 found by its HELLO answer, peeked")

print("FAILED" if failed else "ok")
sys.exit(1 if failed else 0)
