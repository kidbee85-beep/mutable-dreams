// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The web backup page (web/backup/index.html, the part between its "backup protocol" markers), filled in as
// tools/make_site.py fills it for the test's app (tests/backup_test.c: "TSTB", version 3; named Template, FM-1_11):
//  1. against tools/fm1_backup.py's own conversation with a simulated FM-1 (tests/backup_record.py): the FM-1's
//     side replayed, every message the page sends checked byte for byte against the tool's (a backup, a restore, a
//     restore while playing);
//  2. against a simulated FM-1 in JS (backup.c's 128-byte frames and end frame, restore_rx()'s reads: the head, each
//     record, its payload in 512s, the tail, the question, the result) on the device's own image (tests/backup_test.c):
//     a backup byte for byte, a restore byte for byte, STOP FIRST, HOME, a damaged file, an older version's and
//     another firmware's (nothing sent), another firmware on the FM-1, an FM-1 that stops asking, a lost frame.
// (From Polly's.)
//   node tests/backup_page_test.mjs TRANSCRIPT.json
import fs from "fs";
import path from "path";
import zlib from "zlib";
import { fileURLToPath } from "url";

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");
// fm1ports.js as tools/make_site.py inlines it (strip_module: no export, no import)
const PORTS_JS = fs.readFileSync(path.join(root, "web/fm1ports.js"), "utf8").replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const FILL = { NAME: "Template", ID: "FM-1_11", MAGIC: "TSTB", BACKUP_VERSION: "3", EXT: "bak", HOLDS: "everything", EXTRA: "", EXTRAS_JS: "const EXTRAS = [];", TERMINAL: "", PORTS_JS };
let html = fs.readFileSync(path.join(root, "web/backup/index.html"), "utf8");
html = html.replace(/\{\{([A-Z_]+)\}\}/g, (m, k) => (k in FILL ? FILL[k] : m));
const unfilled = html.match(/\{\{[A-Z_]+\}\}/g);
const m = /\/\/ --- backup protocol: begin[^\n]*\n([\s\S]*?)\/\/ --- backup protocol: end/.exec(html);
if (!m) { console.log("FAIL no backup protocol markers in web/backup/index.html"); process.exit(1); }
if (unfilled) { console.log(`FAIL web/backup/index.html: ${unfilled.join(" ")}, not make_site.py's`); process.exit(1); }
const PB = new Function(`"use strict";\n${m[1]}\nreturn PB;`)();
Object.assign(PB.DELAY, { poll: 5, idle: 300, ask: 500, first: 300, hs: 100, hsFull: 100 });

let fails = 0;
const check = (ok, msg) => { console.log(`${ok ? "ok  " : "FAIL"} ${msg}`); if (!ok) fails++; };
const hex = (a) => Buffer.from(a).toString("hex");
const same = (a, b) => a.length === b.length && Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0;
const crc = (b) => zlib.crc32 ? zlib.crc32(Buffer.from(b)) >>> 0 : null;

const tr = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const img = Uint8Array.from(Buffer.from(tr.image, "hex")), other = Uint8Array.from(Buffer.from(tr.other, "hex"));

// the page's CRC-32 is zlib's
if (crc(img) !== null) check(PB.crc32(img) === crc(img) && PB.crc32(other) === crc(other), "CRC-32 as zlib's");

