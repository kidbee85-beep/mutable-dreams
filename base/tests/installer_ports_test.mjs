// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The web installer (web/fm1ota.js, web/fm1pkg.js) finding the FM-1 by its port, against a simulated FM-1 on a fake
// WebMIDI: a port with an unusual name is asked only once allowed; an update's loader and the restarted firmware are
// followed when they come back under other port names (macOS names ports from its cache); M-VAVE's Open firmware's
// identities; resume checks the FM-1 came back as the product; a damaged package is caught by its checksums.
//   node tests/installer_ports_test.mjs web/m-bootloader/fm1-uboot-1.2.fwsc
import fs from "fs";
import { Updater, pack7 } from "../web/fm1ota.js";
import { logicalImage, checkUfw } from "../web/fm1pkg.js";

let fails = 0;
const check = (ok, msg) => { console.log(`${msg.padEnd(64)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7].join();
const UP = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7].join();

const image = logicalImage(new Uint8Array(fs.readFileSync(process.argv[2])));

// one WebMIDI port pair that a device can appear on and leave
function port(name) {
  const ears = new Set();
  const p = {
    input: { name, state: "disconnected", open: async () => {}, onmidimessage: null, ears,
             addEventListener: (t, f) => ears.add(f), removeEventListener: (t, f) => ears.delete(f) },
    output: { name, state: "disconnected", open: async () => {}, send: (b) => { if (p.output.state !== "connected") throw new Error("gone"); p.rx && p.rx(Array.from(b)); } },
    up(rx) { p.rx = rx; p.input.state = p.output.state = "connected"; },
    down() { p.rx = null; p.input.state = p.output.state = "disconnected"; },
  };
  return p;
}
function midi(ports) {
  return { inputs: new Map(ports.map((p, k) => ["i" + k, p.input])), outputs: new Map(ports.map((p, k) => ["o" + k, p.output])) };
}

// an FM-1: identity on a port; on UPGRADE it asks for one block then the finish address, drops off, and comes back
// as next() says (a port and an identity)
class Dev {
  constructor(p, ident, plan) { this.asked = 0; this.plan = plan; this.on(p, ident); }
  on(p, ident) {
    this.p = p; this.ident = ident;
    p.up((d) => this.rx(d));
  }
  tx(bytes) {
    const p = this.p, e = { data: Uint8Array.from(bytes), target: p.input };
    setTimeout(() => { p.input.ears.forEach((f) => f(e)); p.input.onmidimessage && p.input.onmidimessage(e); }, 1);
  }
  request(addr, len) {
    this.waiting = addr;
    const u = [0, 0x59, 0x30, 8, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    this.tx([0xF0, ...pack7([...u, ~s & 0xFF]), 0xF7]);
  }
  rx(d) {
    const j = d.join();
    if (d[1] === 0x7D && d[2] === 0x53 && d[3] === 0x52) {     // the screen reader's HELLO: an fm1-base firmware answers
      if (d[4] === 1 && this.reader) this.tx([0xF0, 0x7D, 0x53, 0x52, 1, 1, ...[...this.reader].map((c) => c.charCodeAt(0)), 0, 0xF7]);
      return;
    }
    if (j === HS) {
      this.asked++;
      const t = [...this.ident].map((c) => c.charCodeAt(0));
      this.tx([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
    } else if (j === UP) {
      this.queue = [[0, 512], [/^ota-/.test(this.ident) ? 0xF0000000 : 0xE0000000, 8]];
      this.request(...this.queue.shift());
    } else if (this.waiting != null) {
      const addr = this.waiting;
      this.waiting = null;
      if (addr >= 0xE0000000) {
        const next = this.plan.shift();
        setTimeout(() => this.p.down(), 20);
        if (next) setTimeout(() => this.on(next[0], next[1]), 1500);
        return;
      }
      this.request(...this.queue.shift());
    }
  }
}

// 1. a port with an unusual name: not asked, then found once allowed
{
  const a = port("Arturia KeyStep"), b = port("SMK-37 Pro");
  new Dev(a, "FM-1_7010300", []);
  b.up(() => {});                                        // other gear: answers nothing
  const up = new Updater(midi([a, b]));
  check(!(await up.find(null)), "an unusual port name is not asked");
  check(up.ports().join() === "Arturia KeyStep,SMK-37 Pro", "ports() lists every in/out pair");
  const id = await up.probe("Arturia KeyStep");
  check(id && id.text === "FM-1_7010300", "Try: the FM-1 answers on it");
  const dev = await up.find(null);
  check(dev && dev.name === "Arturia KeyStep", "allowed, it is found from then on");
  if (dev) dev.link.close();
}

// 2. an install whose loader and restarted firmware come back under other names
{
  const a = port("Arturia KeyStep"), l = port("JL-USB Audio"), c = port("Launchkey Mini");
  const PRODUCT = "FM-1_7010400";
  new Dev(a, "FM-1_7010300", [[l, "ota-FM-1_1"], [c, PRODUCT]]);
  const up = new Updater(midi([a, l, c]));
  up.allow("Arturia KeyStep");
  const steps = [];
  let got = null, err = null;
  try { got = await up.install(image, PRODUCT, (k) => steps.push(k)); } catch (e) { err = e; }
  check(!err && got === PRODUCT, `followed to the loader and back (${err ? err.code + " " + err.message : got})`);
  check(steps.includes("loader") && steps.includes("done"), `steps: ${[...new Set(steps)].join()}`);
  check(up.baseline === null, "stops watching for new ports after the install");
  const other = await up.find(null);
  check(!other, "the new names are not asked after it");
}

// 3. M-VAVE's Open firmware and its loader
{
  const a = port("FM1 Community");
  new Dev(a, "FM-1_8000014", []);
  const id = await new Updater(midi([a])).probe("FM1 Community");
  check(id && id.open && id.model === "FM-1", "FM-1_8000014 (on \"FM1 Community\") is the Open firmware");
  const b = port("FM1 Community");
  new Dev(b, "ota-FM1C_d017fb65e70aca97", []);
  const ota = await new Updater(midi([b])).find((i) => /^ota-/i.test(i.model));
  check(ota && ota.id.model === "ota-FM1C", "its update loader ota-FM1C_<hex> is found");
  if (ota) ota.link.close();
  const c = port("FM-1");
  new Dev(c, "FM-1_8000400", []);
  const fomni = await new Updater(midi([c])).probe("FM-1");
  check(fomni && !fomni.open, "FoMni-1 0.4 (FM-1_8000400) is not");
}

// 4. resume: the loader writes, the FM-1 has to come back as the product
for (const [back, want] of [["FM-1_7010400", "done"], ["FM-1_7010300", "mismatch"]]) {
  const l = port("FM-1 Update"), c = port("Launchkey Mini");
  new Dev(l, "ota-FM-1_1", [[c, back]]);
  let got = null, err = null;
  try { got = await new Updater(midi([l, c])).resume(image, null, "FM-1_7010400"); } catch (e) { err = e; }
  check(want === "done" ? got === true && !err : err && err.code === "mismatch", `resume, back as ${back}: ${want}`);
}

// 5. an fm1-base firmware under an unusual name answers the screen reader's HELLO: the install finds it without Try;
// the loopback and the other gear get three HELLOs, never the identity query
{
  const a = port("Arturia KeyStep"), b = port("SMK-37 Pro"), iac = port("IAC Driver Bus 1"), l = port("JL-USB Audio");
  const PRODUCT = "FM-1_7010400";
  const dev = new Dev(a, "FM-1_7010300", [[l, "ota-FM-1_1"], [a, PRODUCT]]);
  dev.reader = "AMB-1 1.3";
  const toB = [], toIac = [];
  b.up((d) => toB.push(d.join()));
  iac.up((d) => { toIac.push(d.join()); setTimeout(() => iac.input.ears.forEach((f) => f({ data: Uint8Array.from(d), target: iac.input })), 1); });
  const up = new Updater(midi([iac, b, a, l]));
  check(!(await up.find(null)), "not found by its name");
  let got = null, err = null;
  try { got = await up.install(image, PRODUCT, () => {}); } catch (e) { err = e; }
  check(!err && got === PRODUCT, `found by its HELLO answer, installed (${err ? err.code + " " + err.message : got})`);
  check(up.answered.has("Arturia KeyStep") && !up.answered.has("IAC Driver Bus 1"), "answered: the FM-1, not the loopback");
  const HELLO = [0xF0, 0x7D, 0x53, 0x52, 1, 1, 0xF7].join();
  check(toB.length === 3 && toB.every((x) => x === HELLO) && toIac.length === 3 && toIac.every((x) => x === HELLO),
        "other gear and the loopback: three HELLOs, nothing else");
}

// 6. the package's checksums
{
  let ok = true;
  try { checkUfw(image); } catch (_) { ok = false; }
  const bad = image.slice(); bad[bad.length - 100] ^= 1;
  let caught = false;
  try { checkUfw(bad); } catch (_) { caught = true; }
  check(ok && caught, "checkUfw: the package passes, one flipped bit does not");
}
console.log(fails ? "INSTALLER PORTS TEST FAILED" : "installer ports test passed");
process.exit(fails ? 1 : 0);
