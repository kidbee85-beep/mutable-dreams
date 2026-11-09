// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "midi" (fm1.toml emu = ["midi"]): the emulated FM-1's USB MIDI on the page. "MIDI ports" in the
// page's bar asks the browser for Web MIDI (with SysEx) and offers its inputs and outputs: what the chosen input
// plays goes into the FM-1 as from its USB port, what the FM-1 sends goes out of the chosen output, SysEx included
// (an app with APP_SYSEX gets and sends whole frames). The choice is kept in this browser and asked again on the
// next visit only if it was made before.
// Other parts reach the same MIDI (the port part: another page): toDevice(bytes) and fromDevice(fn) below.
// The worklet's side is midi_worklet.js.

let post = null;
const listeners = new Set();

/** whole MIDI messages (a Uint8Array or an array of bytes) into the FM-1, as from its USB port */
export function toDevice(bytes) {
  if (post) post({ data: Uint8Array.from(bytes) });
}
/** fn(msg) for each whole message the FM-1 sends (a Uint8Array); returns a function that stops it */
export function fromDevice(fn) {
  listeners.add(fn);
  return () => listeners.delete(fn);
}
export function message(m) {
  for (const msg of m.msgs || []) for (const fn of listeners) fn(msg);
}

// ------------------------------------------------------------------ Web MIDI ports
const NONE = "";
let access = null, input = null, output = null, prefsKey = "", prefs = {};
const readPrefs = () => { try { return JSON.parse(localStorage.getItem(prefsKey) || "{}"); } catch { return {}; } };
const savePrefs = () => { try { localStorage.setItem(prefsKey, JSON.stringify(prefs)); } catch { /* private window */ } };

function el(tag, attrs = {}, text = "") {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  if (text) e.textContent = text;
  return e;
}
function fill(sel, ports, want) {
  sel.replaceChildren(el("option", { value: NONE }, "none"));
  for (const p of ports.values()) if (p.state !== "disconnected") sel.append(el("option", { value: p.name }, p.name));
  sel.value = [...sel.options].some((o) => o.value === want) ? want : NONE;
}
function onInput(e) { toDevice(e.data); }
function choose(ui, save) {
  if (input) input.onmidimessage = null;
  input = [...access.inputs.values()].find((p) => p.name === ui.inSel.value) || null;
  output = [...access.outputs.values()].find((p) => p.name === ui.outSel.value) || null;
  if (input) input.onmidimessage = onInput;
  if (!save) return;                      // (a port gone for now stays the choice for when it comes back)
  prefs = { on: true, in: ui.inSel.value, out: ui.outSel.value };
  savePrefs();
}
function refresh(ui) {
  fill(ui.inSel, access.inputs, input ? input.name : prefs.in);
  fill(ui.outSel, access.outputs, output ? output.name : prefs.out);
  choose(ui, false);
}
async function open(ui, status) {
  if (!navigator.requestMIDIAccess) { status("This browser has no Web MIDI (Chrome, Edge, Firefox and Opera have it)."); return; }
  try {
    access = await navigator.requestMIDIAccess({ sysex: true });
  } catch (e) {
    status("MIDI was not allowed: " + (e && e.message || e));
    return;
  }
  if (!prefs.on) { prefs = { ...prefs, on: true }; savePrefs(); }
  ui.ask.hidden = true;
  ui.pick.hidden = false;
  access.onstatechange = () => refresh(ui);
  refresh(ui);
}

export function attach(ctx) {
  post = ctx.post;
  prefsKey = `${ctx.id}-emu-midi`;
  prefs = readPrefs();
  fromDevice((msg) => {
    if (output && output.state !== "disconnected") {
      try { output.send(msg); } catch { /* the port went meanwhile: statechange follows */ }
    }
  });
  if (!ctx.bar) return;
  const box = el("span", { class: "midi" });
  const ask = el("button", { type: "button" }, "MIDI ports");
  const pick = el("span", { class: "keys" });
  const inSel = el("select", { "aria-label": "MIDI input: played into the FM-1" });
  const outSel = el("select", { "aria-label": "MIDI output: what the FM-1 sends" });
  pick.append("MIDI in ", inSel, " out ", outSel);
  pick.hidden = true;
  box.append(ask, pick);
  ctx.bar.insertBefore(box, ctx.bar.querySelector(".cpu"));
  const ui = { ask, pick, inSel, outSel };
  ask.addEventListener("click", () => open(ui, ctx.status));
  inSel.addEventListener("change", () => choose(ui, true));
  outSel.addEventListener("change", () => choose(ui, true));
  if (prefs.on) open(ui, ctx.status);
}
