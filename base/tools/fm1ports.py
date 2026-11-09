# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""Finding FM-1s on USB-MIDI from Python, as web/fm1ports.js does in the browser: for tools/fm1_install.py (and
through it fm1_backup.py and fm1_mirror.py), fm1_remote.py and fm1_debug.py. Needs mido with python-rtmidi.

A port is taken when its name is an FM-1's usual one (PORT_RE, fm1ports.js PORT_NAMES). Otherwise the other ports are
asked with the screen reader's HELLO (fm1-reader PROTOCOL.md: F0 7D 53 52 01 01 F7; 7D is the ID for non-commercial
use, which other gear ignores), at most three times each (Asking), and the ones that answer with a name are taken:
every firmware on fm1-base answers it (device/announce.c), whatever its port is called. A loopback (macOS's IAC bus, a
DAW's MIDI through) sends the HELLO back as it went out, with no name, so it is not taken for an FM-1. An input's output
is the one with its name, else the only one with its name but for a number at the end (Windows numbers each port),
else the only output there is.

  ports = fm1ports.find(port=None, names=())    [(input, output)], best first (port: part of a name, only those)
  inp, out, name = fm1ports.connect(...)         the first of them that opens (LookupError: none)
"""
import re
import time
from collections import namedtuple

# port names taken without asking (any case, part of the name): fm1ports.js PORT_NAMES. Never probe other gear
PORT_RE = re.compile(r"fm-?1|felucca|ota|composite|sinco|usb-midi|optimist|sloop|melodee|loom-?8", re.I)   # + the names FM-1 firmwares give their own port
SR = (0x7D, 0x53, 0x52)
HELLO = bytes([*SR, 1, 1])                 # as mido has a SysEx: without F0 and F7
TRIES, TIMEOUT = 3, 0.5

Found = namedtuple("Found", "input output firmware words")


def _mido(mido):
    if mido is None:
        import mido
    return mido


def usual(name, names=()):
    """an FM-1's usual port name, or one of names (parts of names, any case)"""
    n = (name or "").lower()
    return bool(PORT_RE.search(n)) or any(x.lower() in n for x in names if x)


def bare(name):
    return re.sub(r"\s+\d+$", "", name or "")


def pair_output(name, outs):
    """an input's output among outs, or None"""
    if name in outs:
        return name
    same = [o for o in outs if bare(o) == bare(name)]
    if len(same) == 1:
        return same[0]
    return outs[0] if len(outs) == 1 else None


def pairs(ins, outs):
    """every input that has an output: [(input, output)]"""
    ins, outs = list(dict.fromkeys(ins)), list(dict.fromkeys(outs))
    return [(i, o) for i in ins if (o := pair_output(i, outs))]


def hello_answer(data):
    """a HELLO answer's SysEx data (mido's: no F0, no F7) -> (firmware, words), or None (a HELLO sent back has no name)"""
    d = bytes(data)
    if len(d) < 5 or tuple(d[:3]) != SR or d[3] != 1:
        return None
    name, _, words = bytes(b for b in d[5:] if b < 128).decode("ascii").partition("\0")
    if not name:
        return None
    words = words.split("\0")[0]
    return name, (words if re.fullmatch(r"https://[^\s\"']+\.json", words) else "")


class Asking:
    """how often each output has been sent a HELLO: at most tries times (other gear gets three short SysEx it ignores,
    not one every time a tool looks). forget(name) when it is unplugged: asked again"""

    def __init__(self, tries=TRIES):
        self.tries, self.n = tries, {}

    def due(self, name):
        k = self.n.get(name, 0)
        if k >= self.tries:
            return False
        self.n[name] = k + 1
        return True

    def forget(self, name):
        self.n.pop(name, None)


def _opened(names, open_port):
    got = {}
    for n in names:
        try:
            got[n] = open_port(n)
        except Exception:           # taken by another program, or gone (python-rtmidi's errors are not OSError)
            pass
    return got


def _close(ports):
    for p in ports:
        try:
            p.close()
        except Exception:
            pass


def ask(mido=None, only=None, skip=lambda n: False, tries=TRIES, timeout=TIMEOUT, asking=None):
    """the port pairs that answer a HELLO -> [Found], in the inputs' order. only: the input names to ask (default: all);
    skip(name): inputs not to ask; asking: an Asking (an output already asked asking.tries times is not asked again).
    Up to tries rounds, timeout seconds each; stops after the first round anything answers in. No BYE: the FM-1 stops
    by itself 5 s after the last HELLO"""
    mido = _mido(mido)
    ps = [(i, o) for i, o in pairs(mido.get_input_names(), mido.get_output_names())
          if (only is None or i in only) and not skip(i)]
    if not ps:
        return []
    ins = _opened([i for i, _ in ps], mido.open_input)
    outs = _opened(dict.fromkeys(o for _, o in ps), mido.open_output)
    got = {}
    try:
        ps = [(i, o) for i, o in ps if i in ins and o in outs]
        for _ in range(tries):
            sent = set()
            for i, o in ps:
                if i in got or o in sent or (asking is not None and not asking.due(o)):
                    continue
                try:
                    outs[o].send(mido.Message("sysex", data=list(HELLO)))
                    sent.add(o)
                except Exception:   # unplugged meanwhile
                    pass
            if not sent:
                break
            end = time.monotonic() + timeout
            while time.monotonic() < end and len(got) < len(ps):
                for i, o in ps:
                    try:
                        msgs = list(ins[i].iter_pending())
                    except Exception:
                        continue
                    for m in msgs:
                        h = hello_answer(m.data) if m.type == "sysex" else None
                        if h and i not in got:
                            got[i] = Found(i, o, *h)
                time.sleep(0.01)
            if got:
                break
    finally:
        _close(list(ins.values()) + list(outs.values()))
    return [got[i] for i, _ in ps if i in got]


def find(port=None, names=(), mido=None, hello=True, timeout=TIMEOUT):
    """the FM-1's port pairs, best first: [(input, output)]. port: part of a port's name (any case), and only those
    (nothing asked); else the pairs with a usual name or one of names, and when there are none, the ones that answer
    a HELLO (hello=False: not asked)"""
    mido = _mido(mido)
    ps = pairs(mido.get_input_names(), mido.get_output_names())
    if port:
        return [p for p in ps if port.lower() in p[0].lower()]
    mine = [p for p in ps if usual(p[0], names)]
    if mine or not hello:
        return mine
    return [(f.input, f.output) for f in ask(mido, skip=lambda n: usual(n, names), timeout=timeout)]


def connect(port=None, names=(), mido=None, hello=True, timeout=TIMEOUT):
    """the first of find()'s pairs that opens -> (input port, output port, its name); LookupError when none does"""
    mido = _mido(mido)
    for i, o in find(port, names, mido, hello, timeout):
        try:
            inp = mido.open_input(i)
        except Exception:
            continue
        try:
            return inp, mido.open_output(o), i
        except Exception:
            _close([inp])
    raise LookupError(f"no MIDI port matching {port!r}" if port else
                      "no FM-1 on USB-MIDI (USB data cable? another app using it? its firmware on fm1-base?)")
