// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "port" (web/emu/parts/port.js, port_client.js): the template built with it (and midi) under node, its
// page side wired to the worklet; another "page" (port_client.js's emuAccess) finds the port pair "template
// (emulator)", connected; a note-on sent to it sounds; a key's note reaches its input; SysEx goes through whole;
// two emulators are two pairs; one that goes is disconnected.
//   node tests/emu_port_test.mjs
import { startEmu, buildEmu, check, done } from "./emu_harness.mjs";

const dir = buildEmu("template", "port");
const emu = await startEmu(dir);
const timers = [];
globalThis.addEventListener = (t, f) => { if (t === "pagehide") timers.push(f); };
Object.defineProperty(globalThis, "localStorage", { configurable: true, value: { getItem: () => null, setItem() {} } });
const midi = await import(`${dir}/parts/midi.js`), port = await import(`${dir}/parts/port.js`);
const posted = [];
midi.attach({ id: "template", post: (m) => { posted.push(m); emu.send({ ...m, type: "app", part: "midi" }); } });
port.attach({ id: "template", name: "template" });
const pump = (ms) => { const n = emu.run(ms); for (const m of emu.take("midi")) midi.message(m); return n; };
const tick = () => new Promise((r) => setTimeout(r, 30));

const { emuAccess } = await import(`${dir}/parts/port_client.js`);
const access = emuAccess();
const events = [];
access.onstatechange = (e) => events.push(`${e.port.type} ${e.port.state}`);
await tick();
const inp = [...access.inputs.values()][0], out = [...access.outputs.values()][0];
check(!!inp && !!out && inp.name === "template (emulator)" && inp.state === "connected" && access.sysexEnabled,
  `the port pair is there: ${inp && inp.name}`);
check(events.includes("input connected") && events.includes("output connected"), "statechange says so");
check(emuAccess() === access, "one access a page");
pump(800);
const quiet = pump(100);
out.send([0x90, 60, 100]);
await tick();
const loud = pump(300);
check(loud > 0.01 && loud > quiet * 4, `a note-on sent to the port sounds (peak ${loud.toFixed(4)})`);
out.send([0x80, 60, 0]);
await tick();
pump(1500);
const got = [];
inp.onmidimessage = (e) => got.push([...e.data]);
emu.send({ type: "keys", mask: 1 << 7 });
pump(100);
await tick();
check(got.some((m) => m[0] === 0x90 && m[1] === 60), "a key's note reaches the port's input");
const sx = [];
inp.addEventListener("midimessage", (e) => { if (e.data[0] === 0xf0) sx.push([...e.data]); });
midi.message({ msgs: [Uint8Array.of(0xf0, 0x7d, 0x01, 0x02, 0xf7)] });
out.send([0xf0, 0x7d, 0x03, 0xf7]);
await tick();
check(sx.length === 1 && sx[0].length === 5, "SysEx from the FM-1 arrives whole");
check(posted.some((m) => m.data && m.data[0] === 0xf0 && m.data.length === 4), "SysEx to it goes in whole");
const other = new BroadcastChannel("fm1-emu");
other.postMessage({ t: "here", emu: "second", name: "radio (emulator)" });
await tick();
check(access.inputs.size === 2, "a second emulator: a second pair");
other.postMessage({ t: "gone", emu: "second" });
await tick();
check(access.inputs.size === 1 && events.includes("input disconnected"), "it goes: disconnected and removed");
for (const f of timers) f();
await tick();
check(access.inputs.size === 0 && inp.state === "disconnected", "the page goes: its pair too");
let threw = false;
try { out.send([0x90, 1, 1]); } catch { threw = true; }
check(threw, "sending to a disconnected port throws, as Web MIDI's does");
other.close();
done();