// ---------------------------------------------------------------- 1. the tool's conversation, replayed
class ReplayLink {
  constructor(log) { this.log = log; this.i = 0; this.q = []; this.lost = false; this.bad = null; this.deliver(); }
  deliver() { while (this.i < this.log.length && this.log[this.i][0] === "dev") this.q.push([...Buffer.from(this.log[this.i++][1], "hex")]); }
  send(pkt) {
    const want = this.log[this.i];
    if (!this.bad && (!want || want[0] !== "host" || want[1] !== hex(pkt))) this.bad = `message ${this.i}: sent ${hex(pkt).slice(0, 40)}..., the tool sent ${want ? want[1].slice(0, 40) : "nothing"}...`;
    this.i++;
    this.deliver();
    return true;
  }
  drain() { this.q.length = 0; }
  async read() { await null; return this.q.shift() || null; }
  close() {}
  get done() { return this.i === this.log.length; }
}
const access1 = { inputs: new Map([["i", { name: "TEMPLATE FM-1", state: "connected", open: async () => {} }]]),
                  outputs: new Map([["o", { name: "TEMPLATE FM-1", state: "connected", open: async () => {} }]]) };
{
  const link = new ReplayLink(tr.runs.backup);
  const dev = await PB.find(access1, () => link);
  const r = await PB.backup(dev.link);
  check(!link.bad && link.done, `backup: every message as fm1_backup.py's (${tr.runs.backup.length})` + (link.bad ? ": " + link.bad : ""));
  check(same(r.image, img) && r.flags === 0, "backup: the file byte for byte as the tool's");
}
{
  const link = new ReplayLink(tr.runs.restore);
  let asked = 0;
  const dev = await PB.find(access1, () => link);
  const code = await PB.restore(dev.link, img, (w) => { if (w === "ask") asked++; });
  check(!link.bad && link.done, `restore: every message as fm1_backup.py's (${tr.runs.restore.length})` + (link.bad ? ": " + link.bad : ""));
  check(code === 0 && asked === 1, "restore: done, the FM-1's question passed on once");
}
{
  const link = new ReplayLink(tr.runs.restore_playing);
  const dev = await PB.find(access1, () => link);
  const code = await PB.restore(dev.link, img);
  check(!link.bad && link.done && code === PB.PLAYING, "restore while playing: STOP FIRST, as the tool");
}

