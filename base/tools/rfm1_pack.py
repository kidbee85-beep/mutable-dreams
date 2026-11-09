#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""An .rfm1 from a WebAssembly module (host/rfm1.c as tools/build.py --rfm1 links it): the module checked against
RemoteFM1IOS's docs/RFM1-FORMAT.md (version 1: its imports, its exports, one fixed memory), then the manifest
(custom section "rfm1"), the credits ("rfm1.credits") and the licence texts ("rfm1.license.N", the first line each
one's name) added to it.

  tools/rfm1_pack.py MODULE.wasm OUT.rfm1 --manifest MANIFEST.json [--credits FILE] [--license NAME=FILE ..]
  tools/rfm1_pack.py --check FILE.rfm1       the checks alone, on a finished file (and its manifest)
"""
import argparse
import json
import re
import sys
from pathlib import Path

IMPORTS = {"fd_write", "fd_read", "fd_close", "fd_seek", "clock_time_get", "proc_exit"}   # wasi_snapshot_preview1
EXPORTS = {   # name: (params, results), i32 all
    "rfm1_version": (0, 1), "rfm1_get": (1, 1), "rfm1_layout": (1, 1), "rfm1_alloc": (1, 1), "rfm1_free": (1, 0),
    "rfm1_options": (4, 0), "rfm1_boot_options": (3, 1), "rfm1_init": (1, 0), "rfm1_tick": (1, 0),
    "rfm1_frame": (0, 0), "rfm1_idle": (0, 0), "rfm1_audio": (2, 0), "rfm1_midi_in": (1, 1),
    "rfm1_midi_out_take": (1, 1), "rfm1_halted": (0, 1), "rfm1_shutdown": (0, 0), "rfm1_flash_stage": (2, 1),
    "rfm1_flash": (0, 1), "rfm1_flash_dirty": (0, 1), "rfm1_flash_sync": (0, 1), "rfm1_stats": (2, 0),
    "rfm1_ui_info": (2, 0),
}
REQUIRED = ("rfm1", "abi", "id", "name", "version", "license", "source_url")


def leb(b, i):
    """an unsigned LEB128 at b[i]: (value, next index)"""
    v = s = 0
    while True:
        c = b[i]
        i += 1
        v |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            return v, i


def uleb(v):
    out = bytearray()
    while True:
        c = v & 0x7F
        v >>= 7
        out.append(c | (0x80 if v else 0))
        if not v:
            return bytes(out)


def name(b, i):
    n, i = leb(b, i)
    return b[i:i + n].decode(), i + n


def limits(b, i):
    flag, i = leb(b, i)
    lo, i = leb(b, i)
    hi = None
    if flag & 1:
        hi, i = leb(b, i)
    return (flag, lo, hi), i


def sections(b):
    """(id, payload start, payload end) of each section"""
    if b[:8] != b"\0asm\1\0\0\0":
        raise ValueError("not a WebAssembly module (version 1)")
    i, out = 8, []
    while i < len(b):
        sid = b[i]
        n, j = leb(b, i + 1)
        out.append((sid, j, j + n))
        i = j + n
    return out


def check(b):
    """the format's rules for the module; the problems, and the custom sections {name: bytes}"""
    errs, types, funcs, imports, exports, mems, custom = [], [], [], [], {}, [], {}
    nimp_funcs = 0
    imp_types = []
    for sid, a, e in sections(b):
        if sid == 0:
            nm, j = name(b, a)
            custom[nm] = b[j:e]
        elif sid == 1:                                  # types
            n, i = leb(b, a)
            for _ in range(n):
                if b[i] != 0x60:
                    errs.append("a type that is not a function")
                    break
                p, i = leb(b, i + 1)
                ps, i = b[i:i + p], i + p
                r, i = leb(b, i)
                rs, i = b[i:i + r], i + r
                types.append((bytes(ps), bytes(rs)))
        elif sid == 2:                                  # imports
            n, i = leb(b, a)
            for _ in range(n):
                mod, i = name(b, i)
                fld, i = name(b, i)
                kind = b[i]
                i += 1
                imports.append((mod, fld, kind))
                if kind == 0:
                    t, i = leb(b, i)
                    imp_types.append(t)
                    nimp_funcs += 1
                elif kind == 1:
                    i += 1
                    _, i = limits(b, i)
                elif kind == 2:
                    lim, i = limits(b, i)
                    mems.append(lim)
                elif kind == 3:
                    i += 2
        elif sid == 3:                                  # functions
            n, i = leb(b, a)
            for _ in range(n):
                t, i = leb(b, i)
                funcs.append(t)
        elif sid == 5:                                  # memories
            n, i = leb(b, a)
            for _ in range(n):
                lim, i = limits(b, i)
                mems.append(lim)
        elif sid == 7:                                  # exports
            n, i = leb(b, a)
            for _ in range(n):
                nm, i = name(b, i)
                kind = b[i]
                idx, i = leb(b, i + 1)
                exports[nm] = (kind, idx)
        elif sid == 11:                                 # data: active, in memory 0
            n, i = leb(b, a)
            for _ in range(n):
                flag, i = leb(b, i)
                if flag == 1:
                    errs.append("a passive data segment (memory.init: not run by wasm3)")
                    break
                if flag == 2:
                    _, i = leb(b, i)                    # (its memory index)
                op = b[i]                               # the offset: i32.const N or global.get G, then end
                if op not in (0x41, 0x23):
                    errs.append(f"a data segment's offset is not a constant (opcode {op:#x})")
                    break
                _, i = leb(b, i + 1)                    # (a signed LEB: its length is the same)
                if b[i] != 0x0B:
                    errs.append("a data segment's offset is more than one instruction")
                    break
                ln, i = leb(b, i + 1)
                i += ln
    for mod, fld, kind in imports:
        if mod != "wasi_snapshot_preview1" or fld not in IMPORTS or kind != 0:
            errs.append(f"import {mod}.{fld} (only {', '.join(sorted(IMPORTS))} from wasi_snapshot_preview1)")
    if len(mems) != 1:
        errs.append(f"{len(mems)} memories: one")
    else:
        flag, lo, hi = mems[0]
        if hi != lo:
            errs.append(f"memory {lo} pages, max {hi}: fixed (initial = maximum)")
        if lo * 65536 > 32 << 20:
            errs.append(f"memory {lo * 65536 >> 20} MiB: 32 at most")
        if flag & 2:
            errs.append("a shared memory")
    if exports.get("memory", (None,))[0] != 2:
        errs.append("no exported memory")
    for nm, (np, nr) in EXPORTS.items():
        if nm not in exports or exports[nm][0] != 0:
            errs.append(f"no export {nm}")
            continue
        idx = exports[nm][1]
        t = types[imp_types[idx] if idx < nimp_funcs else funcs[idx - nimp_funcs]]
        if t != (b"\x7f" * np, b"\x7f" * nr):
            errs.append(f"{nm}: ({t[0].hex()}) -> ({t[1].hex()}), not {np} i32 -> {nr} i32")
    if "_initialize" in exports:
        idx = exports["_initialize"][1]
        t = types[imp_types[idx] if idx < nimp_funcs else funcs[idx - nimp_funcs]]
        if t != (b"", b""):
            errs.append("_initialize is not () -> ()")
    return errs, custom, (mems[0][1] * 65536 if mems else 0)


def check_manifest(m):
    errs = [f"manifest: no {k}" for k in REQUIRED if k not in m]
    if m.get("rfm1") != 1:
        errs.append("manifest: rfm1 is not 1")
    if "id" in m and not re.fullmatch(r"[a-z0-9_]{1,31}", m["id"]):
        errs.append(f"manifest: id {m['id']!r} is not [a-z0-9_]{{1,31}}")
    return errs


def section(nm, data):
    body = uleb(len(nm.encode())) + nm.encode() + data
    return b"\0" + uleb(len(body)) + body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("module", type=Path)
    ap.add_argument("out", type=Path, nargs="?")
    ap.add_argument("--manifest", type=Path)
    ap.add_argument("--credits", type=Path)
    ap.add_argument("--license", action="append", default=[], metavar="NAME=FILE")
    ap.add_argument("--check", action="store_true", help="check a finished .rfm1")
    a = ap.parse_args()
    b = a.module.read_bytes()
    errs, custom, mem = check(b)
    if a.check:
        if "rfm1" not in custom:
            errs.append("no rfm1 section (the manifest)")
        else:
            errs += check_manifest(json.loads(custom["rfm1"]))
    else:
        if any(k == "rfm1" or k.startswith("rfm1.") for k in custom):
            errs.append("already has rfm1 sections")
        m = json.loads(a.manifest.read_text())
        m.setdefault("memory_mib", mem >> 20)
        errs += check_manifest(m)
    for e in errs:
        print(f"rfm1: {e}", file=sys.stderr)
    if errs:
        return 1
    if a.check:
        m = json.loads(custom["rfm1"])
        print(f"rfm1: {a.module.name}: {m['name']} {m['version']} ({m['id']}), {m['license']}, {mem >> 20} MiB, "
              f"{len(b)} B: ok")
        return 0
    out = bytearray(b)
    out += section("rfm1", json.dumps(m, indent=1, ensure_ascii=False).encode())
    if a.credits:
        out += section("rfm1.credits", a.credits.read_bytes())
    for n, spec in enumerate(a.license):
        nm, _, path = spec.partition("=")
        text = Path(path).read_text(errors="replace")
        out += section(f"rfm1.license.{n}", (nm + "\n" + text).encode())
    a.out.write_bytes(bytes(out))
    print(f"rfm1     {a.out}  ({len(out)} B, {mem >> 20} MiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
