// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// Copyright (C) 2026 Henri David
// The web installer (web/fm1ota.js) resuming an update refuses another firmware's update loader, against a simulated
// FM-1 in update mode on a fake WebMIDI port: an FM-1 left waiting in the update loader of M-VAVE's own firmware (the
// stock one, ota-FM-1_0NN, or the Open firmware's, ota-FM1C_<hex>) is sent nothing when the package is another firmware's
// (it may lay the image out differently), and is written when the package is M-VAVE's own (the return to stock); a
// loader of the package's own kind is written as before.
//   node tests/installer_foreign_loader_test.mjs
import { Updater, IS_MVAVE_LOADER, pack7 } from "../web/fm1ota.js";

let fails = 0;
const check = (ok, msg) => { console.log(`${msg.padEnd(72)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7].join();
const UP = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7].join();

const image = new Uint8Array(2048).map((_, i) => i & 0xFF);

function port(name) {
  const p = {
    input: { name, state: "disconnected", open: async () => {}, onmidimessage: null },
    output: { name, state: "disconnected", open: async () => {}, send: (b) => { if (p.output.state !== "connected") throw new Error("gone"); p.rx && p.rx(Array.from(b)); } },
    up(rx) { p.rx = rx; p.input.state = p.output.state = "connected"; },
    down() { p.rx = null; p.input.state = p.output.state = "disconnected"; },
  };
  return p;
}
const midi = (ports) => ({ inputs: new Map(ports.map((p, k) => ["i" + k, p.input])), outputs: new Map(ports.map((p, k) => ["o" + k, p.output])) });

// an FM-1 in the update loader called ident: answers the handshake; on UPGRADE reads a block, then the finish address,
// drops off and comes back as backAs on the other port. written: the loader was sent the update command
class Loader {
  constructor(p, ident, other, backAs) { this.p = p; this.ident = ident; this.other = other; this.backAs = backAs; this.written = false; p.up((d) => this.rx(d)); }
  tx(bytes) { const p = this.p; setTimeout(() => p.input.onmidimessage && p.input.onmidimessage({ data: Uint8Array.from(bytes) }), 1); }
  request(addr, len) {
    this.waiting = addr;
    const u = [0, 0x59, 0x30, 8, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    this.tx([0xF0, ...pack7([...u, ~s & 0xFF]), 0xF7]);
  }
  rx(d) {
    const j = d.join();
    if (j === HS) {
      const t = [...this.ident].map((c) => c.charCodeAt(0));
      this.tx([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
    } else if (j === UP) {
      this.written = true;
      this.queue = [[0, 512], [0xF0000000, 8]];
      this.request(...this.queue.shift());
    } else if (this.waiting != null) {
      const addr = this.waiting;
      this.waiting = null;
      if (addr >= 0xF0000000) {
        setTimeout(() => this.p.down(), 20);
        setTimeout(() => {
          const t = [...this.backAs].map((c) => c.charCodeAt(0));
          this.other.up((e) => { if (e.join() === HS) setTimeout(() => this.other.input.onmidimessage({ data: Uint8Array.from([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]) }), 1); });
        }, 1500);
        return;
      }
      this.request(...this.queue.shift());
    }
  }
}

check(IS_MVAVE_LOADER({ text: "ota-FM-1_015" }) && IS_MVAVE_LOADER({ text: "ota-FM1C_d017fb65e70aca97" }) &&
      !IS_MVAVE_LOADER({ text: "ota-FM-1_7010300" }) && !IS_MVAVE_LOADER({ text: "ota-FM-1_900" }) && !IS_MVAVE_LOADER({ text: "FM-1_015" }),
      "IS_MVAVE_LOADER: M-VAVE's stock and Open loaders only");

async function resume(ident, product, backAs) {
  const l = port("FM-1 Update"), c = port("Launchkey Mini");
  const dev = new Loader(l, ident, c, backAs);
  const up = new Updater(midi([l, c]));
  up.allow("Launchkey Mini");
  let got = null, err = null;
  try { got = await up.resume(image, null, product); } catch (e) { err = e; }
  return { dev, got, err };
}

for (const loader of ["ota-FM-1_015", "ota-FM-1_016", "ota-FM1C_d017fb65e70aca97"]) {
  const r = await resume(loader, "FM-1_7010400", "FM-1_7010400");
  check(r.err && r.err.code === "foreign" && r.err.detail === loader && !r.dev.written && r.got === null,
        `another firmware's package into ${loader}: refused, nothing written`);
}
for (const loader of ["ota-FM-1_7010300", "ota-FM-1_900", "ota-FM-1_1"]) {
  const r = await resume(loader, "FM-1_7010400", "FM-1_7010400");
  check(!r.err && r.got === true && r.dev.written, `the package's own kind of loader ${loader}: written as before`);
}
for (const loader of ["ota-FM-1_015", "ota-FM1C_d017fb65e70aca97", "ota-FM-1_900"]) {
  const r = await resume(loader, "FM-1_015", "FM-1_015");
  check(!r.err && r.got === true && r.dev.written, `M-VAVE's own package (the return to stock) into ${loader}: written`);
}
console.log(fails ? "INSTALLER FOREIGN LOADER TEST FAILED" : "installer foreign loader test passed");
process.exit(fails ? 1 : 0);
