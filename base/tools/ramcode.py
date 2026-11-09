# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""The check on an app's own RAM code (section .app_ram: plat.h PLAT_RAM_CODE), over the image's disassembly.

.ram_text is code that runs while the flash (XIP) is off: the flash driver. It calls nothing (tools/build.py check):
a call could land in XIP. .app_ram is the app's code that runs from RAM for speed, out of the XIP cache's way (an
audio path), with the flash on; it may call, but only code that is in RAM too, so no XIP instruction runs under it:

  - a call or a jump (a tail call) from it goes to RAM code: .app_ram, or the rest of .ram_text, whose own calls are
    followed in turn (the RAM code it reaches calls only RAM code, to any depth);
  - a call through a pointer ('call rN', 'goto rN') is one the build cannot follow: refused, with the XIP functions
    whose addresses that function loads (the likely targets: a pointer to XIP code is a call into XIP);
  - fm1.toml ram_code_allow = ["fn", ...] names the RAM functions whose calls out of RAM, or through a pointer, the
    app vouches for (a cold path, a pointer that only ever holds RAM code): each is printed with every build, and a
    name that matches no function in RAM is an error (the list stays what the code is). "fn?" names one an app's
    switch may leave out, or a build may place in XIP (its RAM placement switched off): absent or in XIP it is said in a
    note (XIP code makes no call out of RAM to vouch for); a plain "fn" must be RAM code.

Constants are not checked: .app_ram code may read .rodata in XIP, so it does not run with the flash off (nothing
but the flash driver does: its windows hold the interrupts off).

The disassembly is objdump's (pi32v2): a function starts at a line 'name:', an instruction is 'ADDR: BYTES \\tTEXT'.
A direct call or jump is 'call N <sym : ADDR >' / 'goto N <...>' (also 'if (...) goto N <...>'), or, in the
conditional form whose label objdump prints on the next line, a bare 'call N' (the target is then the
instruction's address + N + its length).
"""
import bisect
import re

LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")
LABEL = re.compile(r"^([A-Za-z_$][\w.$]*):\s*$")
DIRECT = re.compile(r"\b(?:call|goto) (-?\d+)(?: <[^>]*: ([0-9a-f]+) >)?")
INDIRECT = re.compile(r"\b(?:call|goto) r\d+\b")
ADDR_LOAD = re.compile(r"= -?\d+ <([A-Za-z_$][\w.$]*) : ([0-9a-f]+) >")


def symbol(syms, name):
    """an `objdump -t` symbol's value, None if absent"""
    m = re.search(r"^([0-9a-f]+) .*\s" + re.escape(name) + r"$", syms, re.M)
    return int(m.group(1), 16) if m else None


def functions(dis):
    """-> (starts, names, insns): the functions' start addresses (sorted), their names, and each one's instructions
    as (pc, length, text)"""
    found, cur = {}, None
    pending = None
    for ln in dis.splitlines():
        m = LABEL.match(ln.strip())
        if m:
            pending = m.group(1)
            continue
        mm = LINE.match(ln)
        if not mm:
            continue
        pc, size, text = int(mm.group(1), 16), len(mm.group(2).split()), mm.group(3)
        if pending is not None:
            cur = found.setdefault(pc, (pending, []))
            pending = None
        if cur is not None:
            cur[1].append((pc, size, text))
    starts = sorted(found)
    return starts, [found[a][0] for a in starts], [found[a][1] for a in starts]


def check(syms, dis, allow=()):
    """-> (errors, notes) for the code between _ar_start and _ar_end (none there: nothing to say)"""
    ar = symbol(syms, "_ar_start"), symbol(syms, "_ar_end")
    rt = symbol(syms, "_rt_start"), symbol(syms, "_rt_end")
    if None in ar or None in rt or ar[1] <= ar[0]:
        return [], []
    starts, names, insns = functions(dis)
    by_name = {n: i for i, n in enumerate(names)}
    optional = {n[:-1] for n in allow if n.endswith("?")}
    absent = sorted(n for n in optional if n not in by_name)       # ("fn?": not built here, the switch off)
    allow = [n.rstrip("?") for n in allow if n.rstrip("?") not in absent]

    def in_ram(a):
        return rt[0] <= a < rt[1]

    def owner(a):
        i = bisect.bisect_right(starts, a) - 1
        return i if i >= 0 else None

    def edges(i):
        """a function's calls and jumps that leave it: [(pc, target or None for a pointer, text)]"""
        lo, hi = starts[i], (starts[i + 1] if i + 1 < len(starts) else 1 << 32)
        out = []
        for pc, size, text in insns[i]:
            if INDIRECT.search(text):
                out.append((pc, None, text))
                continue
            m = DIRECT.search(text)
            if m:
                t = int(m.group(2), 16) if m.group(2) else (pc + int(m.group(1)) + size) & 0xFFFFFFFF
                if not lo <= t < hi:
                    out.append((pc, t, text))
        return out

    def xip_fns_loaded(i):
        return sorted({n for _, _, text in insns[i] for n, a in ADDR_LOAD.findall(text)
                       if not in_ram(int(a, 16)) and n in by_name and starts[by_name[n]] == int(a, 16)})

    def at(pcs):
        return ", ".join(f"{pc:#x}" for pc in pcs[:3]) + (f" (+{len(pcs) - 3} more)" if len(pcs) > 3 else "")

    errors, used = [], set()
    roots = [i for i, a in enumerate(starts) if ar[0] <= a < ar[1]]
    seen, path_of = set(roots), {i: [names[i]] for i in roots}
    todo = list(roots)
    while todo:
        i = todo.pop()
        path = path_of[i]
        ptr, xip = [], {}                     # this function's calls through a pointer; into XIP, by target
        for pc, t, text in edges(i):
            if names[i] in allow and (t is None or not in_ram(t)):
                used.add(names[i])
            elif t is None:
                ptr.append(pc)
            elif not in_ram(t):
                xip.setdefault(t, []).append(pc)
            else:
                j = owner(t)
                if j is not None and j not in seen:
                    seen.add(j)
                    path_of[j] = path + [names[j]]
                    todo.append(j)
        for t, pcs in sorted(xip.items()):
            j = owner(t)
            errors.append(f"RAM code calls XIP code: {' > '.join(path)} at {at(pcs)} -> "
                          f"{names[j] if j is not None else '?'} ({t:#x})")
        if ptr:
            xf = xip_fns_loaded(i)
            errors.append(f"RAM code calls through a pointer: {' > '.join(path)} at {at(ptr)}"
                          + (f"; it takes the address of XIP code {', '.join(xf)}" if xf else "; the target is not known")
                          + " (fm1.toml ram_code_allow if it is RAM code)")
    in_xip = []
    for n in allow:
        if n not in by_name or not in_ram(starts[by_name[n]]):
            if n in optional and n in by_name:
                in_xip.append(n)                    # ("fn?" built in XIP here: nothing out of RAM to vouch for)
            else:
                errors.append(f"fm1.toml ram_code_allow: {n} is no function in RAM code")
    allowed = f"; calls out of RAM or through a pointer allowed in {', '.join(sorted(used))} (ram_code_allow)" \
        if used else ""
    notes = [f"app RAM code (.app_ram): {ar[1] - ar[0]} B, {len(roots)} functions, {len(seen)} with the RAM code they "
             f"call (.ram_text {rt[1] - rt[0]} B in all){allowed}"]
    if absent:
        notes.append(f"ram_code_allow: not in this build: {', '.join(absent)}")
    if in_xip:
        notes.append(f"ram_code_allow: in XIP in this build (not RAM code): {', '.join(sorted(in_xip))}")
    return errors, notes
