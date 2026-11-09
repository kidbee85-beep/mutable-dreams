#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""Write a firmware's resource region into the attached FM-1 over USB-MIDI: an fm1-base firmware built with
resources = FILE (fm1.toml; device/res.c). After Melodee's tools/fm1_resources.py (Kerem Kilic, Ellic Studio; used
under MPL-2.0 with permission). tools/fm1_install.py runs it after an update when NAME-res.bin is beside the package.

    python3 tools/fm1_resources.py build/NAME-res.bin [--port NAME]

The file is build/NAME-res.bin (tools/build.py): the region as the FM-1 holds it, a 32-byte header (magic RES1, the
payload's length and CRC-32, the header's CRC-32) then the payload. The FM-1 takes only its own build's (the length
and CRC-32 its build knows), answers at once when it holds them already, and is stopped first (STOP FIRST). Then it
erases the region, pulls the payload (read requests, as a restore: tools/fm1_backup.py), checks it as written and
commits it.

Exit codes: 0 written or there already, 1 failed, 2 bad arguments or file, 3 FM-1 not found, 4 connection lost or the
FM-1 stopped answering, 5 playing (stop it first), 6 another build's resources.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fm1_backup as B  # noqa: E402
import fm1_install as I  # noqa: E402

RES_REQ, MAGIC, HEAD = 0x44, 0x31534552, 32
EXIT = dict(B.EXIT, build=6)
ERRORS = {2: "the transfer (a read request unanswered)", 7: "a flash erase", 8: "a flash write",
          10: "the FM-1 is playing: STOP FIRST, then again", 20: "these resources are another build's than the FM-1 runs",
          21: "the FM-1 is still moving its projects (switch it off and on, then again)",
          22: "the check of what was written"}


class ResError(Exception):
    pass


def payload_of(region):
    """the payload of a NAME-res.bin (its header checked) -> (payload, crc)"""
    if len(region) < HEAD:
        raise ResError("too short for a resource file")
    magic, length, crc = struct.unpack_from("<3I", region)
    if magic != MAGIC or struct.unpack_from("<I", region, 28)[0] != zlib.crc32(region[:28]):
        raise ResError("not a resource file (tools/build.py's NAME-res.bin)")
    payload = region[HEAD:HEAD + length]
    if len(payload) != length or zlib.crc32(payload) != crc:
        raise ResError("cut short or damaged")
    return payload, crc


def push(dev, region, out=sys.stdout):
    """the region into the FM-1 found (fm1_install's device) -> 0, else an error code (ERRORS)"""
    payload, crc = payload_of(region)
    dev.link.drain()
    dev.link.send(B.frame(RES_REQ, struct.pack("<II", len(payload), crc)))
    try:
        return B.serve(dev.link, payload, out)
    finally:
        print(file=out)


def run(backend, path, port=None, out=sys.stdout):
    try:
        region = Path(path).read_bytes()
        payload_of(region)
    except OSError as e:
        raise I.InstallError("usage", f"cannot read {path}: {e.strerror}")
    except ResError as e:
        raise I.InstallError("badpkg", f"{path}: {e}")
    dev = B.find(backend, port)
    print(f"device: {dev.id.text}  ({dev.name})", file=out)
    try:
        code = push(dev, region, out)
    finally:
        dev.link.close()
    if code:
        print(f"error: resources not written: {ERRORS.get(code, 'code %d' % code)}", file=sys.stderr)
        return EXIT["playing"] if code == 10 else EXIT["build"] if code == 20 else 1
    print("done: the resources are on the FM-1", file=out)
    return 0


def main(argv=None, backend=None, out=sys.stdout):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file", help="build/NAME-res.bin")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name)")
    a = ap.parse_args(argv)
    try:
        if backend is None:
            try:
                backend = I.MidoBackend()
            except ImportError:
                raise I.InstallError("usage", "needs mido and python-rtmidi (pip install mido python-rtmidi)")
        return run(backend, a.file, a.port, out)
    except I.InstallError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        print("\ninterrupted (the FM-1 keeps no half-written region: it writes the resources again when asked)",
              file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