// ---------------------------------------------------------------- 2. a simulated FM-1
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const le32 = (v) => [v & 255, v >>> 8 & 255, v >>> 16 & 255, v >>> 24 & 255];
function devFrame(cmd, body) {                                  // backup.c's bu_frame
  const s = body.reduce((a, v) => a + v, 0);
  return [0xF0, ...PB.pack7([0, 0x59, cmd, body.length & 255, body.length >> 8, 0, ...body, ~s & 255]), 0xF7];
}
class FakeFM1 {                                                // its app's magic, version, count: its image's
  constructor(image, o = {}) {
    Object.assign(this, { image, identity: "FM-1_1100", playing: false, answer: true, stallAfter: null, dropFrame: null }, o);
    this.magic = Buffer.from(image.subarray(0, 4)).toString("latin1"); this.version = image[4] | image[5] << 8; this.nobj = image[6] | image[7] << 8;
    this.req = null; this.asked = 0; this.reads = 0; this.q = []; this.lost = false;
  }
  // the Link the page gets
  send(pkt) { this.rx(pkt); return true; }
  drain() { this.q.length = 0; }
  async read(ms) { if (!this.q.length) await new Promise((r) => setTimeout(r, Math.min(ms, 2))); return this.q.shift() || null; }
  close() {}
  stream() {                                                   // bu_step: 128-byte frames, then the end frame
    let n = 0;
    for (let off = 0; off < this.image.length; off += 128, n++) {
      if (n === this.dropFrame) continue;
      this.q.push(devFrame(PB.BU_REQ, [...le32(off), ...this.image.subarray(off, off + 128)]));
    }
    this.q.push(devFrame(PB.BU_END, [...le32(this.image.length), ...le32(PB.crc32(this.image)), 0, 0]));
  }
  *puller(n, c) {                                              // restore_rx's reads
    if (this.playing) { yield [PB.FAIL_ADDR | PB.PLAYING, 8]; return; }
    const got = [...(yield [0, 16])];
    const h = Uint8Array.from(got);
    if (Buffer.from(h.subarray(0, 4)).toString("latin1") !== this.magic || (h[4] | h[5] << 8) !== this.version ||
        (h[6] | h[7] << 8) !== this.nobj || u32(h, 12) !== PB.crc32(h, 0, 12)) { yield [(PB.FAIL_ADDR | 3) >>> 0, 8]; return; }
    let pos = 16;
    for (let i = 0; i < this.nobj; i++) {
      const r = yield [pos, 8];
      got.push(...r);
      const ln = r[2] | r[3] << 8;
      pos += 8;
      for (let k = 0; k < ln; k += 512) got.push(...(yield [pos + k, Math.min(512, ln - k)]));
      pos += ln;
    }
    got.push(...(yield [pos, 4]));
    const g = Uint8Array.from(got);
    if (g.length !== n || PB.crc32(g) !== c) { yield [PB.FAIL_ADDR | 6, 8]; return; }
    try { PB.parse(g); } catch (_) { yield [(PB.FAIL_ADDR | 5) >>> 0, 8]; return; }
    yield [PB.ASK_ADDR, 8];
    this.asked++;
    if (!this.answer) { yield [(PB.FAIL_ADDR | PB.DECLINED) >>> 0, 8]; return; }
    this.image = g;
    yield [PB.OK_ADDR, 8];
  }
  ask(addr, n) {
    const q = [0, ...le32(addr >>> 0), n & 255, n >> 8, 0];
    this.q.push([0xF0, ...PB.pack7([0, 0x59, 0x30, 8, 0, 0, ...q, ~q.reduce((a, v) => a + v, 0) & 255]), 0xF7]);
    this.wanted = [addr >>> 0, n];
  }
  next(data) {
    const r = this.pull.next(data);
    if (r.done) { this.pull = null; return; }
    this.ask(...r.value);
  }
  rx(d) {
    // newOta: chip/ota.c from M-UPGRADE 3.11's fix on, NAME_ and three digits to HS_QUERY, the whole to HS_FULL
    const reply = (text) => {
      const t = [...Buffer.from(text), ...new Array(27 - text.length).fill(0)];
      t[24] = 0xB3, t[25] = 0x60, t[26] = 0x14;                  // felucca-bsp#29: the flash's JEDEC id after the text
      this.q.push([0xF0, ...PB.pack7([0, 0x59, 0x11, 27, 0, 0, ...t, ~t.reduce((a, v) => a + v, 0) & 255]), 0xF7]);
    };
    if (same(d, PB.HS_QUERY)) {
      reply(this.newOta ? this.identity.slice(0, this.identity.indexOf("_") + 4) : this.identity);
      return;
    }
    if (same(d, PB.HS_FULL)) {
      this.hsFull = (this.hsFull || 0) + 1;
      if (this.newOta) reply(this.identity);
      return;
    }
    const f = PB.parseFrame(d);
    if (f && f.cmd === PB.BU_REQ && Buffer.from(f.body).toString("latin1") === this.magic) { this.stream(); return; }
    if (f && f.cmd === PB.RS_REQ && f.body.length === 8) {
      this.req = [u32(f.body, 0), u32(f.body, 4)];
      this.pull = this.puller(...this.req);
      this.next(undefined);
      return;
    }
    const u = PB.unpack7(d.slice(1, -1));
    if (this.pull && this.wanted && u.length >= 15 && u[2] === 0x30) {
      const [addr, n] = this.wanted;
      if (u32(u, 7) !== addr || (u[11] | u[12] << 8) !== n) return;
      if (this.stallAfter !== null && ++this.reads > this.stallAfter) return;
      this.next(Uint8Array.from(u.slice(14, 14 + n)));
    }
  }
}
const accessOf = (dev) => ({ inputs: new Map([["i", { name: "TEMPLATE FM-1", state: "connected", open: async () => {} }]]),
                             outputs: new Map([["o", { name: "TEMPLATE FM-1", state: "connected", open: async () => {} }]]), dev });
