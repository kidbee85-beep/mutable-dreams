#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""Back up what an FM-1 firmware saves to a file, and put it back, over USB-MIDI: any fm1-base firmware built with
backup = true (fm1.toml; device/backup.c). (From Polly's tools/polly_backup.py.)

    python3 tools/fm1_backup.py backup FILE [--magic XXXX] [--port NAME]
    python3 tools/fm1_backup.py restore FILE [--yes] [--port NAME]
    python3 tools/fm1_backup.py info FILE

A backup holds the app's saved objects (app.h APP_BACKUP_OBJS), never the panel's calibration. The firmware can play
on while it is backed up (what is not saved yet is left out then: stop first to have it). The FM-1 answers only a
request with its app's magic (app.h APP_BACKUP_MAGIC, 4 letters: Polly's PLYB): --magic, or known here by the FM-1's
identity (KNOWN). A restore takes the magic from the file.

A restore replaces all of it. The firmware must be stopped ("STOP FIRST" otherwise). It receives the whole backup and
checks it (every part's CRC-32, the whole file's) before anything changes, then asks on its screen: RESTORE FROM
USB? SEL yes, HOME no. A cut cable, a damaged file, another firmware's or another version's backup, or HOME changes
nothing.

The file (little-endian): the magic, u16 version, u16 count, u32 flags (bit 0: taken while playing), u32 CRC-32 of
those 12 bytes; each object: u16 id, u16 length, u32 CRC-32, the payload; u32 CRC-32 of everything before.
After that a file may carry the firmware's own sections, which the web backup page reads and writes over the
firmware's own SysEx (web/backup/index.html EXTRAS: LOOM-8's samples): each a 4-letter tag, u32 length, the bytes,
u32 CRC-32 of the bytes. This tool restores the backup itself and leaves the sections alone (split, the firmware's
own tool for them); info lists them.

As a library (a firmware's own tool: Polly's polly_backup.py): Kind(magic, version, objs, names, identity, name),
then parse / build / find / backup / restore / main with kind=.

