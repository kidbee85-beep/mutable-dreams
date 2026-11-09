// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// emu-needs: midi
// Emulator part "port" (fm1.toml emu = ["port"], with midi): the emulated FM-1 as a MIDI port for other pages of the
// same site, as if it were plugged in: an app's web editor, a librarian, a backup page. Such a page imports
// port_client.js from here (emu/parts/port_client.js) and calls emuAccess(), a Web MIDI access of its own whose port
// pair "NAME (emulator)" is there while an emulator runs; it uses it as it would navigator.requestMIDIAccess's. Both
// ways, SysEx included, through a BroadcastChannel ("fm1-emu"): same origin only, no permission asked, nothing leaves
// the browser. The protocol (port_client.js has the other side):
//   emulator -> all  { t: "here", emu, name }   when it starts, every 2 s, and when asked ({ t: "who" })
//                    { t: "gone", emu }          when the page goes
//                    { t: "from", emu, data }    a whole message the FM-1 sent
//   page -> emulator { t: "to", emu, data }      a whole message into the FM-1, as from its USB port
import { toDevice, fromDevice } from "./midi.js";

export const CHANNEL = "fm1-emu";

export function attach(ctx) {
  if (typeof BroadcastChannel === "undefined") return;
  const ch = new BroadcastChannel(CHANNEL);
  const emu = Math.random().toString(36).slice(2, 10), name = `${ctx.name} (emulator)`;
  const here = () => ch.postMessage({ t: "here", emu, name });
  ch.onmessage = (e) => {
    const m = e.data || {};
    if (m.t === "who") here();
    else if (m.t === "to" && m.emu === emu && m.data) toDevice(m.data);
  };
  fromDevice((data) => ch.postMessage({ t: "from", emu, data }));
  // every 2 s: on the worklet's frames (they keep coming in a background tab, whose timers the browser may hold back
  // for many seconds when the page is busy), and a timer for when the sound is stopped
  let said = 0;
  const every = () => { const now = performance.now(); if (now - said >= 2000) { said = now; here(); } };
  if (ctx.node && ctx.node.port) ctx.node.port.addEventListener("message", every);
  setInterval(every, 2000);
  every();
  addEventListener("pagehide", () => ch.postMessage({ t: "gone", emu }));
}
