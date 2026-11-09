// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// The web installer (web/fm1ota.js) leaves Bluetooth MIDI ports alone, against fake WebMIDI ports: an FM-1 paired over
// Bluetooth shows a port named "... Bluetooth" beside its USB one (a Mac lists the paired one first) and it never
// answers an update. Such a port is never asked, not even when the user allowed it or it appeared during an update,
// and is not offered in the list to Try; the USB port of the same FM-1 is still found.
//   node tests/installer_bluetooth_test.mjs
import { Updater, IS_BLUETOOTH, pack7 } from "../web/fm1ota.js";

let fails = 0;
const check = (ok, msg) => { console.log(`${msg.padEnd(64)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7].join();

// a fake port pair that counts what was sent to it and, when answers is set, answers the handshake as an FM-1
function port(name, answers) {
  const p = {
    sent: 0,
    input: { name, state: "connected", open: async () => {}, onmidimessage: null },
    output: {
      name, state: "connected",
      open: async () => {},
      send: (b) => {
        p.sent++;
        if (!answers || Array.from(b).join() !== HS) return;
        const t = [..."FM-1_7010300"].map((c) => c.charCodeAt(0));
        const data = Uint8Array.from([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
        setTimeout(() => p.input.onmidimessage && p.input.onmidimessage({ data }), 1);
      },
    },
  };
  return p;
}
const midi = (ports) => ({ inputs: new Map(ports.map((p, k) => ["i" + k, p.input])), outputs: new Map(ports.map((p, k) => ["o" + k, p.output])) });

check(IS_BLUETOOTH("Optimist 112D Bluetooth") && IS_BLUETOOTH("FM-1 bluetooth") && !IS_BLUETOOTH("FM-1") && !IS_BLUETOOTH(""),
      "IS_BLUETOOTH: any case, part of the name");

// the paired port first, then the USB one: the USB one is found, the Bluetooth one is never sent anything
{
  const bt = port("FM-1 Bluetooth", true), usb = port("FM-1", true);
  const up = new Updater(midi([bt, usb]));
  const dev = await up.find(null);
  check(dev && dev.name === "FM-1" && bt.sent === 0, "the USB port is found, the Bluetooth one is not asked");
  if (dev) dev.link.close();
}
// only a Bluetooth port: not found, nothing sent
{
  const bt = port("Optimist 112D Bluetooth", true);
  const up = new Updater(midi([bt]));
  check(!(await up.find(null)) && bt.sent === 0, "only a Bluetooth port: not found, nothing sent");
  up.allow("Optimist 112D Bluetooth");
  check(!up.askable("Optimist 112D Bluetooth") && !(await up.find(null)) && bt.sent === 0, "not even allowed by the user");
  check(!(await up.probe("Optimist 112D Bluetooth")) && bt.sent === 0, "Try on it answers nothing and sends nothing");
  check(up.ports().length === 0, "ports(): not offered in the list to Try");
}
// a Bluetooth port that appears during an update is not asked either
{
  const bt = port("Some Bluetooth", true);
  const up = new Updater(midi([]));
  up.watch();
  up.access.inputs.set("n", bt.input);
  up.access.outputs.set("n", bt.output);
  check(!up.askable("Some Bluetooth") && !(await up.find(null)) && bt.sent === 0, "a Bluetooth port that appears during an update: not asked");
  up.unwatch();
}
console.log(fails ? "INSTALLER BLUETOOTH TEST FAILED" : "installer bluetooth test passed");
process.exit(fails ? 1 : 0);
