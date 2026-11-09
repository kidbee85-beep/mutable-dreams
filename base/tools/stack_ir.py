# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""The indirect calls' targets, from the units' optimised LLVM IR (fm1-base's tools/build.py with FM1B_OPT_IR=1:
unity.opt.ll and o2/*.opt.ll, the backend's own input, so its functions and calls are the disassembly's): for
tools/stack_depth.py. A field-based flow of the functions' addresses:
  keys   ("G", g)      stored in global g (its initializer, or a store into it)
         ("O", n)      stored at byte n of a struct (a struct-path TBAA tag's offset, or a struct initializer's field at
                       n from any enclosing struct's start: the name is not compared, anonymous structs have none)
         ("P", f, i)   passed as f's parameter i;  ("R", f): returned by f
  a store of a function's address (or of a value traced to one) adds it to the store's keys; a load of a pointer reads
  its keys; a call through it reaches what its keys hold. What cannot be followed stays sound:
         a store through a pointer with no struct path (a scalar 'any pointer' tag) and not into a global, an address
         passed to a function with no body here (asm, the ROM), or used in any other way: "escaped", in every key;
         a callee loaded from such memory, or made any other way: "any", every function whose address is taken.
  Each call's targets are those with its signature (the return and argument types; opaque pointers all alike)."""
import re

DEF = re.compile(r'^define [^@]*?(\S+) @"?([\w.$]+)"?\((.*)\)[^(]*\{$')
NAMED = re.compile(r"^%([\w.$\"]+) = type (.*)$")
GLOBAL = re.compile(r'^@"?([\w.$]+)"? = (.*)$')
TNAME = re.compile(r'^!(\d+) = !\{!"([^"]*)"')
TAG = re.compile(r"^!(\d+) = !\{!(\d+), !(\d+), i64 (\d+)(?:, .*)?\}$")
ASSIGN = re.compile(r"^\s*(%[\w.$]+) = (.*)$")
FREF = re.compile(r'@"?([\w.$]+)"?')
ICALL = re.compile(r"\b(?:call|invoke) (?:[a-z_]+ )*(\S+?)(?: (\([^)]*\)))? (%[\w.$]+)\((.*)\)")
DCALL = re.compile(r'\b(?:call|invoke) (?:[a-z_]+ )*(\S+?)(?: (\([^)]*\)))? @"?([\w.$]+)"?\((.*)\)')
CALL_ATTR = re.compile(r"\b(?:range|nofpclass|dereferenceable|dereferenceable_or_null|align|captures|memory)\([^)]*\)\s*")
TBAA = re.compile(r"!tbaa !(\d+)")


# ---- types and constants (for the globals' initializers)
def split_top(s):
    out, d, cur, q = [], 0, "", False
    for ch in s:
        if ch == '"':
            q = not q
        if not q:
            if ch in "([{<":
                d += 1
            elif ch in ")]}>":
                d -= 1
        if ch == "," and d == 0 and not q:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def take_type(s):
    """-> (type text, rest) of the type at s's start"""
    s = s.lstrip()
    if s[:1] in "[{<":
        close = {"[": "]", "{": "}", "<": ">"}[s[0]]
        if s.startswith("<{"):
            close = "}>"
        d, i = 0, 0
        while i < len(s):
            if s[i] in "[{<(":
                d += 1
            elif s[i] in "]}>)":
                d -= 1
                if d == 0:
                    if close == "}>" and s[i] == "}":
                        i += 1
                    return s[:i + 1], s[i + 1:]
            i += 1
        raise ValueError(s[:60])
    m = re.match(r'(%"[^"]*"|[%\w.$]+)(\*?)', s)
    return m.group(0), s[m.end():]


class Layout:
    def __init__(self, named):
        self.named, self.memo = named, {}

    def fields(self, t):
        t = t.strip()
        if t.startswith("<{"):
            return split_top(t[2:-2]), True
        if t.startswith("{"):
            return split_top(t[1:-1]), False
        raise ValueError(t)

    def size_align(self, t):
        t = t.strip()
        if t in self.memo:
            return self.memo[t]
        if t.startswith("%"):
            r = self.size_align(self.named[t[1:]])
        elif t == "ptr" or t.endswith("*"):
            r = (4, 4)
        elif re.fullmatch(r"i\d+", t):
            n = (int(t[1:]) + 7) // 8
            r = (n, 4 if t == "i1" else min(4, 1 if n == 1 else 2 if n == 2 else 4))
        elif t in ("float",):
            r = (4, 4)
        elif t in ("double",):
            r = (8, 4)
        elif t == "half":
            r = (2, 2)
        elif t.startswith("["):
            n, e = re.match(r"\[(\d+) x (.*)\]$", t).groups()
            es, ea = self.size_align(e)
            r = (int(n) * es, ea)
        elif t.startswith("<") and not t.startswith("<{"):
            n, e = re.match(r"<(\d+) x (.*)>$", t).groups()
            es, _ = self.size_align(e)
            r = (int(n) * es, int(n) * es)
        else:
            fs, packed = self.fields(t)
            off, al = 0, 1
            for f in fs:
                s, a = self.size_align(f)
                a = 1 if packed else a
                off = (off + a - 1) // a * a + s
                al = max(al, a)
            r = ((off + al - 1) // al * al, al)
        self.memo[t] = r
        return r

    def offsets(self, t):
        fs, packed = self.fields(self.named[t.strip()[1:]] if t.strip().startswith("%") else t)
        out, off = [], 0
        for f in fs:
            s, a = self.size_align(f)
            a = 1 if packed else a
            off = (off + a - 1) // a * a
            out.append((f, off))
            off += s
        return out


def walk_init(lay, t, v, base, starts, out):
    """the functions' addresses in constant v of type t at byte base: out += (name, base, [enclosing struct starts])"""
    t, v = t.strip(), v.strip()
    if v in ("zeroinitializer", "undef", "poison", "null", "none") or v[:1].isdigit() or v[:1] == "-" or v.startswith(
            ("c\"", "true", "false", "0x")):
        return
    tt = lay.named.get(t[1:], t) if t.startswith("%") else t
    nm = re.sub(r"^(?:struct|union)\.|\.\d+$", "", t[1:]) if t.startswith("%") else ""
    if tt.startswith("[") and v.startswith("["):
        e = re.match(r"\[(\d+) x (.*)\]$", tt).group(2)
        es, _ = lay.size_align(e)
        for k, item in enumerate(split_top(v[1:-1])):
            it, iv = take_type(item)
            walk_init(lay, it, iv, base + k * es, starts, out)
        return
    if (tt.startswith("{") or tt.startswith("<{")) and v[:1] in "{<":
        body = v[2:-2] if v.startswith("<{") else v[1:-1]
        for (ft, off), item in zip(lay.offsets(tt), split_top(body)):
            it, iv = take_type(item)
            walk_init(lay, it, iv, base + off, starts + [(base, nm)], out)
        return
    for name in FREF.findall(v):                 # (a pointer: an address, or a constant expression of one)
        out.append((name, base, starts))


# ---- the flow
class Flow:
    def __init__(self):
        self.funcs = {}                          # name -> {sig, icalls: [(sig, keys)]}
        self.direct = {}                         # key -> {function}
        self.edges = {}                          # key -> {key}: key's functions include the other's
        self.escaped, self.taken = set(), set()
        self.pa_any = set()                      # (a parameter's memory some caller's pointer is not followed to)

    def add(self, key, fn):
        self.direct.setdefault(key, set()).add(fn)

    def link(self, dst, src):
        self.edges.setdefault(dst, set()).add(src)

    def solve(self):
        memo = {}
        for k in set(self.direct) | set(self.edges):
            seen, todo, out = set(), [k], set()
            while todo:
                x = todo.pop()
                if x in seen:
                    continue
                seen.add(x)
                out |= self.direct.get(x, set())
                todo.extend(self.edges.get(x, ()))
            memo[k] = out
        return memo


def ir_types(args):
    out = []
    for x in split_top(args):
        if x and x != "...":
            out.append(take_type(x)[0])
        elif x == "...":
            out.append("...")
    return tuple(out)


def module(text, flow):
    lines = text.splitlines()
    named, tags, defs, tnames = {}, {}, set(), {}
    for ln in lines:
        m = NAMED.match(ln)
        if m:
            named[m.group(1)] = m.group(2).strip()
        m = TAG.match(ln)
        if m:
            tags[m.group(1)] = (m.group(2), m.group(3), int(m.group(4)))
        m = TNAME.match(ln)
        if m:
            tnames[m.group(1)] = m.group(2)
        m = DEF.match(ln)
        if m:
            defs.add(m.group(2))
    lay = Layout(named)
    for k, (b, a, o) in list(tags.items()):
        tags[k] = (b, a, o, tnames.get(b, ""), tnames.get(a, ""))
    fnames = defs | {m.group(1) for ln in lines if ln.startswith("declare ")
                     for m in [re.search(r'@"?([\w.$]+)"?\(', ln)] if m}
    bodies = {}
    for ln in lines:
        m = GLOBAL.match(ln)
        if m and not ln.startswith("@llvm."):
            g, rest = m.group(1), m.group(2)
            mm = re.match(r"(?:[a-z_]+(?:\([^)]*\))? )*(?:global|constant) (.*)$", rest)
            if not mm:
                continue
            try:
                t, v = take_type(mm.group(1))
                v = re.split(r", (?:section|align|comdat|partition|!)", v, maxsplit=1)[0]
                found = []
                walk_init(lay, t, v, 0, [], found)
            except (ValueError, KeyError, AttributeError):
                found = [(n, None, []) for n in FREF.findall(mm.group(1))]
            for name, off, starts in found:
                if name not in fnames:
                    continue
                flow.taken.add(name)
                flow.add(("G", g), name)
                if off is None:
                    flow.escaped.add(name)
                for st, nm in starts:
                    flow.add(("ON", nm, off - st) if nm else ("O?", off - st), name)
    cur, defs_of = None, {}
    for ln in lines:
        m = DEF.match(ln)
        if m:
            cur = m.group(2)
            if cur in flow.funcs:                # (a static of the same name in another unit: its own entry)
                cur = f"{cur}#{sum(1 for k in flow.funcs if k.split('#')[0] == cur) + 1}"
            flow.funcs[cur] = {"sig": (m.group(1), ir_types(m.group(3))), "icalls": []}
            params = [x.split()[-1] for x in split_top(m.group(3)) if x and x != "..."]
            defs_of = {p: ("param", i) for i, p in enumerate(params)}
            defs_of[" fn"] = cur
            bodies[cur] = []
            continue
        if cur is None:
            continue
        if ln.startswith("}"):
            body(flow, cur, bodies[cur], defs_of, tags, fnames, defs)
            cur = None
            continue
        a = ASSIGN.match(ln)
        if a:
            defs_of[a.group(1)] = ("ins", a.group(2))
        bodies[cur].append(ln)


ARG_ATTRS = re.compile(r"^(?:(?:noundef|nonnull|readonly|writeonly|readnone|nocapture|signext|zeroext|inreg|noalias|returned|"
                       r"nofree|immarg|noundef|byval|sret|nest|swiftself|allocalign|allocptr)\b\s*(?:\([^)]*\))?\s*)*")
GEP = re.compile(r"^getelementptr (?:inbounds )?(?:nuw )?(?:nusw )?")


def bases(v, defs_of, seen=None):
    """what address v points into, through its GEPs, phis and selects: {("G", g) a global, ("A", i) the memory the
    function's parameter i points to, ("L", a) a local (alloca a), ("?",) anything else}"""
    v = v.strip()
    m = re.match(r'^@"?([\w.$]+)"?$', v)
    if m:
        return {("G", m.group(1))}
    m = re.match(r"^getelementptr [^(]*\((?:[^,]+), ptr @\"?([\w.$]+)\"?", v)
    if m:
        return {("G", m.group(1))}
    seen = set() if seen is None else seen
    if v in seen:
        return set()
    seen.add(v)
    d = defs_of.get(v)
    if not d:
        return {("?",)}
    if d[0] == "param":
        return {("A", d[1])}
    ins = d[1]
    if ins.startswith("alloca "):
        return {("L", v)}
    if ins.startswith("getelementptr"):
        ops = split_top(GEP.sub("", ins))
        return bases(take_type(ops[1])[1], defs_of, seen) if len(ops) > 1 else {("?",)}
    if ins.startswith("phi "):
        out = set()
        for x in re.findall(r"\[\s*([^,\]]+),", ins):
            out |= bases(x, defs_of, seen)
        return out
    if ins.startswith("select "):
        ops = split_top(ins[7:])
        out = set()
        for x in ops[1:3]:
            out |= bases(take_type(x)[1], defs_of, seen)
        return out
    return {("?",)}


def mem_keys(addr, tag, defs_of, tags, load=False):
    """the keys of a pointer stored at or loaded from addr (tag: its !tbaa); a load from memory not followed (no
    struct path, some base unknown) has ("ANY",) among them; a store there has none of its own (escaped)"""
    keys, f = set(), defs_of.get(" fn", "").split("#")[0]
    t = tags.get(tag) if tag else None
    struct = bool(t and t[0] != t[1])
    for b in bases(addr, defs_of):
        if b[0] == "G":
            keys.add(("G", b[1]))
        elif b[0] == "L":
            keys.add(("L", defs_of.get(" fn"), b[1]))   # (a local: the far call's 'void (*volatile f)() = fn')
        elif b[0] == "A" and load and not struct:
            keys.add(("PA", f, b[1]))            # (what the callers pass it: the globals' arrays, ED_CMDS)
        elif not struct and load:
            keys.add(("ANY",))
    if struct:                                   # (a struct path: named, or anonymous "")
        keys.add(("ON", t[3], t[2]) if t[3] else ("O?", t[2]))
    return keys


def trace(v, f, defs_of, tags, fnames, depth=0):
    """the keys a pointer value may be (a function's own name: ("F", name)); ("ANY",): not followed. Each value
    once per function (a phi's loop: what it adds is there already)"""
    v = v.strip()
    memo = defs_of.setdefault(" memo", {})
    if v in memo:
        return memo[v]
    memo[v] = set()
    memo[v] = r = trace1(v, f, defs_of, tags, fnames, depth)
    return r


def trace1(v, f, defs_of, tags, fnames, depth):
    m = re.match(r'^@"?([\w.$]+)"?$', v)
    if m:
        return {("F", m.group(1))} if m.group(1) in fnames else set()
    if v in ("null", "undef", "poison") or not v.startswith("%"):
        return set() if not v.startswith(("bitcast", "inttoptr", "getelementptr", "select")) else {("ANY",)}
    d = defs_of.get(v)
    if d is None or depth > 16:
        return {("ANY",)}
    if d[0] == "param":
        return {("P", f.split("#")[0], d[1])}
    ins = d[1]
    if ins.startswith("load "):
        ops = split_top(ins[5:])
        if len(ops) < 2:
            return {("ANY",)}
        addr = take_type(ops[1])[1]
        tm = TBAA.search(ins)
        acc = tags.get(tm.group(1), (0, 0, 0, "", ""))[4] if tm else ""
        if re.match(r"^p\d ", acc):
            return set()                         # (a data pointer's access, 'p1 int': no function's address)
        k = mem_keys(addr, tm.group(1) if tm else None, defs_of, tags, load=True)
        k = {("O*", x[1]) if x[0] == "O?" else x for x in k} | {("O?", x[2]) for x in k if x[0] == "ON"}
        return k or {("ANY",)}
    if ins.startswith("phi "):
        out = set()
        for x in re.findall(r"\[\s*([^,\]]+),", ins):
            out |= trace(x, f, defs_of, tags, fnames, depth + 1)
        return out
    if ins.startswith("select "):
        ops = split_top(ins[7:])
        out = set()
        for x in ops[1:3]:
            out |= trace(take_type(x)[1], f, defs_of, tags, fnames, depth + 1)
        return out
    dc = DCALL.search(CALL_ATTR.sub("", ins))
    if dc and not dc.group(3).startswith("llvm."):
        return {("R", dc.group(3))}
    return {("ANY",)}


def body(flow, f, lines, defs_of, tags, fnames, defs):
    for ln in lines:
        handled = set()
        ln = CALL_ATTR.sub("", ln) if "call " in ln or "invoke " in ln else ln
        ic = ICALL.search(ln)
        dc = DCALL.search(ln)
        if ic and not dc:
            ret = ic.group(1)
            sig = (ret, ir_types(ic.group(2)[1:-1]) if ic.group(2) else ir_types(ic.group(4)))
            ck = trace(ic.group(3), f, defs_of, tags, fnames)
            flow.funcs[f]["icalls"].append((sig, ck or {("ANY",)}))   # (nothing followed to it: any)
            n_ic = len(flow.funcs[f]["icalls"]) - 1
            for i, a in enumerate(split_top(ic.group(4))):
                t, v = take_type(a) if a else ("", "")
                v = ARG_ATTRS.sub("", v.strip())
                if t != "ptr":
                    continue
                flow.funcs[f].setdefault("iargs", {}).setdefault(n_ic, set()).add(i)
                for k in trace(v, f, defs_of, tags, fnames):
                    if k[0] == "F":
                        flow.add(("IA", f, n_ic, i), k[1])
                        flow.taken.add(k[1])
                        handled.add(k[1])
                    elif k != ("ANY",):
                        flow.link(("IA", f, n_ic, i), k)
        if dc:
            callee = dc.group(3)
            handled.add(callee)
            for i, a in enumerate(split_top(dc.group(4))):
                t, v = take_type(a) if a else ("", "")
                v = ARG_ATTRS.sub("", v.strip())
                if t != "ptr":
                    continue
                for b in bases(v, defs_of):      # (the memory it points to: the callee's PA)
                    pa = ("PA", callee.split("#")[0], i)
                    if b[0] == "G":
                        flow.link(pa, ("G", b[1]))
                    elif b[0] == "A":
                        flow.link(pa, ("PA", f.split("#")[0], b[1]))
                    elif b[0] == "L":
                        flow.link(pa, ("L", f, b[1]))
                    else:
                        flow.pa_any.add(pa)
                src = trace(v, f, defs_of, tags, fnames)
                fl = {k for k in src if k[0] == "F"}
                for k in fl:
                    flow.taken.add(k[1])
                    handled.add(k[1])
                if callee.startswith("llvm.") and not callee.startswith(("llvm.memcpy", "llvm.memmove")):
                    continue
                for k in src:                    # (a callee with no body in any unit: targets() escapes it)
                    if k[0] == "F":
                        flow.add(("P", callee, i), k[1])
                    elif k != ("ANY",):
                        flow.link(("P", callee, i), k)
        s = re.match(r"^\s*store (?:volatile )?ptr (.+?), ptr (.+?)(?:, align \d+)?(?:, !tbaa !(\d+))?(?:, !.*)?$", ln)
        acc = tags.get(s.group(3), (0, 0, 0, "", ""))[4] if s and s.group(3) else ""
        if s and not re.match(r"^p\d ", acc):     # (a data pointer's store, 'p1 int': no function's address)
            src = trace(s.group(1), f, defs_of, tags, fnames)
            dst = mem_keys(s.group(2), s.group(3), defs_of, tags)
            for k in src:
                if k[0] == "F":
                    flow.taken.add(k[1])
                    handled.add(k[1])
                for d in dst:
                    if k[0] == "F":
                        flow.add(d, k[1])
                    elif k != ("ANY",):
                        flow.link(d, k)
                if not dst:
                    if k[0] == "F":
                        flow.escaped.add(k[1])
                    elif k != ("ANY",):
                        flow.link(("ESC",), k)
        r = re.match(r"^\s*ret ptr (.+)$", ln)
        if r:
            for k in trace(r.group(1), f, defs_of, tags, fnames):
                if k[0] == "F":
                    flow.add(("R", f.split("#")[0]), k[1])
                    flow.taken.add(k[1])
                    handled.add(k[1])
                elif k != ("ANY",):
                    flow.link(("R", f.split("#")[0]), k)
        a = ASSIGN.match(ln)
        if a and re.match(r"(phi|select) ", a.group(2)):
            for n in FREF.findall(a.group(2)):
                handled.add(n)                   # (followed by trace where it is used)
                if n in fnames:
                    flow.taken.add(n)
        for n in FREF.findall(ln):
            if n in fnames and n not in handled:
                flow.escaped.add(n)              # (used some other way: ptrtoint, a vector, an aggregate)
                flow.taken.add(n)


def sig_match(isig, sig):
    if isig[0] != sig[0]:
        return False
    if isig[1] and isig[1][-1] == "...":
        return sig[1][:len(isig[1]) - 1] == isig[1][:-1]
    return isig[1] == sig[1]


def targets(texts):
    """-> ({function: set of the functions its indirect calls may reach} for every function with one, stats).
    An indirect call's arguments reach its targets' parameters: solved again until no call finds a new target"""
    flow = Flow()
    for t in texts:
        module(t, flow)
    keys = set(flow.direct) | set(flow.edges) | {x for v in flow.edges.values() for x in v}
    for k in keys:                               # (O*: every struct's field at that offset)
        if k[0] in ("ON", "O?"):
            flow.link(("O*", k[-1]), k)
    for k in keys:                               # (an address passed to code with no body here: anywhere)
        if k[0] == "P" and k[1] not in flow.funcs:
            flow.link(("ESC",), k)
    done = set()
    iarg_pa = set()                              # (a parameter an indirect call reaches: its memory not followed)
    while True:
        sets = flow.solve()
        esc = set(flow.escaped) | sets.get(("ESC",), set())
        out, stats, new = {}, {"calls": 0, "any": 0, "any_in": []}, False
        for f, info in flow.funcs.items():
            if not info["icalls"]:
                continue
            reach = set()
            for n, (sig, keys) in enumerate(info["icalls"]):
                stats["calls"] += 1
                if ("ANY",) in keys or any(k[0] == "R" and k[1] not in flow.funcs or k[0] == "PA" and (
                    k in flow.pa_any or (k[1], k[2]) in iarg_pa) for k in keys):
                    stats["any"] += 1
                    stats["any_in"].append(f)
                    cand = flow.taken
                else:
                    cand = set(esc)
                    for k in keys:
                        cand |= sets.get(k, set())
                hit = {g for g in cand if g in flow.funcs and sig_match(sig, flow.funcs[g]["sig"])}
                reach |= hit
                for g in hit:
                    for i in info.get("iargs", {}).get(n, ()):
                        if (g.split("#")[0], i) not in iarg_pa:
                            iarg_pa.add((g.split("#")[0], i))
                            new = True
                        if (f, n, g, i) not in done:
                            done.add((f, n, g, i))
                            flow.link(("P", g.split("#")[0], i), ("IA", f, n, i))
                            new = True
            out.setdefault(f.split("#")[0], set()).update(g.split("#")[0] for g in reach)
        if not new:
            return out, stats
