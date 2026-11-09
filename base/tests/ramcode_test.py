#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""tools/ramcode.py, the check on an app's RAM code (.app_ram), on small disassemblies in objdump's pi32v2 form:
a RAM -> RAM call passes (to any depth, a tail call too), a call from RAM into XIP fails with the path to it, a call
through a pointer fails with the XIP code whose address it loads, fm1.toml ram_code_allow lets one through, a name
there that is no RAM function is an error, and nothing in .app_ram says nothing. Run from the repo root:
  python3 tests/ramcode_test.py"""
import importlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
ramcode = importlib.import_module("ramcode")

failed = 0


def ok(cond, what):
    global failed
    print(("  ok   " if cond else "  FAIL ") + what)
    failed += not cond


def syms(ar_end=0x1c00040):
    return "\n".join(f"{a:08x} g       .ram_text\t00000000 {n}" for a, n in (
        (0x1c00000, "_rt_start"), (0x1c00020, "_ar_start"), (ar_end, "_ar_end"), (0x1c00060, "_rt_end")))


def dis(body):
    """.ram_text: fl_wait (the flash driver's, no calls) at 0x1c00000, jl_ram (a radio library's RAM code that calls
    out, allowed in .ram_text in a radio build) at 0x1c00004; the app's RAM code from 0x1c00020; then XIP"""
    return f"""
Disassembly of section .text:

xip_helper:
 2000200:    00 2a             \tr0 = r0 + r0
 2000202:    81 00             \trts

Disassembly of section .ram_text:

fl_wait:
 1c00000:    00 2a             \tr0 = r0 + r0
 1c00002:    81 00             \trts

jl_ram:
 1c00004:    81 ea fc 00       \tcall 4194808 <xip_helper : 2000200 >
 1c00008:    81 00             \trts
{body}
"""


def run(body, allow=(), ar_end=0x1c00040):
    return ramcode.check(syms(ar_end), dis(body), allow)


# an audio render in RAM calling a RAM helper, which tail-calls (goto) the flash driver's RAM code: all RAM
RAM_TO_RAM = """
render:
 1c00020:    80 ea 08 00       \tcall 8 <mix : 1c0002c >
 1c00024:    04 81             \tgoto 2 <render+0x8 : 1c00028 >
 1c00026:    00 2a             \tr0 = r0 + r0
 1c00028:    81 00             \trts
 1c0002a:    00 2a             \tr0 = r0 + r0
mix:
 1c0002c:    bf ea e8 ff       \tgoto -48 <fl_wait : 1c00000 >
"""
errs, notes = run(RAM_TO_RAM)
ok(errs == [], f"RAM -> RAM calls pass ({errs})")
ok(any("2 functions, 3 with the RAM code they call" in n for n in notes), f"the note counts them ({notes})")

# the same, but the helper calls XIP code
RAM_TO_XIP = RAM_TO_RAM.replace("goto -48 <fl_wait : 1c00000 >", "call 1048528 <xip_helper : 2000200 >")
errs, _ = run(RAM_TO_XIP)
ok(len(errs) == 1 and "RAM code calls XIP code: mix at 0x1c0002c -> xip_helper (0x2000200)" in errs[0],
   f"RAM -> XIP fails with the path ({errs})")

# to any depth: RAM code that is not the app's (jl_ram), called from the app's, is followed into XIP
DEEP = RAM_TO_RAM.replace("goto -48 <fl_wait : 1c00000 >", "goto -40 <jl_ram : 1c00004 >")
errs, _ = run(DEEP)
ok(len(errs) == 1 and "RAM code calls XIP code: mix > jl_ram at 0x1c00004 -> xip_helper" in errs[0],
   f"RAM -> RAM -> XIP fails with the path ({errs})")

# the conditional form: a bare 'call N', its target the address + N + the instruction's length
BARE = RAM_TO_RAM.replace("goto -48 <fl_wait : 1c00000 >", "call 4194768")
errs, _ = run(BARE)
ok(len(errs) == 1 and "-> xip_helper (0x2000200)" in errs[0], f"a bare call's target is computed ({errs})")

# through a pointer: the function loads an XIP function's address
PTR = """
render:
 1c00020:    c0 ff 00 02 00 02 \tr0 = 33554944 <xip_helper : 2000200 >
 1c00026:    c0 00             \tcall r0
 1c00028:    81 00             \trts
"""
errs, _ = run(PTR)
ok(len(errs) == 1 and "calls through a pointer: render at 0x1c00026" in errs[0] and "xip_helper" in errs[0],
   f"a pointer call fails, naming the XIP code it loads ({errs})")
errs, notes = run(PTR, allow=["render"])
ok(errs == [] and any("allowed in render" in n for n in notes), f"ram_code_allow lets it through, said ({errs}, {notes})")
errs, _ = run(RAM_TO_RAM, allow=["xip_helper", "nothing"])
ok(len(errs) == 2 and all("is no function in RAM code" in e for e in errs), f"a stale allow-list fails ({errs})")
errs, notes = run(PTR, allow=["render?", "nothing?"])
ok(errs == [] and any("allowed in render" in n for n in notes) and any("not in this build: nothing" in n for n in notes),
   f"an optional name: allowed when built, said when not ({errs}, {notes})")
errs, notes = run(RAM_TO_RAM, allow=["xip_helper?"])
ok(errs == [] and any("in XIP in this build (not RAM code): xip_helper" in n for n in notes),
   f"an optional name built in XIP (its RAM placement off in this build) is said in a note, not an error ({errs}, {notes})")
errs, _ = run(RAM_TO_RAM, allow=["xip_helper"])
ok(len(errs) == 1 and "xip_helper is no function in RAM code" in errs[0], f"a plain name built in XIP still fails ({errs})")

# the flash driver's .ram_text is no root: its code is only followed when RAM code calls it
errs, notes = run(RAM_TO_RAM, ar_end=0x1c00020)
ok(errs == [] and notes == [], f"no .app_ram: nothing checked, nothing said ({errs}, {notes})")

print("ramcode: " + ("FAILED" if failed else "all passed"))
sys.exit(1 if failed else 0)
