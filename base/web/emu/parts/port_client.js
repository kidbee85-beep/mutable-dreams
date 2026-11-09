// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// The emulated FM-1 as a MIDI port, for another page of the same site (the emulator's port part, port.js):
//   import { emuAccess } from ".../emu/parts/port_client.js";
//   const access = emuAccess();          // in place of await navigator.requestMIDIAccess({ sysex: true })
// access.inputs / access.outputs hold a port pair "NAME (emulator)" for each emulator running in this browser
// (another tab or this one), state "connected"; onstatechange (or addEventListener("statechange")) says when one
// comes or goes. input.onmidimessage (or addEventListener("midimessage")) gets { data } for each whole message the
// FM-1 sends, SysEx included; output.send(data) plays into it, as over its USB port. No permission is asked: it
// never leaves the browser. One access serves the page: emuAccess() gives the same one again.

const CHANNEL = "fm1-emu";
const LAPSE_MS = 15000;                  // an emulator not heard from this long is gone (its tab closed hard)

function events(target) {
  const ls = new Map();
  target.addEventListener = (t, f) => { if (!ls.has(t)) ls.set(t, new Set()); ls.get(t).add(f); };
  target.removeEventListener = (t, f) => { if (ls.has(t)) ls.get(t).delete(f); };
  return (t, e) => {
    const h = target["on" + t];
    if (typeof h === "function") h.call(target, e);
    for (const f of ls.get(t) || []) f.call(target, e);
  };
}

function port(type, emu, name, ch) {
  const p = { id: `fm1-emu-${type}-${emu}`, name, manufacturer: "fm1-base", version: "", type, state: "connected",
    connection: "open", onmidimessage: null, onstatechange: null,
    open() { return Promise.resolve(p); }, close() { return Promise.resolve(p); } };
  p.fire = events(p);
  if (type === "output") {
    p.send = (data) => {
      if (p.state !== "connected") throw new Error(`${name}: disconnected`);
      ch.postMessage({ t: "to", emu, data: Uint8Array.from(data) });
    };
    p.clear = () => {};
  }
  return p;
}

let shared = null;
export function emuAccess() {
  if (shared) return shared;
  const ch = new BroadcastChannel(CHANNEL);
  const access = { inputs: new Map(), outputs: new Map(), sysexEnabled: true, onstatechange: null };
  const fire = events(access);
  const emus = new Map();                  // emu -> { input, output, seen }
  const change = (p) => { p.fire("statechange", { port: p }); fire("statechange", { port: p }); };
  function gone(emu) {
    const e = emus.get(emu);
    if (!e) return;
    emus.delete(emu);
    for (const p of [e.input, e.output]) {
      p.state = "disconnected";
      p.connection = "closed";
      (p.type === "input" ? access.inputs : access.outputs).delete(p.id);
      change(p);
    }
  }
  ch.onmessage = (ev) => {
    const m = ev.data || {};
    if (m.t === "here" && !emus.has(m.emu)) {
      const e = { input: port("input", m.emu, m.name, ch), output: port("output", m.emu, m.name, ch), seen: Date.now() };
      emus.set(m.emu, e);
      access.inputs.set(e.input.id, e.input);
      access.outputs.set(e.output.id, e.output);
      change(e.input);
      change(e.output);
    } else if (m.t === "here") {
      emus.get(m.emu).seen = Date.now();
    } else if (m.t === "gone") {
      gone(m.emu);
    } else if (m.t === "from" && emus.has(m.emu)) {
      const e = emus.get(m.emu);
      e.seen = Date.now();
      e.input.fire("midimessage", { data: Uint8Array.from(m.data), timeStamp: performance.now(), target: e.input });
    }
  };
  setInterval(() => { for (const [emu, e] of emus) if (Date.now() - e.seen > LAPSE_MS) gone(emu); }, 1000);
  ch.postMessage({ t: "who" });
  shared = access;
  return access;
}
