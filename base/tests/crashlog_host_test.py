#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""The crash history read as a program reads it: the frames the host simulator sent for tests/crashlog.txt (its
sysex_log, raw F0 .. F7) through tools/fm1_remote.py's parser (parse_crash_frames, no mido), each record's fields
against the crashes the script made; the end frames ("end 0", "end 4": none told from an answer lost) and the clear;
a power-on with the dumped flash (FM1B_FLASH) keeping the history; and the --elf symbols (ElfSymbols) on a small
ELF32 made here, as the FM-1's.
    python3 tests/crashlog_host_test.py HOST_EXE OUTDIR    (OUTDIR: where tests/crashlog.txt ran)"""
import importlib
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
R = importlib.import_module("fm1_remote")
FAILS = []


def check(what, ok):
    print(f"{what:<64} {'ok' if ok else 'FAIL'}")
    if not ok:
        FAILS.append(what)


def frames(raw, head):
    """the texts of the frames F0 <head> .. F7 in a raw SysEx log, in order"""
    out, k = [], 0
    while True:
        a = raw.find(b"\xf0", k)
        if a < 0:
            return out
        b = raw.find(b"\xf7", a)
        if b < 0:
            return out
        if list(raw[a + 1:a + 4]) == head:
            out.append(raw[a + 4:b])
        k = b + 1


def answers(texts):
    """the frames split into answers, each up to its "end N" (or "cleared")"""
    got, cur = [], []
    for t in texts:
        cur.append(t)
        if t.startswith(b"end ") or t in (b"cleared", b"clear failed"):
            got.append(cur)
            cur = []
    return got


def elf32(syms):
    """a minimal ELF32 little-endian with a .symtab of (name, value, size, type) and its .strtab"""
    strtab = b"\0" + b"".join(n.encode() + b"\0" for n, _, _, _ in syms)
    pos, st = 1, [b"\0" * 16]
    for n, v, sz, typ in syms:
        st.append(struct.pack("<IIIBBH", pos, v, sz, (1 << 4) | typ, 0, 1))
        pos += len(n) + 1
    symtab = b"".join(st)
    shstr = b"\0.symtab\0.strtab\0"
    body_off = 52
    sym_off, str_off = body_off, body_off + len(symtab)
    shs_off = str_off + len(strtab)
    sh_off = (shs_off + len(shstr) + 3) & ~3
    head = b"\x7fELF" + bytes([1, 1, 1]) + b"\0" * 9 + struct.pack("<HHIIIIIHHHHHH", 2, 0, 1, 0, 0, sh_off, 0, 52, 0, 0,
                                                                      40, 4, 3)
    sh = [b"\0" * 40,
          struct.pack("<IIIIIIIIII", 1, 2, 0, 0, sym_off, len(symtab), 2, 1, 4, 16),
          struct.pack("<IIIIIIIIII", 9, 3, 0, 0, str_off, len(strtab), 0, 0, 1, 0),
          struct.pack("<IIIIIIIIII", 0, 3, 0, 0, shs_off, len(shstr), 0, 0, 1, 0)]
    img = head + symtab + strtab + shstr
    return img + b"\0" * (sh_off - len(img)) + b"".join(sh)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    exe, out = Path(sys.argv[1]), Path(sys.argv[2])
    raw = (out / "crashlog_frames.bin").read_bytes()
    ans = answers(frames(raw, R.LH))
    check("four answers: end 0, end 4, cleared, end 0 (and nothing else)", [a[-1] for a in ans] ==
          [b"end 0", b"end 4", b"cleared", b"end 0"])
    recs, end = R.parse_crash_frames(ans[0])
    check("none: no records, end 0 (not None: an answer)", recs == [] and end == 0)
    recs, end = R.parse_crash_frames(ans[1])
    check("four records, end 4, numbered 1/4 .. 4/4", end == 4 and [(r["i"], r["n"]) for r in recs] ==
          [(1, 4), (2, 4), (3, 4), (4, 4)])
    check("... oldest first: the script's pcs in order",
          [r["pc"] for r in recs] == [0x02011000, 0x02012000, 0x02013000, 0x02015000])
    check("... counts 1, 2 then 1 after the power-off, 3 (the one in a save not kept)",
          [r["count"] for r in recs] == [1, 2, 1, 3])
    check("... vec, seq, uptime", [r["vec"] for r in recs] == [3, 5, 3, 3] and [r["seq"] for r in recs] ==
          [1, 2, 3, 4] and recs[0]["ms"] == 1221 and recs[1]["ms"] == 500)
    check("... rets, sp, the build the same in each", recs[0]["rets"] == 0x02010FC0 and
          recs[0]["sp"] == 0x01C79FFC and len({r["build"] for r in recs}) == 1)
    check("... the registers with each (r0..r15, usp, ssp)", all(len(r["regs"]) == 18 for r in recs) and
          recs[0]["regs"]["r1"] == 0x11111110 and recs[1]["regs"]["r0"] == 2 and
          recs[0]["regs"]["usp"] == recs[0]["sp"] + 0x100)
    recs, end = R.parse_crash_frames(ans[0][:0])
    check("no frames at all: end None (an answer lost)", end is None and recs == [])
    lines = R.format_crash(R.parse_crash_frames(ans[1])[0][0])
    check("format_crash: a reader's lines", lines[0].startswith("crash 1/4: n 1 vec 0x3 pc 02011000") and
          len(lines) == 5)

    # a power-on with the flash dumped after the four (the history is the flash's, not the simulator's RAM)
    script = out / "crashlog_boot.txt"
    script.write_text("expect crashes 4\nexpect crash_count 0\n")
    r = subprocess.run([str(exe), str(script), str(out)], capture_output=True, text=True,
                       env={"FM1B_FLASH": str(out / "crashlog_flash.bin"), "PATH": "/usr/bin:/bin"})
    check("power-on with the dumped flash: the four records there", r.returncode == 0 and "FAIL" not in r.stdout)

    # --elf: function+offset from the symbol table
    elf = out / "crashlog_syms.elf"
    elf.write_bytes(elf32([("app_render", 0x02011000, 0x200, 2), ("ui_frame", 0x02012000, 0x80, 2),
                           ("fm1_crash", 0x01C7C000, 0x40, 1)]))
    syms = R.ElfSymbols(str(elf))
    check("ElfSymbols: an address inside a function", syms.name(0x02011010) == "app_render+0x10")
    check("... its start", syms.name(0x02012000) == "ui_frame+0x0")
    check("... past its size: none", syms.name(0x02012100) == "?")
    check("... an object", syms.name(0x01C7C004) == "fm1_crash+0x4")
    lines = R.format_crash(R.parse_crash_frames(ans[1])[0][1], syms)
    check("format_crash with the ELF: the pc named", "pc 02012000 (ui_frame+0x0)" in lines[0])
    print("all ok" if not FAILS else f"FAIL: {len(FAILS)}")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
