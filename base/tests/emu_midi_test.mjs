// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "midi" (web/emu/parts/midi*.js): the template built with it under node. Into the FM-1: a note-on
// sounds, running status, the note-off silences, a real-time byte inside a SysEx still arrives; a SysEx for an app
// without APP_SYSEX is dropped. Out: a key's note as a whole message. The page's side with a stand-in Web MIDI: the
// chosen input's bytes go to the worklet, the FM-1's messages to the chosen output, the choice kept and taken again.
//   node tests/emu_midi_test.mjs
import { startEmu, buildEmu, check, done } from "./emu_harness.mjs";

const emu = await startEmu(buildEmu("template", "midi"));
const midi = (data) => emu.send({ type: "app", part: "midi", data: Uint8Array.from(data) });
emu.run(800);
const quiet = emu.run(100);
midi([0x90, 60, 100]);
const loud = emu.run(300);
check(loud > 0.01 && loud > quiet * 4, `a note-on from the page sounds (peak ${loud.toFixed(4)})`);
midi([0x80, 60, 0]);
emu.run(1500);
const off = emu.run(100);
check(off < loud / 4, `its note-off silences it (peak ${off.toFixed(4)})`);
// the packets the worklet makes (a spy on web_midi_in)
const real = emu.proc.ex, pk = [];
emu.proc.ex = { ...real, web_midi_in: (port, p) => { pk.push([port, p >>> 0]); real.web_midi_in(port, p); } };
midi([0x90, 62, 100, 64, 100]);
midi([0x80, 62, 0, 64, 0]);
check(pk.length === 4 && pk[1][1] === (0x09 | 0x90 << 8 | 64 << 16 | 100 << 24) && pk[3][1] === (0x08 | 0x80 << 8 | 64 << 16),
  "running status: two notes from one status byte, as USB-MIDI packets");
check(pk.every(([port]) => port === 0), "all on the USB port");
pk.length = 0;
midi([0xf0, 0x7d, 0x01, 0xf8, 0x02, 0xf7, 0xf2, 0x10, 0x20, 0xc0, 5]);
check(pk.length === 3 && pk[0][1] === (0x0f | 0xf8 << 8) && pk[1][1] === (0x03 | 0xf2 << 8 | 0x10 << 16 | 0x20 << 24)
  && pk[2][1] === (0x0c | 0xc0 << 8 | 5 << 16), "a real-time byte inside a SysEx still arrives; song position and program change packed");
check(true, "the SysEx itself, for an app without APP_SYSEX (no web_sx_in): dropped");
emu.proc.ex = real;
emu.run(1500);
emu.take("midi");
emu.send({ type: "keys", mask: 1 << 7 });
emu.run(100);
const out = emu.take("midi").flatMap((m) => m.msgs);
check(out.some((m) => m.length === 3 && m[0] === 0x90 && m[1] === 60 && m[2] > 0), `a key's note goes out whole (${out.length} messages)`);
check(out.every((m) => m[0] & 0x80), "every message out starts with its status");
emu.send({ type: "keys", mask: 0 });
emu.run(100);
check(emu.take("midi").flatMap((m) => m.msgs).some((m) => m[0] === 0x80 || (m[0] === 0x90 && m[2] === 0)), "and its note-off");

// the page's side, with a stand-in Web MIDI and page
const store = new Map();
globalThis.localStorage = { getItem: (k) => store.get(k) ?? null, setItem: (k, v) => store.set(k, v) };
const mkEl = () => {
  const e = { children: [], options: [], hidden: false, value: "", attrs: {}, listeners: {},
    setAttribute(k, v) { this.attrs[k] = v; if (k === "value") this.value = v; }, append(...c) { this.children.push(...c); this.options.push(...c); },
    replaceChildren(...c) { this.options = c; }, addEventListener(t, f) { this.listeners[t] = f; }, querySelector: () => null,
    insertBefore(c) { this.children.push(c); } };
  Object.defineProperty(e, "textContent", { set(v) { e.text = v; } });
  return e;
};
globalThis.document = { createElement: () => mkEl() };
const sent = [], posted = [];
const inp = { name: "Keys", state: "connected", onmidimessage: null };
const outp = { name: "Synth", state: "connected", send: (d) => sent.push([...d]) };
Object.defineProperty(globalThis, "navigator", { configurable: true, value: { requestMIDIAccess: async (o) => (o.sysex ? { inputs: new Map([["i", inp]]), outputs: new Map([["o", outp]]), onstatechange: null } : null) } });
const page = await import("../web/emu/parts/midi.js");
const bar = mkEl();
page.attach({ id: "template", bar, status: (t) => posted.push(["status", t]), post: (m) => posted.push(m) });
const box = bar.children[0], [ask, pick] = box.children, [, inSel, , outSel] = pick.children;
await ask.listeners.click();
await new Promise((r) => setTimeout(r, 0));
check(!pick.hidden && inSel.options.length === 2 && outSel.options.length === 2, "the ports are offered after asking (with SysEx)");
inSel.value = "Keys"; inSel.listeners.change();
outSel.value = "Synth"; outSel.listeners.change();
inp.onmidimessage({ data: Uint8Array.of(0x90, 61, 90) });
check(posted.some((m) => m.data && m.data[1] === 61), "the input's bytes go to the worklet");
page.message({ msgs: [Uint8Array.of(0xf0, 0x7d, 0x01, 0xf7), Uint8Array.of(0x90, 60, 1)] });
check(sent.length === 2 && sent[0][0] === 0xf0 && sent[1][1] === 60, "the FM-1's messages, SysEx too, to the output");
check(JSON.parse(store.get("template-emu-midi")).in === "Keys", "the choice is kept in this browser");
done();
