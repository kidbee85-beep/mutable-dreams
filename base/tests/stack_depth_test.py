#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""tools/stack_depth.py and stack_ir.py, the static stack check (fm1.toml stack_check), on a made-up disassembly and IR:
the frames, the calls by address, a tail call, a switch's table as data, the interrupts nested a level each with the
fault path on top, a bounded and an unbounded recursion, and the indirect calls resolved through a struct's field, a
global, a local (the far call), a parameter, an indirect call's argument, with the fallbacks that keep it sound; and
tools/build.py's linker rewrite for fm1.toml main_stack / irq_stack. Run from the repo root:
  python3 tests/stack_depth_test.py"""
import importlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sd = importlib.import_module("stack_depth")
build = importlib.import_module("build")

failed = 0


def check(what, ok):
    global failed
    print(("ok    " if ok else "FAIL  ") + what)
    failed += not ok


def dis(funcs):
    """{name: [instruction or (bytes, instruction), ...]} -> objdump -d's text, 4 bytes an instruction"""
    out, pc, at = [], 0x2000000, {}
    for f, lines in funcs.items():
        at[f] = pc
        pc += 4 * (len(lines) + 1) + 0x100
    pc = 0x2000000
    for f, lines in funcs.items():
        out.append(f + ":")
        for ln in lines + ["rts"]:
            raw, text = ln if isinstance(ln, tuple) else ("00 00 00 00", ln)
            for name, a in at.items():
                text = text.replace(f"<{name}>", f"<{name} : {a:x} >")
            out.append(f" {pc:x}:    {raw} \t{text}")
            pc += len(raw.split())
        pc = at[f] + 4 * (len(lines) + 1) + 0x100 if f != list(funcs)[-1] else pc
    return "\n".join(out) + "\n"


ISR = ["[--sp] = {psr, rets, reti}", "[--sp] = {r3-r0}"]
BASE = {
    "_start": ["call 4 <main>"],
    "main": ["[--sp] = {rets, r7-r4}", "sp += -100", "call 4 <ui>", "call 4 <deep>"],
    "ui": ["[--sp] = {rets}", "sp += -40"],
    "deep": ["[--sp] = {rets, r4}", "sp += -1000", "goto 4 <leaf>"],
    "leaf": ["sp += -12"],
    "isr_ble_event": ISR + ["call 4 <ble_irq>"], "ble_irq": ["[--sp] = {rets}", "sp += -200"],
    "isr_alnk0": ISR + ["call 4 <audio>"], "audio": ["[--sp] = {rets, r15-r4}", "sp += -300"],
    "isr_timer5": ISR + ["call 4 <tick>"], "tick": ["sp += -20"],
    "isr_bti_bt": ISR + ["call 4 <bti>"], "bti": ["sp += -8"],
    "fm1_fatal_stubs": ["[--sp] = {rets}", "call 4 <fault>"], "fault": ["[--sp] = {rets}", "sp += -500"],
}
r = sd.analyse(dis(BASE), set())
check("main: 20 + 100 (main), 8 + 1000 (deep, a tail call to leaf), 12: 1,140 B, the path to leaf",
      r["main"] == (1140, ["_start", "main", "deep", "leaf"]))
check("interrupts nested (a stub 28 B): 28 + 204, + 28 + 352, + 28 + 20, + 28 + 8, + 8 + 500 (fault): 1,204 B",
      r["irq_total"] == 1204 and [x[1] for x in r["irq"]] == [232, 380, 48, 36, 508])
t = dict(BASE, deep=["[--sp] = {rets, r4}", ("01 01", "tbh [r1]"), ("06 00", "<unknown instruction>"),
                      ("02 00", "<unknown instruction>"), ("04 00", "<unknown instruction>"), "sp += -1000"])
r = sd.analyse(dis(t), set())
check("a 'tbh' table is data up to its nearest target: its halfwords are no instructions, the code after it is",
      r["main"][0] == 20 + 100 + 8 + 1000 and not sd.STRADDLE)
t = dict(BASE, deep=["[--sp] = {rets, r4}", "sp += -16", "call 4 <deep>"])
r = sd.analyse(dis(t), set())
check("a recursion with no bound: listed, its cycle said", "deep" in r["notes"]["recursion"]
      and r["cycles"].get("deep") == ["deep", "deep"])
r = sd.analyse(dis(t), set(), None, {"deep": 5})
check("a recursion bounded to 5 levels: its frame 5 times (24 x 5)", r["main"][0] == 120 + 24 * 5 and
      not r["notes"]["recursion"])
t = dict(BASE, main=BASE["main"] + ["sp = r3"])
check("sp moved by a register: listed (var)", "main" in sd.analyse(dis(t), set())["notes"]["var"])
dup = {"_start": ["call 4 <main>"], "main": ["[--sp] = {rets}", "call 4 <f#2>"], "f": ["sp += -4000"], "f#2": ["sp += -8"]}
d = dis(dup).replace("f#2", "f")          # (objdump names both "f", the call by its address)
r = sd.analyse(d, set())
check("two static functions of one name: the call reaches the one at its address (8 B, not 4,000)", r["main"][0] == 12)

