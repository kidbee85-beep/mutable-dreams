// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// The web installer (web/fm1ota.js) asks the ports that FM-1 firmwares of the family name after themselves (Optimist,
// SLOOP, Melodee, LOOM-8: an FM-1 running one of them shows as that name alone) without the user picking them, as
// tools/fm1_install.py does (tests/install_test.py); other gear is still never sent anything. Fake WebMIDI ports.
//   node tests/installer_family_ports_test.mjs
import { Updater, pack7 } from "../web/fm1ota.js";
import { PORT_NAMES } from "../web/fm1ports.js";

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

for (const name of ["Optimist", "SLOOP 2.4", "Melodee", "LOOM-8", "Loom8"]) {
  const fm = port(name, true), gear = port("Launchkey Mini", false);
  const dev = await new Updater(midi([gear, fm])).find(null);
  check(dev && dev.name === name && gear.sent === 0, `an FM-1 on "${name}" is found, other gear is not asked`);
  if (dev) dev.link.close();
}
{
  const a = port("Arturia KeyStep", false), b = port("IAC Bus 1", false);
  const up = new Updater(midi([a, b]));
  check(!(await up.find(null)) && a.sent === 0 && b.sent === 0, "other gear: not found, nothing sent");
}
check(["optimist", "sloop", "melodee", "loom-8"].every((n) => PORT_NAMES.includes(n)), "PORT_NAMES lists the family's firmwares");
console.log(fails ? "INSTALLER FAMILY PORTS TEST FAILED" : "installer family ports test passed");
process.exit(fails ? 1 : 0);