const connect = (dev) => PB.find(accessOf(dev), () => dev);
const err = async (p) => { try { await p; return null; } catch (e) { return e; } };

{
  const dev = new FakeFM1(img);
  const r = await PB.backup((await connect(dev)).link);
  check(same(r.image, img), "simulated FM-1: backup byte for byte");
  const back = new FakeFM1(other);
  const code = await PB.restore((await connect(back)).link, r.image);
  check(code === 0 && same(back.image, img) && back.asked === 1, "simulated FM-1: restore, the device holds the backup byte for byte");
  check(back.req[0] === img.length && back.req[1] === PB.crc32(img), "... the request: its size and CRC-32");
}
{
  const dev = new FakeFM1(other, { playing: true });
  const code = await PB.restore((await connect(dev)).link, img);
  check(code === PB.PLAYING && same(dev.image, other), "playing: STOP FIRST, nothing changed");
}
{
  const dev = new FakeFM1(other, { answer: false });
  const code = await PB.restore((await connect(dev)).link, img);
  check(code === PB.DECLINED && same(dev.image, other) && dev.asked === 1, "HOME on the FM-1: not restored, nothing changed");
}
{
  const bad = img.slice(); bad[bad.length >> 1] ^= 1;
  let e = null; try { PB.parse(bad); } catch (x) { e = x; }
  check(e && /damaged/.test(e.message), "a damaged file: refused (" + (e && e.message) + ")");
  const dev = new FakeFM1(other);
  const e2 = await err(PB.restore((await connect(dev)).link, bad));
  check(e2 && dev.req === null && same(dev.image, other), "... nothing sent");
}
const resealed = (b) => {                                      // its CRCs right again
  b.set(le32(PB.crc32(b.subarray(0, 12))), 12);
  b.set(le32(PB.crc32(b.subarray(0, b.length - 4))), b.length - 4);
  return b;
};
{
  const old = img.slice();                                     // version 2, its CRCs right
  old[4] = 2; old[5] = 0;
  resealed(old);
  let e = null; try { PB.parse(old); } catch (x) { e = x; }
  check(e && /older Template/.test(e.message), "an older version's backup (version 2): refused, said so");
  const dev = new FakeFM1(other);
  await err(PB.restore((await connect(dev)).link, old));
  check(dev.req === null && same(dev.image, other), "... nothing sent");
  check(PB.VERSION === 3 && (img[4] | img[5] << 8) === 3 && PB.MAGIC.join() === [...Buffer.from("TSTB")].join(),
        "the page's magic and version: the app's (TSTB, 3)");
}
{
  const ply = img.slice();                                     // another firmware's (its magic), its CRCs right
  ply.set([...Buffer.from("PLYB")], 0);
  resealed(ply);
  let e = null; try { PB.parse(ply); } catch (x) { e = x; }
  check(e && /not a Template backup/.test(e.message), "another firmware's backup: not a Template backup");
  e = null; try { PB.parse(Uint8Array.from(Buffer.from("not a backup at all".repeat(20)))); } catch (x) { e = x; }
  check(e && /not a Template backup/.test(e.message), "another file: not a Template backup");
}
{
  const dev = new FakeFM1(other, { identity: "FM-1_906" });
  const e = await err(connect(dev));
  check(e && e.code === "model" && /not Template/.test(e.message), "another firmware on the FM-1 (X0X's identity): refused");
}
{
  const dev = new FakeFM1(img, { identity: "FM-1_11000300", newOta: true });
  const c = await connect(dev);
  check(c && c.id === "FM-1_11000300" && dev.hsFull === 1,
        "a firmware that answers FM-1_110 to the plain query (M-UPGRADE 3.11): its whole identity asked with 01");
  c.link.close();
}
{
  const dev = new FakeFM1(other, { stallAfter: 5 });
  const e = await err(PB.restore((await connect(dev)).link, img));
  check(e && e.code === "stopped" && same(dev.image, other), "the FM-1 stops asking: stopped, nothing changed");
}
{
  const dev = new FakeFM1(img, { dropFrame: 3 });
  const e = await err(PB.backup((await connect(dev)).link));
  check(e && e.code === "lost", "a backup frame lost on the way: said, nothing saved");
}
{
  const empty = { inputs: new Map(), outputs: new Map() };
  const e = await err(PB.find(empty, () => null));
  check(e && e.code === "notfound", "no FM-1: not found");
}
{
  // an FM-1 under another device's name: not asked; listed for Try; asked once picked (allowed), or alone (only)
  const dev = new FakeFM1(img);
  const sent = {};
  const port = (name) => ({ name, state: "connected", open: async () => {}, addEventListener() {}, removeEventListener() {},
                            send: (b) => (sent[name] = sent[name] || []).push(b) });
  const acc = { inputs: new Map([["i", port("Arturia KeyStep")], ["j", port("Launchkey Mini")], ["k", port("IAC Bus 1")]]),
                outputs: new Map([["o", port("Arturia KeyStep")], ["p", port("Launchkey Mini")]]) };
  let made = 0;
  let e = await err(PB.find(acc, () => { made++; return dev; }));
  check(e && e.code === "notfound" && made === 0, "a port with another device's name: not asked");
  check(Object.values(sent).every((l) => l.length === 3 && l.every((b) => b.join() === "240,125,83,82,1,1,247")),
        "  only the screen reader's HELLO goes to it, three times");
  check(PB.others(acc).join() === "Arturia KeyStep,Launchkey Mini", "others: the pairs are listed to try (an input alone is not)");
  const one = await PB.find(acc, () => dev, [], "Arturia KeyStep");
  check(one && one.name === "Arturia KeyStep", "Try: asked alone, it answers");
  const got = await PB.find(acc, () => dev, ["Arturia KeyStep"]);
  check(got && got.name === "Arturia KeyStep" && PB.others(acc, ["Arturia KeyStep"]).join() === "Launchkey Mini", "picked: found from then on, no longer listed");
}
{
  // an FM-1 under another device's name that answers the screen reader's HELLO (every fm1-base firmware): found without
  // picking it; a loopback (an IAC bus: the HELLO comes back as it went) and other gear are never sent the identity query
  const dev = new FakeFM1(img);
  const port = (name, answer) => {
    const ears = new Set(), sent = [];
    const input = { name, state: "connected", open: async () => {}, addEventListener: (t, f) => ears.add(f), removeEventListener: (t, f) => ears.delete(f) };
    const hear = (b) => setTimeout(() => ears.forEach((f) => f({ data: Uint8Array.from(b), target: input })), 1);
    const output = { name, state: "connected", open: async () => {}, send: (b) => { sent.push(b); answer && answer(Array.from(b), hear); } };
    return { input, output, sent };
  };
  const fm1 = port("Arturia KeyStep", (b, hear) => b[4] === 1 && hear([0xF0, 0x7D, 0x53, 0x52, 1, 1, ...Buffer.from("Template 1.0"), 0, 0xF7]));
  const iac = port("IAC Bus 1", (b, hear) => hear(b)), other = port("Launchkey Mini");
  const all = [iac, other, fm1];
  const acc = { inputs: new Map(all.map((p, i) => ["i" + i, p.input])), outputs: new Map(all.map((p, i) => ["o" + i, p.output])) };
  const linked = [];
  const got = await PB.find(acc, (i) => { linked.push(i.name); return dev; });
  check(got && got.name === "Arturia KeyStep" && linked.join() === "Arturia KeyStep", "an FM-1 answering HELLO on another device's port: found, and only it asked");
  check(iac.sent.length === 3 && other.sent.length === 3 && fm1.sent.length === 1, "  the loopback and the other gear: three HELLOs, nothing more");
}
{
  // a Bluetooth MIDI port (an FM-1 paired beside its USB cable) is never asked, allowed, tried or listed
  const dev = new FakeFM1(img);
  const port = (name) => ({ name, state: "connected", open: async () => {} });
  const acc = { inputs: new Map([["i", port("Optimist 112D Bluetooth")], ["j", port("FM-1")]]),
                outputs: new Map([["o", port("Optimist 112D Bluetooth")], ["p", port("FM-1")]]) };
  const asked = [];
  const got = await PB.find(acc, (i) => { asked.push(i.name); return dev; }, ["Optimist 112D Bluetooth"]);
  check(got && got.name === "FM-1" && !asked.includes("Optimist 112D Bluetooth"), "Bluetooth port: skipped, the USB port is found");
  check(!PB.others(acc, []).length, "Bluetooth port: not listed for Try");
  const only = { inputs: new Map([["i", port("Optimist 112D Bluetooth")]]), outputs: new Map([["o", port("Optimist 112D Bluetooth")]]) };
  const e = await err(PB.find(only, (i) => { asked.push(i.name); return dev; }, ["Optimist 112D Bluetooth"], "Optimist 112D Bluetooth"));
  check(e && e.code === "notfound" && !asked.includes("Optimist 112D Bluetooth"), "Bluetooth port: not even Try asks it");
}
{
  // an FM-1 under the name of a firmware of the family (Optimist, SLOOP, Melodee, LOOM-8) is asked by itself
  for (const name of ["Optimist", "SLOOP 2.4", "Melodee", "LOOM-8"]) {
    const dev = new FakeFM1(img);
    const port = (n) => ({ name: n, state: "connected", open: async () => {} });
    const acc = { inputs: new Map([["i", port("Launchkey Mini")], ["j", port(name)]]), outputs: new Map([["o", port("Launchkey Mini")], ["p", port(name)]]) };
    const asked = [];
    const got = await PB.find(acc, (i) => { asked.push(i.name); return dev; });
    check(got && got.name === name && !asked.includes("Launchkey Mini") && !PB.others(acc).includes(name), `family port "${name}": asked by itself, other gear not`);
  }
}
{
  // the firmware's own sections after the backup (EXTRAS): the page's join and split against fm1_backup.py's
  const secs = [{ tag: "LSMP", data: Uint8Array.from({ length: 2304 }, (_, i) => i & 0xFF) }, { tag: "ZZZZ", data: new Uint8Array(0) }];
  const withsec = Uint8Array.from(Buffer.from(tr.withsec, "hex"));
  const joined = PB.join(img, secs);
  check(same(joined, withsec), "join: the file fm1_backup.py's join makes, byte for byte");
  const sp = PB.split(withsec);
  check(same(sp.image, img) && sp.sections.length === 2 && sp.sections[0].tag === "LSMP" && same(sp.sections[0].data, secs[0].data) &&
        sp.sections[1].tag === "ZZZZ" && !sp.sections[1].data.length, "split: the backup and its sections");
  check(PB.split(img).sections.length === 0 && same(PB.split(img).image, img), "split: a file without sections");
  let e = null; try { PB.split(withsec.subarray(0, withsec.length - 1)); } catch (x) { e = x; }
  check(e && e.code === "file" && /cut short/.test(e.message), "split: a section cut short");
  const bad = Uint8Array.from(withsec); bad[img.length + 20] ^= 1;
  e = null; try { PB.split(bad); } catch (x) { e = x; }
  check(e && e.code === "file" && /LSMP/.test(e.message), "split: a section damaged");
  const dev = new FakeFM1(other);
  const code = await PB.restore((await connect(dev)).link, PB.split(withsec).image);
  check(code === 0 && same(dev.image, img), "restore of the backup split from a file with sections");
}
console.log(fails ? `backup page test FAILED (${fails})` : "backup page test passed");
process.exit(fails ? 1 : 0);
