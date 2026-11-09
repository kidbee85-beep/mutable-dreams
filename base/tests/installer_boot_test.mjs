// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The installer's m-bootloader step (web/fm1ota.js Updater.installBoot) against a simulated FM-1 on a fake
// WebMIDI: the firmware reads the installer package as felucca-bsp chip/ota.c does (its UFW header, the
// installer's head and body, then 0xE0000000 "success"), drops off USB while the installer writes the boot area,
// and comes back as itself. A firmware that refuses the package asks for nothing: no "success", nothing written.
//   node tests/installer_boot_test.mjs web/m-bootloader/fm1-uboot-1.2.fwsc
import fs from "fs";
import { Updater, pack7, unpack7 } from "../web/fm1ota.js";
import { productOf, logicalImage, ufwEntries } from "../web/fm1pkg.js";

let fails = 0;
const check = (ok, msg) => { console.log(`${msg.padEnd(60)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };

const raw = new Uint8Array(fs.readFileSync(process.argv[2]));
const image = logicalImage(raw);
const ldr = ufwEntries(image)[100];
const PRODUCT = "FM-1_900123";

class FakeFM1 {
  constructor({ accept }) {
    this.accept = accept; this.served = 0; this.bad = 0; this.finished = false; this.reboots = 0;
    this.input = { name: "FM-1", state: "connected", open: async () => {}, onmidimessage: null };
    this.output = { name: "FM-1", state: "connected", open: async () => {}, send: (b) => this.rx(Array.from(b)) };
    this.access = { inputs: new Map([["i", this.input]]), outputs: new Map([["o", this.output]]) };
  }
  tx(bytes) { setTimeout(() => this.input.onmidimessage && this.input.onmidimessage({ data: Uint8Array.from(bytes) }), 1); }
  request(addr, len) {
    this.waiting = { addr, len };
    const u = [0, 0x59, 0x30, 8, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
      len & 0xFF, (len >> 8) & 0xFF, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    this.tx([0xF0, ...pack7([...u, ~s & 0xFF]), 0xF7]);
  }
  rx(d) {
    if (this.output.state !== "connected") throw new Error("disconnected");
    if (d.join() === [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7].join()) {
      const t = [...PRODUCT].map((c) => c.charCodeAt(0));
      this.tx([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
    } else if (d.join() === [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7].join()) {
      if (!this.accept) return;                          /* an older firmware: -7, it asks for nothing more */
      this.queue = [[0, 512], [512, 512], [ldr.off, 512], [ldr.off, 512], [ldr.off + 0x20, 512], [0xE0000000, 8]];
      this.request(...this.queue.shift());
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1)), { addr, len } = this.waiting;
      const want = addr === 0xE0000000 ? [..."success"].map((c) => c.charCodeAt(0)).concat(0) : Array.from(image.subarray(addr, addr + len));
      if (u.slice(14, 14 + want.length).join() !== want.join()) this.bad++;
      this.waiting = null;
      this.served++;
      if (addr === 0xE0000000) { this.finished = true; this.reset(); return; }
      this.request(...this.queue.shift());
    }
  }
  reset() {                                              /* the installer writes the boot area, then the firmware */
    setTimeout(() => { this.input.state = this.output.state = "disconnected"; }, 50);
    setTimeout(() => { this.input.state = this.output.state = "connected"; this.reboots++; }, 4000);
  }
}

check(productOf(raw) === "FM-1_099" && ldr && ldr.len > 0, "the package: FM-1_099 with an installer");
{
  const dev = new FakeFM1({ accept: true }), steps = [];
  let got = null, err = null;
  try { got = await new Updater(dev.access).installBoot(image, PRODUCT, (k) => steps.push(k)); } catch (e) { err = e; }
  check(!err && got === PRODUCT, `installs, and the firmware comes back (${err ? err.code : got})`);
  check(dev.finished && dev.bad === 0 && dev.served === 6, "every read answered with the package, then 'success'");
  check(steps.join() === "bootstart,bootwrite,bootdone", `steps: ${steps.join()}`);
  check(dev.reboots === 1, "waited for the restart");
}
{
  const dev = new FakeFM1({ accept: false });
  let err = null;
  try { await new Updater(dev.access).installBoot(image, PRODUCT); } catch (e) { err = e; }
  check(err && err.code === "bootrefused" && !dev.finished && dev.served === 0, "a firmware that refuses it: bootrefused, nothing asked");
}
console.log(fails ? "INSTALLER BOOT TEST FAILED" : "installer boot test passed");
process.exit(fails ? 1 : 0);