Needs mido with python-rtmidi (as tools/fm1_install.py). Exit codes: 0 done, 1 cancelled or failed, 2 bad arguments
or file, 3 FM-1 not found, 4 connection lost or the FM-1 stopped answering, 5 playing (stop it first), 6 not the
firmware the backup is for.
"""
import argparse
import os
import struct
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fm1_install as I  # noqa: E402

RS_REQ, BU_REQ, BU_END = 0x41, 0x42, 0x43
OK_ADDR, ASK_ADDR, FAIL_ADDR = 0xC0000000, 0xC1000000, 0xCF000000
HEAD, REC, PAYLOAD_MAX = 16, 8, 3840
PLAYING, DECLINED = 10, 11
DELAY = {"poll": 0.5, "idle": 10.0, "ask": 75.0, "first": 3.0}
EXIT = dict(I.EXIT, playing=5)


class BackupError(Exception):
    pass


class Kind:
    """what one firmware's backups are. magic: 4 bytes (APP_BACKUP_MAGIC), None: any (taken from the file); version
    (APP_BACKUP_VERSION) and objs (APP_BACKUP_OBJS' ids), None: whatever the file says; names(obj) -> a part's name;
    identity: the package identity's prefix (fm1.toml id_prefix), None: any FM-1; name: the firmware's, in messages;
    port: the MIDI port's name to try first"""

    def __init__(self, magic=None, version=None, objs=None, names=None, identity=None, name=None, port=None):
        self.magic, self.version, self.objs, self.identity = magic, version, objs, identity
        self.name = name or "the firmware"
        self.who = name or "the FM-1"                 # (who plays, who reloads: the FM-1 when the firmware is any)
        self.named = bool(name)
        self.names = names or (lambda obj: f"object {obj}")
        self.port = port or name or "FM-1"

    def older(self, ver):
        """the message for a backup of another (older or newer) version"""
        return (f"a backup of another version of {self.name} (version {ver}; this one makes version {self.version}): "
                f"it cannot be restored")

    def summary(self, objs):
        used = sum(1 for _, d in objs if d)
        return f"{sum(len(d) for _, d in objs)} bytes: {used} of {len(objs)} parts hold something"


ANY = Kind()
# firmwares whose magic need not be given (--magic): an identity prefix -> (magic, name)
KNOWN = {"FM-1_3": (b"PLYB", "Polly")}


def magic_arg(s):
    b = s.encode("ascii", "replace")
    if len(b) != 4 or not all(0x20 < v < 0x7F for v in b):
        raise argparse.ArgumentTypeError(f"{s!r}: four letters (app.h APP_BACKUP_MAGIC, e.g. PLYB)")
    return b


def frame(cmd, body):
    """a host message in the update's framing (ota.c ota_take)"""
    n = len(body)
    msg = bytes([0x00, 0x59, cmd, n & 0xFF, (n >> 8) & 0xFF, 0]) + body + bytes([~sum(body) & 0xFF])
    return b"\xF0" + I.pack7(msg) + b"\xF7"


def parse_frame(pkt):
    """a device message -> (cmd, body) or None"""
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    u = I.unpack7(pkt[1:-1])
    if len(u) < 7 or u[:2] != b"\x00\x59":
        return None
    n = u[3] | u[4] << 8 | u[5] << 16
    if n + 7 != len(u) or ~sum(u[6:6 + n]) & 0xFF != u[6 + n]:
        return None
    return u[2], u[6:6 + n]


def parse(image, kind=ANY):
    """check a backup file whole -> (flags, [(obj, payload)]); BackupError if it is not one (of kind's) or is
    damaged"""
    what = f"a {kind.name} backup" if kind.magic else "a backup"
    if len(image) < HEAD + 4:
        raise BackupError(f"too short for {what}")
    magic, ver, n, flags, hcrc = struct.unpack_from("<4sHHII", image)
    if kind.magic and magic != kind.magic:
        raise BackupError(f"not {what}")
    if not all(0x20 < v < 0x7F for v in magic) or hcrc != zlib.crc32(image[:12]):
        raise BackupError("its head is damaged" if all(0x20 < v < 0x7F for v in magic) else "not a backup")
    if kind.version is not None and ver != kind.version:
        raise BackupError(kind.older(ver))
    if kind.objs is not None and n != len(kind.objs):
        raise BackupError(f"a backup of another version ({ver}, {n} parts)")
    if len(image) < HEAD + n * REC + 4:
        raise BackupError("cut short")
    objs, pos = [], HEAD
    for i in range(n):
        want = kind.objs[i] if kind.objs is not None else None
        if pos + REC > len(image):
            raise BackupError("cut short")
        obj, ln, crc = struct.unpack_from("<HHI", image, pos)
        pos += REC
        part = kind.names(want if want is not None else obj)
        if (want is not None and obj != want) or ln > PAYLOAD_MAX or pos + ln + 4 > len(image):
            raise BackupError(f"part {part}: a wrong header")
        data = image[pos:pos + ln]
        if zlib.crc32(data) != crc:
            raise BackupError(f"part {part}: damaged (CRC-32)")
        objs.append((obj, bytes(data)))
        pos += ln
    if pos + 4 != len(image) or struct.unpack_from("<I", image, pos)[0] != zlib.crc32(image[:pos]):
        raise BackupError("damaged (the whole file's CRC-32)")
    return flags, objs


def split(data):
    """a file -> (the backup itself, [(tag, bytes)]): the firmware's sections after the backup's own CRC-32;
    BackupError on a damaged section (the backup itself is checked by parse)"""
    pos = HEAD
    if len(data) >= HEAD:
        n = struct.unpack_from("<H", data, 6)[0]
        for _ in range(n):
            if pos + REC > len(data):
                break
            pos += REC + struct.unpack_from("<H", data, pos + 2)[0]
    pos += 4
    if pos >= len(data):
        return bytes(data), []
    out, at = [], pos
    while at < len(data):
        if at + 8 > len(data):
            raise BackupError("a section after the backup is cut short")
        tag, ln = data[at:at + 4].decode("latin-1"), struct.unpack_from("<I", data, at + 4)[0]
        if at + 8 + ln + 4 > len(data):
            raise BackupError(f"its section {tag} is cut short")
        body = bytes(data[at + 8:at + 8 + ln])
        if struct.unpack_from("<I", data, at + 8 + ln)[0] != zlib.crc32(body):
            raise BackupError(f"its section {tag} is damaged (CRC-32)")
        out.append((tag, body))
        at += 8 + ln + 4
    return bytes(data[:pos]), out


def join(image, sections):
    """the backup and the firmware's sections, as the web page writes them"""
    out = bytearray(image)
    for tag, body in sections:
        out += tag.encode("latin-1") + struct.pack("<I", len(body)) + body + struct.pack("<I", zlib.crc32(body))
    return bytes(out)


def magic_of(image):
    return bytes(image[:4])


def build(objs, flags=0, magic=None, version=None, kind=ANY):
    """a backup file from [(obj, payload)] in the app's order (the tests' simulated FM-1)"""
    magic = magic or kind.magic
    version = kind.version if version is None else version
    head = magic + struct.pack("<HHI", version, len(objs), flags)
    out = bytearray(head + struct.pack("<I", zlib.crc32(head)))
    for obj, data in objs:
        out += struct.pack("<HHI", obj, len(data), zlib.crc32(data)) + data
    out += struct.pack("<I", zlib.crc32(out))
    return bytes(out)


def find(backend, port, kind=ANY):
    up = I.Updater(backend, port or kind.port)
    dev = up.find(only_port=bool(port))           # ports with the firmware's name first, then any FM-1
    if not dev:
        raise I.not_found(up)
    if kind.identity and not dev.id.text.startswith(kind.identity):
        dev.link.close()
        raise I.InstallError("model", f"the FM-1 runs {dev.id.text}, not {kind.name} ({kind.identity}xx)")
    return dev


def known(identity):
    """the KNOWN firmware an identity is: (magic, name), or None"""
    for prefix, v in KNOWN.items():
        if identity.startswith(prefix):
            return v
    return None


def receive(link, out, kind=ANY):
    """the device's backup frames until its end frame -> the image (checked)"""
    got, last = bytearray(), time.monotonic()
    first = True
    while True:
        pkt = link.read(DELAY["poll"])
        if pkt is None:
            if link.lost:
                raise I.InstallError("lost", "the FM-1 was disconnected: run this again")
            if time.monotonic() - last > (DELAY["first"] if first else DELAY["idle"]):
                raise I.InstallError("stopped", ("the FM-1 does not answer (" + (
                    f"an older {kind.name}, without backup?)" if kind.named else
                    "a firmware without backup, or a magic that is not its app's?)")) if first else
                    "the FM-1 stopped sending")
            continue
        f = parse_frame(pkt)
        if not f:
            continue
        cmd, body = f
        last = time.monotonic()
        if cmd == BU_REQ and len(body) >= 4:
            first = False
            off = struct.unpack_from("<I", body)[0]
            if off != len(got):
                raise I.InstallError("lost", f"a piece of the backup went missing at {len(got)}: run this again")
            got += body[4:]
            print(f"\rreceiving {len(got)} bytes\033[K", end="", file=out, flush=True)
        elif cmd == BU_END and len(body) >= 10:
            n, crc, status, flags = struct.unpack_from("<IIBB", body)
            print(file=out)
            if status:
                raise I.InstallError("badreq", f"the FM-1 could not read its flash (status {status})")
            if n != len(got) or crc != zlib.crc32(got):
                raise I.InstallError("lost", "the backup arrived damaged: run this again")
            return bytes(got)


def backup(backend, path, port=None, out=sys.stdout, kind=ANY, magic=None):
    dev = find(backend, port, kind)
    print(f"device: {dev.id.text}  ({dev.name})", file=out)
    magic = magic or kind.magic
    if not magic:
        k = known(dev.id.text)
        if not k:
            dev.link.close()
            raise I.InstallError("usage", f"{dev.id.text}: give its app's backup magic, --magic XXXX (app.h "
                                 "APP_BACKUP_MAGIC, four letters)")
        magic = k[0]
    try:
        dev.link.drain()
        dev.link.send(frame(BU_REQ, magic))
        image = receive(dev.link, out, kind)
    finally:
        dev.link.close()
    try:
        flags, objs = parse(image, kind)
        if magic_of(image) != magic:
            raise BackupError("another magic")
    except BackupError as e:
        raise I.InstallError("lost", f"the backup arrived damaged ({e}): run this again")
    with open(path, "wb") as f:
        f.write(image)
    print(f"done: {path}  ({kind.summary(objs)})", file=out)
    if flags & 1:
        print(f"note: {kind.who} was playing, so changes it had not saved yet are not in it (stop, then back up "
              "again to have them)", file=out)
    return 0


def serve(link, image, out, kind=ANY):
    """answer the FM-1's read requests until it reports; -> 0 restored, else its error code"""
    last, idle, asked = time.monotonic(), DELAY["first"], 0
    while True:
        pkt = link.read(DELAY["poll"])
        if pkt is None:
            if link.lost:
                raise I.InstallError("lost", "the FM-1 was disconnected: nothing was changed unless it said so")
            if time.monotonic() - last > idle:
                raise I.InstallError("stopped", "the FM-1 stopped asking: nothing changed" if asked else
                                     "the FM-1 does not answer (" + (f"an older {kind.name}, without restore?)"
                                                                     if kind.named else "a firmware without restore?)"))
            continue
        r = I.parse_request(pkt)
        if not r:
            continue
        fl, addr, n = r
        last, idle, asked = time.monotonic(), DELAY["idle"], asked + 1
        if addr & 0xFF000000 in (OK_ADDR, FAIL_ADDR, ASK_ADDR):
            link.send(I.response(addr, b"success\0", fl))
            if addr == ASK_ADDR:
                print("\rreceived and checked. On the FM-1: RESTORE FROM USB? SEL yes, HOME no\033[K", file=out,
                      flush=True)
                idle = DELAY["ask"]
                continue
            return 0 if addr == OK_ADDR else (addr & 0xFFFF) or 99
        if n > I.MAXDATA or addr >= len(image):
            raise I.InstallError("badreq", f"the FM-1 asked for {addr:#x}+{n}, outside the backup")
        data = image[addr:addr + n]
        if not link.send(I.response(addr, data + bytes(n - len(data)), fl)):
            raise I.InstallError("lost", "the FM-1 was disconnected")
        print(f"\rsending {min(addr + n, len(image)) * 100 // len(image):3d}%\033[K", end="", file=out, flush=True)


def errors(kind):
    """backup.c's RS_E_* codes, in words"""
    return {1: "the file's size", 2: "the transfer (a read request unanswered)",
            3: "the file's head (a backup of another firmware, or of another version" +
               (f" of {kind.name})" if kind.named else ")"),
            4: "a part's header", 5: "a part's CRC-32 (a byte changed on the way)", 6: "the whole file's CRC-32",
            7: "a flash erase", 8: "a flash write", 9: "the read-back (the flash does not hold what was written)",
            10: f"{kind.who} is playing: STOP FIRST, then restore", 11: "answered HOME (no) on the FM-1",
            12: "writing the headers (the FM-1 finishes it when switched on again)"}


def restore(backend, path, port=None, yes=False, ask=I.ask_tty, out=sys.stdout, kind=ANY):
    try:
        with open(path, "rb") as f:
            image = f.read()
    except OSError as e:
        raise I.InstallError("usage", f"cannot read {path}: {e.strerror}")
    try:
        image, sections = split(image)
        flags, objs = parse(image, kind)
    except BackupError as e:
        raise I.InstallError("badpkg", f"{path}: {e}")
    print(f"backup: {kind.summary(objs)}", file=out)
    if sections:
        print(f"note: its section{'s' if len(sections) > 1 else ''} {', '.join(t for t, _ in sections)} "
              f"({sum(len(b) for _, b in sections)} bytes) {'are' if len(sections) > 1 else 'is'} not restored by "
              "this tool: the web backup page restores them, or the firmware's own tool", file=out)
    dev = find(backend, port, kind)
    print(f"device: {dev.id.text}  ({dev.name})", file=out)
    k = known(dev.id.text)
    if not kind.magic and k and k[0] != magic_of(image):
        dev.link.close()
        raise I.InstallError("model", f"{path} is not a {k[1]} backup (its magic {magic_of(image)!r}, {k[1]}'s "
                             f"{k[0]!r}); the FM-1 runs {k[1]}")
    if not yes and not ask(f"Replace everything on {kind.who} with this backup? (the FM-1 asks too) [y/N] "):
        dev.link.close()
        print("cancelled", file=out)
        return 1
    try:
        dev.link.drain()
        dev.link.send(frame(RS_REQ, struct.pack("<II", len(image), zlib.crc32(image))))
        code = serve(dev.link, image, out, kind)
    finally:
        print(file=out)
        dev.link.close()
    if code == PLAYING:
        print(f"STOP FIRST: {kind.who} is playing. Stop it, then restore again.", file=sys.stderr)
        return EXIT["playing"]
    if code == DECLINED:
        print("not restored: answered no on the FM-1. Nothing changed.", file=out)
        return 1
    if code:
        print(f"error: not restored: {errors(kind).get(code, 'code %d' % code)}." +
              ("" if code == 12 else " Nothing changed."), file=sys.stderr)
        return 1
    print(f"done: restored ({kind.who} reloaded what it saves)", file=out)
    return 0


def info(path, out=sys.stdout, kind=ANY):
    with open(path, "rb") as f:
        image = f.read()
    image, sections = split(image)
    flags, objs = parse(image, kind)
    what = f"a {kind.name} backup" if kind.magic else f"a backup ({magic_of(image).decode('ascii')}, version " \
        f"{struct.unpack_from('<H', image, 4)[0]})"
    print(f"{path}: {what}, {kind.summary(objs)}" + (" (taken while playing)" if flags & 1 else ""), file=out)
    for o, d in objs:
        print(f"  {kind.names(o):10s} {len(d):5d} bytes" if d else f"  {kind.names(o):10s}     -", file=out)
    for tag, body in sections:
        print(f"  section {tag} {len(body)} bytes (the firmware's own: not restored by this tool)", file=out)
    return 0


def main(argv=None, backend=None, out=sys.stdout, ask=I.ask_tty, kind=ANY, description=None):
    ap = argparse.ArgumentParser(description=description or __doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["backup", "restore", "info"])
    ap.add_argument("file", help="the backup file")
    ap.add_argument("--port", metavar="NAME", help=f"MIDI port to use (part of its name; default: {kind.port}'s)")
    ap.add_argument("--yes", action="store_true", help="restore: do not ask here (the FM-1 still asks)")
    if not kind.magic:
        ap.add_argument("--magic", type=magic_arg, metavar="XXXX",
                        help="backup: the app's magic (app.h APP_BACKUP_MAGIC), unless KNOWN by the FM-1's identity")
    a = ap.parse_args(argv)
    try:
        if a.command == "info":
            return info(a.file, out, kind)
        if backend is None:
            try:
                backend = I.MidoBackend()
            except ImportError:
                raise I.InstallError("usage", "needs mido and python-rtmidi (pip install mido python-rtmidi)")
        if a.command == "backup":
            return backup(backend, a.file, a.port, out, kind, getattr(a, "magic", None))
        return restore(backend, a.file, a.port, a.yes, ask, out, kind)
    except (OSError, BackupError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except I.InstallError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        print("\ninterrupted (a restore not confirmed on the FM-1 changed nothing)", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
