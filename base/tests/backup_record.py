#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""Record what tools/fm1_backup.py says to a simulated FM-1, and what the FM-1 answers, for the web backup page's test
(tests/backup_page_test.mjs replays the FM-1's side and checks that the page sends the same bytes). The simulated FM-1
is tests/backup_tool_test.py's (backup.c's frames, restore_rx()'s reads), on the device's own image (made by
tests/backup_test.c: "TSTB", version 3). Recorded: a backup, a restore answered SEL, a restore while playing.

    python3 tests/backup_record.py IMAGE OUT.json
"""
import io
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from backup_tool_test import B, FakeFM1, Link, fast  # noqa: E402


class Recorder(FakeFM1):
    """FakeFM1 whose links note every message each way, in order"""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.log = []

    def open(self, i, o):
        link = Link(self)
        put, send, log = link.q.put, link.send, self.log
        link.q.put = lambda pkt: (log.append(["dev", bytes(pkt).hex()]), put(pkt))[1]

        def rec_send(pkt):
            log.append(["host", bytes(pkt).hex()])
            return send(pkt)
        link.send = rec_send
        return link


def main(image_path, out_path):
    fast()
    img = open(image_path, "rb").read()
    flags, objs = B.parse(img)
    other = B.build([(o, d[::-1] if d else b"x" * 10) for o, d in objs], magic=img[:4], version=img[4] | img[5] << 8)
    tmp = Path(out_path).with_suffix(".bak")
    runs = {}

    dev = Recorder(img)
    assert B.main(["backup", str(tmp), "--magic", img[:4].decode()], backend=dev, out=io.StringIO()) == 0
    assert tmp.read_bytes() == img
    runs["backup"] = dev.log

    dev = Recorder(other)
    assert B.main(["restore", image_path, "--yes"], backend=dev, out=io.StringIO()) == 0 and dev.image == img
    runs["restore"] = dev.log

    dev = Recorder(other, playing=True)
    assert B.main(["restore", image_path, "--yes"], backend=dev, out=io.StringIO()) == B.EXIT["playing"]
    runs["restore_playing"] = dev.log

    tmp.unlink()
    withsec = B.join(img, [("LSMP", bytes(range(256)) * 9), ("ZZZZ", b"")])   # the page's join must make the same
    json.dump({"image": img.hex(), "other": other.hex(), "withsec": withsec.hex(), "runs": runs}, open(out_path, "w"))
    print(f"{out_path}: " + ", ".join(f"{k} {len(v)} messages" for k, v in runs.items()))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
