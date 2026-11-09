// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The installer's resource step (web/fm1ota.js resPayload, Updater.installRes) against a simulated FM-1 on a fake
// WebMIDI that answers RES_REQ as device/res.c does: its own build's length and CRC-32 only, then the payload pulled in
// reads of 512 and RS_OK_ADDR, or RS_FAIL_ADDR | code (playing, another build's), or OK at once when it holds them.
// Also: a damaged file is refused before anything is sent, a lost cable is said, and web/installer.html's words for
// it are there in both languages.
//   node tests/installer_res_test.mjs
import fs from "fs";
import { Updater, pack7, unpack7, crc32, resPayload } from "../web/fm1ota.js";

let fails = 0;
const check = (ok, msg) => { console.log(`${msg.padEnd(64)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7].join();
const PRODUCT = "FM-1_100123";
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;

// a NAME-res.bin as tools/build.py res_file makes it
function resFile(payload) {
  const f = new Uint8Array(32 + payload.length), v = new DataView(f.buffer);
  v.setUint32(0, 0x31534552, true); v.setUint32(4, payload.length, true); v.setUint32(8, crc32(payload), true);
  v.setUint32(28, crc32(f.subarray(0, 28)), true);
  f.set(payload, 32);
  return f;
}
const payload = Uint8Array.from({ length: 5000 }, (_, i) => (i * 7 + (i >> 8)) & 0xFF);
const file = resFile(payload);

class FakeFM1 {
  // mode: write, have (holds them already), playing; want: the build's payload; dropAt: unplugged after so many reads
  constructor({ mode = "write", want = payload, dropAt = -1, bootMs = 0 } = {}) {
    Object.assign(this, { mode, want, dropAt, got: new Uint8Array(want.length), reads: 0, bad: 0, result: null, reqs: 0 });
    this.input = { name: "FM-1", state: bootMs ? "disconnected" : "connected", open: async () => {}, onmidimessage: null };
    this.output = { name: "FM-1", state: this.input.state, open: async () => {}, send: (b) => this.rx(Array.from(b)) };
    this.access = { inputs: new Map([["i", this.input]]), outputs: new Map([["o", this.output]]) };
    if (bootMs) setTimeout(() => { this.input.state = this.output.state = "connected"; }, bootMs);
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
  note(addr) { this.result = addr; this.request(addr, 8); }
  next() {                                               /* res_rx: the payload in reads of 512, then RS_OK_ADDR */
    const off = this.reads * 512;
    if (this.reads === this.dropAt) { this.input.state = this.output.state = "disconnected"; return; }
    if (off < this.want.length) this.request(off, Math.min(512, this.want.length - off));
    else this.note(crc32(this.got) === crc32(this.want) ? 0xC0000000 : 0xCF000016);
  }
  rx(d) {
    if (this.output.state !== "connected") throw new Error("disconnected");
    if (d.join() === HS) {
      const t = [...PRODUCT].map((c) => c.charCodeAt(0));
      this.tx([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
      return;
    }
    const u = unpack7(d.slice(1, -1));
    if (!this.waiting && u[2] === 0x44 && u[3] === 8) {
      this.reqs++;
      if (u32(u, 6) !== this.want.length || u32(u, 10) !== crc32(this.want)) this.note(0xCF000000 | 20);
      else if (this.mode === "have") this.note(0xC0000000);
      else if (this.mode === "playing") this.note(0xCF000000 | 10);
      else this.next();
    } else if (this.waiting) {
      const { addr, len } = this.waiting;
      this.waiting = null;
      if (addr >= 0xC0000000) return;                    /* the host's "success" to a reply address */
      const data = u.slice(14, 14 + len);
      if (data.length !== len) this.bad++;
      this.got.set(data, addr);
      this.reads++;
      this.next();
    }
  }
}
const run = async (dev, region = file) => {
  const pcts = [];
  try { return { code: await new Updater(dev.access).installRes(region, PRODUCT, (p) => pcts.push(p)), pcts }; }
  catch (e) { return { err: e, pcts }; }
};

{
  const p = resPayload(file);
  check(p.payload.length === payload.length && p.crc === crc32(payload), "resPayload: the payload and its CRC-32");
  const bad = file.slice(); bad[40] ^= 1;
  const head = file.slice(); head[12] = 1;
  let e1 = null, e2 = null, e3 = null;
  try { resPayload(bad); } catch (e) { e1 = e; }
  try { resPayload(head); } catch (e) { e2 = e; }
  try { resPayload(file.subarray(0, 100)); } catch (e) { e3 = e; }
  check([e1, e2, e3].every((e) => e && e.code === "resbad"), "a damaged payload, header or a cut file: resbad");
}
{
  const dev = new FakeFM1({ bootMs: 1500 }), r = await run(dev);
  check(r.code === 0 && dev.result === 0xC0000000, `waits for the FM-1, writes it: OK (${r.err ? r.err.code : r.code})`);
  check(dev.reads === 10 && dev.bad === 0 && crc32(dev.got) === crc32(payload), "every read answered with the payload");
  check(r.pcts.length === 10 && Math.round(r.pcts[9]) === 100, "progress to 100");
}
{
  const dev = new FakeFM1({ mode: "have" }), r = await run(dev);
  check(r.code === 0 && dev.reads === 0, "holds them already: OK at once, nothing read");
}
{
  const dev = new FakeFM1({ mode: "playing" }), r = await run(dev);
  check(r.code === 10 && dev.reads === 0, "playing: code 10, nothing read");
}
{
  const dev = new FakeFM1({ want: payload.subarray(0, 4000) }), r = await run(dev);
  check(r.code === 20 && dev.reads === 0, "another build's resources: code 20, nothing read");
}
{
  const dev = new FakeFM1(), bad = file.slice(); bad[100] ^= 0x10;
  const r = await run(dev, bad);
  check(r.err && r.err.code === "resbad" && dev.reqs === 0, "a damaged file: nothing sent");
}
{
  const dev = new FakeFM1({ dropAt: 3 }), r = await run(dev);
  check(r.err && r.err.code === "lost", `unplugged on the way: lost (${r.err ? r.err.code : r.code})`);
}
{
  // the page's words: every key in both languages, and each code installRes can return has its own
  const html = fs.readFileSync(new URL("../web/installer.html", import.meta.url), "utf-8");
  const src = html.slice(html.indexOf("const TEXT = {") + 13, html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = new Function(`return ${src}`)();
  const en = Object.keys(TEXT.en), ja = Object.keys(TEXT.ja);
  check(en.length === ja.length && en.every((k) => k in TEXT.ja), "the installer's words: the same keys in en and ja");
  check(["res", "resdone", "resfail", "res2", "res7", "res8", "res10", "res20", "res21", "res22", "resload", "resnoreturn",
         "reslost", "resother"].every((k) => k in TEXT.en), "the resource step's words");
}
console.log(fails ? "INSTALLER RES TEST FAILED" : "installer res test passed");
process.exit(fails ? 1 : 0);