# ---- the indirect calls (stack_ir.py), each kind of flow
IR = """
%struct.ops = type { i32, ptr }
@TAB = internal constant %struct.ops { i32 1, ptr @hook_a }
@cb = internal global ptr null
@ARR = internal constant [2 x ptr] [ptr @arr_a, ptr @arr_b]
define void @hook_a(i32 %0) {
  ret void
}
define void @hook_b(i32 %0) {
  ret void
}
define void @hook_c(i32 %0) {
  ret void
}
define void @arr_a(i32 %0) {
  ret void
}
define void @arr_b(i32 %0) {
  ret void
}
define void @big(i32 %0) {
  ret void
}
define void @via_field(ptr %0) {
  %2 = getelementptr inbounds nuw i8, ptr %0, i32 4
  %3 = load ptr, ptr %2, align 4, !tbaa !10
  call void %3(i32 noundef 1)
  ret void
}
define void @set_cb() {
  store ptr @hook_b, ptr @cb, align 4, !tbaa !20
  ret void
}
define void @via_global() {
  %1 = load ptr, ptr @cb, align 4, !tbaa !20
  call void %1(i32 noundef 2)
  ret void
}
define void @far() {
  %1 = alloca ptr, align 4
  store volatile ptr @hook_c, ptr %1, align 4, !tbaa !20
  %2 = load volatile ptr, ptr %1, align 4, !tbaa !20
  call void %2(i32 noundef 3)
  ret void
}
define void @run(ptr %0) {
  call void %0(i32 noundef 4)
  ret void
}
define void @runner() {
  call void @run(ptr noundef @big)
  ret void
}
define void @each(ptr %0) {
  %2 = load ptr, ptr %0, align 4, !tbaa !20
  call void %2(i32 noundef 5)
  ret void
}
define void @each_caller() {
  call void @each(ptr noundef @ARR)
  ret void
}
define void @lost(ptr %0) {
  %2 = inttoptr i32 1234 to ptr
  call void %2(i32 noundef 6)
  ret void
}
!10 = !{!11, !12, i64 4}
!11 = !{!"ops", !13, i64 0, !12, i64 4}
!12 = !{!"any pointer", !14, i64 0}
!13 = !{!"int", !14, i64 0}
!14 = !{!"omnipotent char", !15, i64 0}
!15 = !{!"Simple C/C++ TBAA"}
!20 = !{!12, !12, i64 0}
"""
tg, st = sd.stack_ir.targets([IR])
check("a struct's field (TBAA ops+4, the initializer's field at 4): hook_a only", tg.get("via_field") == {"hook_a"})
check("a global (stored into cb): hook_b only", tg.get("via_global") == {"hook_b"})
check("a local (the far call's volatile pointer): hook_c only", tg.get("far") == {"hook_c"})
check("a parameter (run's, the caller's @big): big only", tg.get("run") == {"big"})
check("the memory a parameter points to (each's, the caller's @ARR): arr_a and arr_b",
      tg.get("each") == {"arr_a", "arr_b"})
check("a callee not followed (inttoptr): every function whose address is taken, of its signature, said",
      tg.get("lost", set()) >= {"hook_a", "hook_b", "hook_c", "arr_a", "arr_b", "big"} and st["any"] == 1)

# ---- tools/build.py: the stacks of fm1.toml in the linker script
build.OUT = Path(__file__).resolve().parent / "build" / "stack_depth_test"
build.OUT.mkdir(parents=True, exist_ok=True)
build.WIFI = False
build.CONF = {"main_stack": 6656, "irq_stack": 6400, "ram_layout": "packed"}
out = build.stack_script(ROOT / "device" / "app_packed.ld").read_text()
g1 = 0x01C7C000 - 6400 - 256
g0 = g1 - 6656 - 256
check("main_stack 6656 / irq_stack 6400: the bands and tops moved, SRAM up to the first band",
      f"_guard0 = {g0:#010x}; _ustack_lo = {g0 + 256:#010x}; _ustack_top = {g1:#010x};" in out
      and f"_guard1 = {g1:#010x}; _sstack_lo = {g1 + 256:#010x}; _sstack_top = 0x01c7c000;" in out
      and f"LENGTH = {g0 - 0x01C00000:#x}" in out and build.memory_regions(out)["SRAM"] == (0x01C00000, g0 - 0x01C00000))
for bad in ({"main_stack": 1000}, {"irq_stack": 8192}, {"main_stack": 6600},
            {"main_stack": 6656}, {"main_stack": 6656, "ram_layout": "fixed"}):
    build.CONF = bad
    try:
        build.stacks()
        ok = False
    except SystemExit:
        ok = True
    check(f"fm1.toml {bad}: refused (a multiple of 256, within the defaults; other sizes only on the packed SRAM)", ok)
build.CONF = {"ram_layout": "tight"}
try:
    build.ram_packed()
    ok = False
except SystemExit:
    ok = True
check("fm1.toml ram_layout = \"tight\": refused (\"fixed\" or \"packed\")", ok)
build.CONF = {}
check("no ram_layout: the fixed regions (device/app.ld)", build.ram_packed() is False)
check("no main_stack / irq_stack: app.ld's own (24,320 and 7,936)", build.stacks() == build.STACKS == (24320, 7936))
check("the margin: 25 % or 1 KiB, the larger (8,000 B -> 10,000; 2,000 -> 3,024)",
      sd.need(8000) == 10000 and sd.need(2000) == 3024)

print(f"stack_depth_test: {'FAILED' if failed else 'passed'}")
sys.exit(1 if failed else 0)
