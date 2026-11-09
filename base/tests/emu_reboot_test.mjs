// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "reboot" (web/emu/parts/reboot*.js): tests/reboot_app built with it. plat_restart starts the wasm again
// (the app's RAM forgotten, the saved object kept), the page's Restart does the same, the update mode stops the sound
// and the app and shows its reason, REC held at a start is the update mode, and a restart leaves it.
//   node tests/emu_reboot_test.mjs
import { readFileSync } from "node:fs";
import { startEmu, buildEmu, check, done } from "./emu_harness.mjs";

const dir = buildEmu("tests/reboot_app", "reboot");
const wasm = readFileSync(`${dir}/app.wasm`);
const emu = await startEmu(dir, { parts: { reboot: { wasm: wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength) } } });
const later = (ms) => new Promise((r) => setTimeout(r, ms));
const last = (kind) => emu.take(kind).at(-1);
const settle = async (ms) => { emu.run(ms); await later(40); emu.run(ms); };
const BTN = { FX: 1 << 0, SEL: 1 << 1, REC: 1 << 11 };
const tap = async (mask) => { emu.send({ type: "buttons", mask }); emu.run(100); emu.send({ type: "buttons", mask: 0 }); emu.run(100); };

emu.run(300);
let f = last("frame");
check(f.keys === 1 << 1, `the first start: init ran once (keys ${f.keys.toString(16)})`);
check(emu.take("reboot").at(-1)?.state === "running", "the part says running");
await tap(BTN.SEL);
await tap(BTN.SEL);
await tap(BTN.SEL);
f = last("frame");
check((f.buttons & 0x3fff) === 3, `three SEL taps: saved value 3 (${f.buttons & 0x3fff})`);

// plat_restart: a fresh wasm, the RAM forgotten (init ran once again, not twice), the saved value kept
const before = emu.ex;
await tap(BTN.FX);
await settle(300);
f = last("frame");
const states = emu.take("reboot").map((m) => m.state);
check(emu.ex !== before, "the wasm was started again (a new instance)");
check(states.includes("restarting") && states.at(-1) === "running", `restarting, then running (${states})`);
check(f.keys === 1 << 1, `RAM forgotten: init counts one start again (keys ${f.keys.toString(16)})`);
check((f.buttons & 0x3fff) === 3, `the saved value is kept (${f.buttons & 0x3fff})`);
await tap(BTN.SEL);
check((last("frame").buttons & 0x3fff) === 4, "the app runs on: SEL counts on from the kept value");

// the page's Restart: the same
emu.send({ type: "app", part: "reboot", cmd: "restart" });
await settle(300);
f = last("frame");
check(f.keys === 1 << 1 && (f.buttons & 0x3fff) === 4, "Restart from the page: RAM forgotten, value 4 kept");

// the update mode: silence, the app stopped, the reason on the screen
emu.take("reboot");
emu.send({ type: "app", part: "reboot", cmd: "update" });
emu.run(100);
check(emu.take("reboot").at(-1)?.state === "update", "the part says update mode");
check(emu.run(200) === 0, "the sound is stopped");
emu.send({ type: "buttons", mask: BTN.SEL });
emu.run(100);
emu.send({ type: "buttons", mask: 0 });
emu.run(100);
const ex = emu.ex;
check(ex.web_update() === 1, "the platform is in update mode (UBOOT (USB))");
const fb = new Uint16Array(ex.memory.buffer, ex.web_fb(), 240 * 240);
check(fb[0] === 0 && fb.some((p) => p !== 0), "the screen is black with text");
// leaving it: a restart, the value kept and not counted by the stopped app
emu.send({ type: "app", part: "reboot", cmd: "restart" });
await settle(300);
f = last("frame");
check(emu.take("reboot").at(-1)?.state === "running" && (f.buttons & 0x3fff) === 4, "Restart leaves it: running, value 4");

// REC held at a start: the chip's update mode
emu.send({ type: "buttons", mask: BTN.REC });
emu.run(50);
emu.send({ type: "app", part: "reboot", cmd: "restart" });
await settle(300);
check(emu.ex.web_update() === 2, "REC held at a start: UPDATE MODE (REC)");
emu.send({ type: "buttons", mask: 0 });
emu.run(50);
emu.send({ type: "app", part: "reboot", cmd: "restart" });
await settle(300);
check(emu.ex.web_update() === 0 && (last("frame").buttons & 0x3fff) === 4, "REC let go, Restart: running again");

// the page side
const props = [];
globalThis.document = { createElement: () => ({ addEventListener() {} }), querySelector: () => null };
const page = await import("../web/emu/parts/reboot.js");
check(page.describe({ state: "running" }) === "" && /app asked/.test(page.describe({ state: "restarting", why: "app" }))
  && /UPDATE MODE \(REC\)/.test(page.describe({ state: "update", why: "UPDATE MODE (REC)" })), "the page's texts");
let hook = null;
page.attach({ post() {}, beforeLoad: (fn) => { hook = fn; }, bar: null });
const msg = hook({ wasm: wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength), parts: {} });
check(msg.parts.reboot.wasm.byteLength === wasm.length, "the page hands the worklet the wasm's bytes");
void props;
done();
