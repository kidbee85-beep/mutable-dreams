#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""The static stack depth of CPU0's two stacks (device/app.ld: main, interrupts), from an app's disassembly
(objdump -d -mcpu=r3), its ELF and its units' optimised IR (build.py with fm1.toml stack_check: OUT/*.opt.ll): the
deepest call path's frames, for every root of each stack. tools/build.py checks the result against fm1.toml
main_stack / irq_stack.

  python3 tools/stack_depth.py DIS ELF --ir UNIT.opt.ll ... [--bound FUNC=N ...] [--fn FUNC ...] [--json OUT]

A function's frame: the registers it pushes ('[--sp] = {..}', 4 B each, a range rA-rB counted whole) and every
'sp += -N'. Any other change of sp ('sp = rN', 'sp += rN': a variable-length array, alloca) is listed ('var') and
fails the check. The tables after a 'tbb' / 'tbh' (a switch's) are data, read up to their nearest target (and the
count its bound check gives); an instruction decoded across a table's end is listed ('tables'' ends'), and fails it.
A path goes through:
  - a direct call ('call N <f : ..>', by its address: two static functions may share a name) and a tail call ('goto
    N <f : ..>' to another function's first address), both counted with the caller's whole frame on the stack;
  - an indirect call ('call rN', or 'goto rN' after no table load) to the functions stack_ir.py finds it can reach
    (the IR's flow of the functions' addresses: by field, global, local, parameter; each call's signature): a call
    that flow cannot follow reaches every function whose address the image holds, and is listed. A function with an
    indirect call and no IR (assembly) reaches all of them too;
  - a recursion: a function that calls itself, bounded by the source (BOUNDS, and --bound for the app's: its frame
    counted that many times), else unbounded: listed with its cycle, the path cut there; it fails the check.
The roots:
  main stack (usp)        _start (crt0: sp = _ustack_top, then fm1_cstart and the main loop under it)
  interrupt stack (ssp)   the interrupts' stubs, nested at their worst: one handler of each priority level on top of
                          the one below (the hardware nests a higher level only; equal levels never nest):
                          BLE (2; 3 with dualcfg flat), the audio (3, alnk0), TIMER5 (4), the Classic radio (2 at rest,
                          5 in a run, the console's prio=1..6: a level of its own, counted on top of every other), and
                          the fault path (fm1_fatal_stubs: the exception at 7, the crash screen) on top of all of them.
The hardware pushes nothing itself on an interrupt (the stubs save psr, rets and reti: fm1_isr.S); an interrupt runs on
ssp, never on the main stack (crt0). No handler re-enables the interrupts below its own level (an assumption: not
checked here)."""
import argparse
import json
import re
import struct
import sys

import stack_ir

FN = re.compile(r"^([A-Za-z_$][\w.$]*):\s*$")
INS = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")   # (an undecoded one: no tab before it)
PUSH = re.compile(r"\[--sp\] = \{([^}]*)\}")
PUSH1 = re.compile(r"\[--sp\] = (r\d+|rets|reti|retx|rete|psr|icfg)\b")
SPSUB = re.compile(r"^sp \+= -(\d+)$")
SPVAR = re.compile(r"^sp (?:[-+]?=) r\d+|^sp = r\d+")
CALL = re.compile(r"^call -?\d+ <([\w.$]+)(\+0x[0-9A-Fa-f]+)? : ([0-9a-f]+) >")
GOTO = re.compile(r"^goto -?\d+ <([\w.$]+)(\+0x[0-9A-Fa-f]+)? : ([0-9a-f]+) >")
CALLR = re.compile(r"^call r\d+")
GOTOR = re.compile(r"^goto r\d+")
REF = re.compile(r"<([A-Za-z_$][\w.$]*) : ([0-9a-f]+) >")
TABLE = re.compile(r"\[r\d+\+r\d+<<2\]|^tbh |^tbb ")

ISR_LEVELS = (                                     # (level, its handlers' stubs): one of each nested on the one below
    ("BLE (2)", ("isr_ble_event", "isr_ble_rx")),
    ("audio (3)", ("isr_alnk0",)),
    ("TIMER5 (4)", ("isr_timer5",)),
    ("Classic radio (2 / 5 / 1..6)", ("isr_bti_bt", "isr_bti_slot")),
    ("fault (7)", ("fm1_fatal_stubs",)),
)
MAIN_ROOTS = ("_start",)
# the recursions fm1-base's sources bound (levels at most, the first call counted): a function that calls itself
BOUNDS = {
    "sd_cmd": 2,                     # (device/sd.c: an ACMD sends CMD55 first, which is no ACMD)
    "fm1_ble_rf_exec": 2,            # (felucca-bsp chip/fm1_ble_rf.h: its sub-program only at depth 0)
}
MARGIN = (0.25, 1024)                # (a stack's size: the static worst and at least 25 % or 1 KiB, the larger)


def need(depth):
    """the bytes a stack needs for a static worst of depth: the margin on top"""
    return depth + max(int(depth * MARGIN[0] + 0.999), MARGIN[1])


def frame_of(text):
    """bytes one instruction pushes or reserves (0 none), -1: sp moved by a register (unbounded)"""
    m = PUSH.search(text)
    if m:
        n = 0
        for part in m.group(1).split(","):
            r = re.match(r"\s*r(\d+)-r(\d+)\s*$", part)
            n += 4 * (abs(int(r.group(1)) - int(r.group(2))) + 1) if r else 4
        return n
    if PUSH1.search(text):
        return 4
    m = SPSUB.match(text)
    if m:
        return int(m.group(1))
    if SPVAR.match(text):
        return -1
    return 0


STRADDLE = []                                      # (an instruction decoded across a table's end: the code after it unsure)


def parse(dis):
    """-> {name: {addr, frame, calls, indirect, refs, var}}; calls: (name, whole, addr) of each call and tail call"""
    fns, cur, name, prev, tab, last = {}, None, None, "", None, []
    for ln in dis.splitlines():
        m = FN.match(ln)
        if m:
            name = m.group(1)
            if name in fns:                      # (two static functions of one name: the second its own entry)
                name = f"{name}#{sum(1 for k in fns if k.split('#')[0] == name) + 1}"
            cur = {"addr": None, "frame": 0, "calls": set(), "indirect": False, "refs": set(), "var": False}
            fns[name] = cur
            prev = ""
            continue
        m = INS.match(ln)
        if not m or cur is None:
            continue
        a, text = int(m.group(1), 16), m.group(3).strip()
        raw = bytes(int(x, 16) for x in m.group(2).split())
        if tab is not None:                      # (a tbh / tbb table: data, not code, up to its nearest target)
            if a < tab["end"]:
                tab["bytes"] += raw
                w, bs = tab["unit"], tab["bytes"]
                while tab["k"] + w <= len(bs) and tab["start"] + tab["k"] < tab["end"]:
                    e = int.from_bytes(bs[tab["k"]:tab["k"] + w], "little")
                    if e:                        # (an entry: halfwords from the table to its target)
                        tab["end"] = min(tab["end"], tab["start"] + 2 * e)
                    tab["k"] += w
                if a + len(raw) > tab["end"]:
                    STRADDLE.append(f"{name} {a:x}")
                    tab = None
                continue
            tab = None
        if re.match(r"^tb[bh] \[r\d+\]", text):
            w = 2 if text.startswith("tbh") else 1
            n = next((int(x.group(1)) + 1 for t in reversed(last) for x in [re.match(r"^if \(r\d+ > (\d+)\)", t)] if x), None)
            st = a + len(raw)
            tab = {"start": st, "unit": w, "k": 0, "bytes": b"",
                   "end": st + (n * w + 1) // 2 * 2 if n else st + 0x20000}
        last = (last + [text])[-5:]
        if cur["addr"] is None:
            cur["addr"] = a
        f = frame_of(text)
        if f < 0:
            cur["var"] = True
        else:
            cur["frame"] += f
        c, g = CALL.match(text), GOTO.match(text)
        if c:
            cur["calls"].add((c.group(1), c.group(2) is None, int(c.group(3), 16)))
        elif g:
            if g.group(2) is None and g.group(1) != name.split("#")[0]:      # (a tail call: another function's first address)
                cur["calls"].add((g.group(1), True, int(g.group(3), 16)))
        elif CALLR.match(text):
            cur["indirect"] = "call"
        elif GOTOR.match(text) and not TABLE.search(prev):
            cur["indirect"] = cur["indirect"] or "goto"      # (a jump table, or an indirect tail call)
        else:
            cur["refs"].update(int(ad, 16) for _, ad in REF.findall(text))
        prev = text
    return fns


def elf_words(path):
    """every 4-aligned little-endian word of the ELF's allocated sections (the tables of function pointers)"""
    b = open(path, "rb").read()
    if b[:4] != b"\x7fELF" or b[4] != 1:
        raise SystemExit(f"stack_depth: {path}: not a 32-bit ELF")
    shoff, = struct.unpack_from("<I", b, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", b, 0x2E)
    words = set()
    for i in range(shnum):
        _, typ, flags, _, off, size = struct.unpack_from("<IIIIII", b, shoff + i * shentsize)
        if not flags & 2 or typ == 8:            # (allocated, with contents: not NOBITS)
            continue
        for o in range(off, off + size - 3, 4):
            words.add(struct.unpack_from("<I", b, o)[0])
    return words


HW_ONLY = re.compile(r"^(isr_|fm1_fatal_stubs$|fm1_cpu1_entry$|_start$)")   # (reached by the hardware only)


def analyse(dis, words, irs=None, bounds=None):
    """-> {main, irq, irq_total, taken, notes, depth}: dis the disassembly, words the ELF's, irs the optimised IR's
    texts (none: every indirect call reaches every function whose address the image holds)"""
    fns = parse(dis)
    start = {v["addr"]: k for k, v in fns.items() if v["addr"] is not None}
    taken = set()
    for v in fns.values():
        taken |= {start[a] for a in v["refs"] if a in start}
    taken |= {start[w] for w in words if w in start}
    taken = {t for t in taken if not HW_ONLY.match(t)}
    irt, stats = stack_ir.targets(irs) if irs else ({}, {"calls": 0, "any": 0})
    memo, notes = {}, {"recursion": set(), "unknown": set(), "var": set(), "indirect, all taken": set(),
                       "indirect, a jump table": set()}

    base = {}
    for k in fns:
        base.setdefault(k.split("#")[0], []).append(k)

    def targets(f):
        if f.split("#")[0] in irt:                # (the IR's names: each copy of a name its targets, every copy)
            return {x for g in irt[f.split("#")[0]] for x in base.get(g, [])}
        if fns[f]["indirect"] == "goto" and irs:
            notes["indirect, a jump table"].add(f)   # (its IR calls nothing indirectly: a switch's table)
            return set()
        notes["indirect, all taken"].add(f)
        return set(taken)

    def callees(f):
        out = set()
        for name, whole, addr in fns[f]["calls"]:
            if addr in start:                    # (by address: a name may be two functions')
                out.add(start[addr])
            elif name in fns and whole:
                out.add(name)
            else:
                notes["unknown"].add(f"{f} -> {name}")
        if fns[f]["indirect"]:
            out |= targets(f)
        return out

    sys.setrecursionlimit(100000)

    bounds = {**BOUNDS, **(bounds or {})}

    def depth(f, onpath):
        if f in onpath and f.split("#")[0] in bounds:
            return 0, []                         # (a bounded recursion: its levels counted at its first call)
        if f in onpath:
            notes["recursion"].add(f)
            return 0, [f + " (recursion)"]
        if f in memo:
            return memo[f]
        if fns[f]["var"]:
            notes["var"].add(f)
        best, path = 0, []
        p2 = onpath | {f}
        for c in callees(f):
            d, p = depth(c, p2)
            if d > best:
                best, path = d, p
        lv = bounds.get(f.split("#")[0], 1) if f in callees(f) else 1
        r = (fns[f]["frame"] * lv + best, [f if lv == 1 else f"{f} x{lv}"] + path)
        if not any(x.endswith("(recursion)") for x in path):
            memo[f] = r
        return r

    out = {"main": None, "irq": [], "taken": len(taken), "ir_calls": stats}
    roots = [r for r in MAIN_ROOTS if r in fns]
    out["main"] = max((depth(r, frozenset()) for r in roots), default=(0, []))
    tot = 0
    for lvl, stubs in ISR_LEVELS:
        d = max((depth(s, frozenset()) for s in stubs if s in fns), default=(0, []))
        out["irq"].append((lvl, d[0], d[1]))
        tot += d[0]
    out["irq_total"] = tot
    cyc = {}
    for f in notes["recursion"]:                 # (each recursion's shortest cycle, for the report)
        prev, todo = {f: None}, [f]
        while todo and f not in cyc:
            nxt = []
            for g in todo:
                for c in callees(g):
                    if c == f:
                        p, x = [f], g
                        while x is not None:
                            p.append(x)
                            x = prev[x]
                        cyc[f] = list(reversed(p))
                        break
                    if c not in prev:
                        prev[c] = g
                        nxt.append(c)
                if f in cyc:
                    break
            todo = nxt
    out["cycles"] = cyc
    out["notes"] = {k: sorted(v) for k, v in notes.items()}
    out["frames"] = {k: v["frame"] for k, v in fns.items()}
    out["depth"] = lambda f: depth(f, frozenset())
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dis")
    ap.add_argument("elf")
    ap.add_argument("--ir", action="append", default=[], help="a unit's LLVM IR (the indirect calls by signature)")
    ap.add_argument("--bound", action="append", default=[], help="FUNC=N: an app's recursion, N levels at most")
    ap.add_argument("--json")
    ap.add_argument("--fn", action="append", default=[], help="also print this function's depth and path")
    a = ap.parse_args(argv)
    r = analyse(open(a.dis, errors="replace").read(), elf_words(a.elf),
                [open(p, errors="replace").read() for p in a.ir], {k: int(v) for k, v in (x.split("=") for x in a.bound)})
    d, p = r["main"]
    print(f"main stack: {d:,} B  {' > '.join(p)}")
    for lvl, n, path in r["irq"]:
        print(f"  ssp {lvl:30s} {n:6,} B  {' > '.join(path)}")
    print(f"interrupt stack, nested: {r['irq_total']:,} B")
    print(f"address-taken functions (an indirect call's targets): {r['taken']}")
    st = r["ir_calls"]
    if st["calls"]:
        print(f"indirect calls in the IR: {st['calls']}, to any taken function (not followed): {st['any']} "
              f"{', '.join(sorted(set(st['any_in'])))}")
    if STRADDLE:
        print(f"tables' ends inside an instruction ({len(STRADDLE)}): {', '.join(STRADDLE[:20])}")
    for k, v in r["notes"].items():
        if v:
            print(f"{k}: {len(v)}: {', '.join(v[:40])}")
    for f, c in sorted(r["cycles"].items()):
        print(f"  cycle: {' > '.join(c)}")
    for f in a.fn:
        d, p = r["depth"](f)
        print(f"{f}: {d:,} B  {' > '.join(p)}")
    if a.json:
        with open(a.json, "w") as fh:
            json.dump({k: v for k, v in r.items() if k not in ("depth", "frames")}, fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
