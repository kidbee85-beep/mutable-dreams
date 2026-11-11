// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// Adapted from fm1-base/web/backup; exact Mutable Dreams object map enforced.
// --- backup protocol: begin (tools/fm1_backup.py, device/backup.c; tested by tests/backup_page_test.mjs)
const FW = { name: "Mutable Dreams", id: "FM-1_710", magic: "MDDB", version: Number("1"), ext: "mddb" };
const PB = (() => {
  const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];   // fm1_install.py / fm1ota.js
  const HS_FULL = frame(0x11, [1]);
  const RS_REQ = 0x41, BU_REQ = 0x42, BU_END = 0x43;
  const OK_ADDR = 0xC0000000, ASK_ADDR = 0xC1000000, FAIL_ADDR = 0xCF000000;
  const NAME = FW.name, MAGIC = [...FW.magic].map((c) => c.charCodeAt(0)), VERSION = FW.version;   // app.h's
  const HEAD = 16, REC = 8, PAYLOAD_MAX = 3840, MAXDATA = 512;
  const PLAYING = 10, DECLINED = 11;
  const ID = FW.id;                                                // the firmware's package identity (its prefix)
  const DELAY = { poll: 500, idle: 10000, ask: 75000, first: 3000, hs: 1000 };   // ms, as fm1_backup.py
  const ERRORS = {
    1: "the file's size", 2: "the transfer (a piece of the file did not arrive)", 3: "the file's head",
    4: "a part's header", 5: "a part's check (a byte changed on the way)", 6: "the whole file's check",
    7: `erasing ${NAME}'s memory`, 8: `writing ${NAME}'s memory`, 9: "reading back what was written",
    10: `${NAME} is playing`, 11: "Restore was cancelled on the FM-1 or its confirmation timed out",
    12: `writing the last step (${NAME} finishes it when it's switched on again)`,
  };

  function fail(code, msg) { const e = new Error(msg); e.code = code; return e; }

  function pack7(data) {
    const out = []; let acc = 0, nb = 0;
    for (const b of data) { acc |= b << nb; nb += 8; while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; } }
    if (nb) out.push(acc & 0x7F);
    return out;
  }
  function unpack7(s) {
    const out = []; let acc = 0, nb = 0;
    for (const b of s) { acc |= (b & 0x7F) << nb; nb += 7; while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; } }
    return out;
  }
  const CRC_T = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
    return t;
  })();
  function crc32(bytes, start = 0, end = bytes.length) {          // zlib's
    let c = 0xFFFFFFFF;
    for (let i = start; i < end; i++) c = CRC_T[(c ^ bytes[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }
  const u16 = (b, o) => b[o] | b[o + 1] << 8;
  const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
  const le32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];

  // a host message in the update's framing (ota.c ota_take)
  function frame(cmd, body) {
    const n = body.length; let s = 0;
    for (const b of body) s += b;
    return [0xF0, ...pack7([0x00, 0x59, cmd, n & 0xFF, (n >> 8) & 0xFF, 0, ...body, ~s & 0xFF]), 0xF7];
  }
  // a device message -> {cmd, body} or null
  function parseFrame(pkt) {
    if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
    const u = unpack7(pkt.slice(1, -1));
    if (u.length < 7 || u[0] !== 0 || u[1] !== 0x59) return null;
    const n = u[3] | u[4] << 8 | u[5] << 16;
    if (n + 7 !== u.length) return null;
    let s = 0;
    for (let i = 6; i < 6 + n; i++) s += u[i];
    if ((~s & 0xFF) !== u[6 + n]) return null;
    return { cmd: u[2], body: u.slice(6, 6 + n) };
  }
  // the answer to a read request (fm1_install.py response)
  function response(addr, data, fl = 0) {
    const n = data.length;
    const body = [0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl, ...le32(addr), n & 0xFF, (n >> 8) & 0xFF, 0, ...data];
    let s = 0;
    for (let i = 6; i < body.length; i++) s += body[i];
    body.push(~s & 0xFF);
    return [0xF0, ...pack7(body), 0xF7];
  }
  function parseRequest(pkt) {
    if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
    const u = unpack7(pkt.slice(1, -1));
    if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    if ((~s & 0xFF) !== u[14]) return null;
    return { fl: u[6], addr: u32(u, 7), len: u[11] | u[12] << 8 | u[13] << 16 };
  }
  function parseIdentity(pkt) {
    const f = parseFrame(pkt);
    if (!f || f.cmd !== 0x11 || f.body.length !== 27) return null;
    // The last three bytes are the flash chip ID. The name ends at its first zero.
    const txt = String.fromCharCode(...f.body.slice(0,24)).split("\0",1)[0];
    return /^[^_]+_\d+$/.test(txt) ? txt : null;
  }

  // a backup file checked whole -> {flags, objs: [{obj, data}]}; throws (code "file") with what is wrong in plain words
  function parse(image) {
    const bad = (m) => fail("file", m);
    if (image.length < HEAD + 4) throw bad(`This file is too short to be a ${NAME} backup.`);
    if (MAGIC.some((b, i) => image[i] !== b)) throw bad(`This file is not a ${NAME} backup.`);
    const ver = u16(image, 4), n = u16(image, 6), flags = u32(image, 8);
    if (u32(image, 12) !== crc32(image, 0, 12)) throw bad("This backup is damaged (its head).");
    if (ver < VERSION) throw bad(`This is a backup from an older ${NAME} (version ${ver}). This ${NAME} can't restore it. Nothing was changed.`);
    if (ver !== VERSION) throw bad(`This is a backup from another version of ${NAME} (version ${ver}). This ${NAME} can't restore it.`);
    if (image.length < HEAD + n * REC + 4) throw bad("This backup is cut short.");
    const expected=[0,2,...Array.from({length:20},(_,i)=>i+3)];
    if(n!==expected.length)throw bad("This backup has the wrong set of saved banks.");
    const objs = []; let pos = HEAD;
    for (let k = 0; k < n; k++) {
      if (pos + REC > image.length) throw bad("This backup is cut short.");
      const obj = u16(image, pos), ln = u16(image, pos + 2), crc = u32(image, pos + 4);
      if(obj!==expected[k])throw bad("This backup has the wrong object order.");
      pos += REC;
      if (ln > PAYLOAD_MAX || pos + ln + 4 > image.length) throw bad(`This backup is damaged (part ${k + 1}).`);
      if (crc32(image, pos, pos + ln) !== crc) throw bad(`This backup is damaged (part ${k + 1}: a byte changed).`);
      objs.push({ obj, data: image.subarray(pos, pos + ln) });
      pos += ln;
    }
    if (pos + 4 !== image.length || u32(image, pos) !== crc32(image, 0, pos)) throw bad("This backup is damaged (the whole file's check).");
    return { flags, objs };
  }
  function summary(objs) {
    const used = objs.filter((o) => o.data.length).length;
    const total = objs.reduce((a, o) => a + o.data.length, 0);
    return `${used} of ${objs.length} parts with something saved (${total.toLocaleString("en")} bytes)`;
  }

  async function handshake(link) {
    link.drain();
    for (const query of [HS_FULL, HS_FULL, HS_QUERY]) {
      if (!link.send(query)) return null;
      const end = Date.now() + DELAY.hs;
      while (Date.now() < end) {
        const p = await link.read(end - Date.now());
        if (!p) break;
        const id = parseIdentity(p);
        if (id) return id;
      }
    }
    return null;
  }

  // the first MIDI port pair that answers as an FM-1 (ports named after the firmware first); a Link from makeLink(input, output)
  const NAME_RE = new RegExp(NAME.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"), "i");
  async function find(access, makeLink) {
    const ins = [...access.inputs.values()].filter((p) => p.state !== "disconnected" &&
      (NAME_RE.test(p.name || "") || /fm-?1|felucca|composite|sinco|usb-midi/i.test(p.name || "")));   // never probe other gear
    ins.sort((a, b) => NAME_RE.test(b.name) - NAME_RE.test(a.name));
    const outs = [...access.outputs.values()].filter(p=>p.state!=="disconnected");
    if(ins.some(p=>ins.filter(q=>q.name===p.name).length>1)||outs.some(p=>outs.filter(q=>q.name===p.name).length>1))throw fail("ambiguous","Two MIDI devices have identical names. Disconnect the other FM-1 before connecting this editor; no controls were sent.");
    const base = (s) => (s || "").replace(/\s+\d+$/, "");
    let other = null;
    for (const input of ins) {
      const same = outs.filter((o) => base(o.name) === base(input.name));
      const output = outs.find((o) => o.name === input.name) || (same.length === 1 ? same[0] : null) || (outs.length === 1 ? outs[0] : null);
      if (!output) continue;
      try { await input.open(); await output.open(); } catch (_) { continue; }
      const link = makeLink(input, output);
      const id = await handshake(link);
      if (id && id.startsWith(ID)) return { link, id, name: input.name };
      if (id) other = other || id;
      link.close();
    }
    if (other) throw fail("model", `The FM-1 runs other firmware (${other}), not ${NAME}. Install ${NAME} first.`);
    throw fail("notfound", `${NAME} not found. Plug the FM-1 in with a USB data cable, close other apps that use its MIDI, and try again.`);
  }

  // the device's backup frames until its end frame -> {image, flags} (checked)
  async function backup(link, onProgress = () => {}) {
    link.drain();
    link.send(frame(BU_REQ, MAGIC));
    const got = []; let last = Date.now(), first = true;
    for (;;) {
      const pkt = await link.read(DELAY.poll);
      if (!pkt) {
        if (link.lost) throw fail("lost", "The FM-1 was disconnected. Plug it in and press Back up again.");
        if (Date.now() - last > (first ? DELAY.first : DELAY.idle)) {
          throw fail("stopped", first ? `${NAME} didn't answer. If it's an older ${NAME} without backup, update it on the Install page.`
                                      : `${NAME} stopped sending. Press Back up again.`);
        }
        continue;
      }
      const f = parseFrame(pkt);
      if (!f) continue;
      last = Date.now();
      if (f.cmd === BU_REQ && f.body.length >= 4) {
        first = false;
        if (u32(f.body, 0) !== got.length) throw fail("lost", "A piece of the backup went missing on the way. Press Back up again.");
        for (let i = 4; i < f.body.length; i++) got.push(f.body[i]);
        onProgress(got.length);
      } else if (f.cmd === BU_END && f.body.length >= 10) {
        const n = u32(f.body, 0), crc = u32(f.body, 4), status = f.body[8], flags = f.body[9];
        if (status) throw fail("flash", `${NAME} could not read its memory (status ${status}). Try again.`);
        const image = Uint8Array.from(got);
        if (n !== image.length || crc !== crc32(image)) throw fail("lost", "The backup arrived damaged. Press Back up again.");
        try { parse(image); } catch (e) { throw fail("lost", `The backup arrived damaged (${e.message}). Press Back up again.`); }
        return { image, flags };
      }
    }
  }

  // send a checked backup image: answer the FM-1's reads until it reports -> 0 restored, else its code (PLAYING, DECLINED, ...)
  async function restore(link, image, onEvent = () => {}) {
    parse(image);                                                  // (the page checked it already: never send a bad one)
    link.drain();
    link.send(frame(RS_REQ, [...le32(image.length), ...le32(crc32(image))]));
    let last = Date.now(), idle = DELAY.first, asked = 0;
    for (;;) {
      const pkt = await link.read(DELAY.poll);
      if (!pkt) {
        if (link.lost) throw fail("lost", `The FM-1 was disconnected. Reconnect and verify the saved banks before retrying; a confirmed restore may have completed.`);
        if (Date.now() - last > idle) {
          throw fail("stopped", asked ? `${NAME} stopped asking for the file. Reconnect and verify the banks before retrying.`
                                      : `${NAME} didn't answer. If it's an older ${NAME} without restore, update it on the Install page.`);
        }
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = Date.now(); idle = DELAY.idle; asked++;
      const top = (r.addr & 0xFF000000) >>> 0;
      if (top === OK_ADDR || top === FAIL_ADDR || top === ASK_ADDR) {
        link.send(response(r.addr, [0x73, 0x75, 0x63, 0x63, 0x65, 0x73, 0x73, 0], r.fl));   // "success\0"
        if (r.addr === ASK_ADDR) { idle = DELAY.ask; onEvent("ask"); continue; }
        return r.addr === OK_ADDR ? 0 : (r.addr & 0xFFFF) || 99;
      }
      if (r.len > MAXDATA || r.addr >= image.length) throw fail("badreq", `${NAME} asked for a part outside the file (${r.addr.toString(16)}+${r.len}). Nothing changed.`);
      const data = Array.from(image.subarray(r.addr, r.addr + r.len));
      while (data.length < r.len) data.push(0);
      if (!link.send(response(r.addr, data, r.fl))) throw fail("lost", "The FM-1 was disconnected. Nothing changed.");
      onEvent("progress", Math.min(r.addr + r.len, image.length) * 100 / image.length);
    }
  }

  return { HS_QUERY, RS_REQ, BU_REQ, BU_END, OK_ADDR, ASK_ADDR, FAIL_ADDR, VERSION, PLAYING, DECLINED, ID, NAME, MAGIC, DELAY, ERRORS,
           pack7, unpack7, crc32, frame, parseFrame, response, parseRequest, parseIdentity, parse, summary, find, backup, restore };
})();

class Link {
  constructor(input, output) {
    this.input = input; this.output = output; this.q = []; this.waiter = null;
    input.onmidimessage = (ev) => {
      const bytes = ev.data;
      if (bytes[0] !== 0xF0 || Link.isMirrorFrame(bytes)) return;
      this.q.push(Array.from(bytes));
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
  }
  static isMirrorFrame(bytes) {
    // Packed 00 59 header, then the mirror command (0x50..0x54).
    // Only the separate viewer needs the stream of display tiles and input reports.
    if (bytes.length < 6 || bytes[1] !== 0 || bytes[2] !== 0x32 || (bytes[3] & 3) !== 1) return false;
    const command = (bytes[3] >> 2) | ((bytes[4] & 7) << 5);
    return command >= 0x50 && command <= 0x54;
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  send(bytes) { try { this.output.send(bytes); return true; } catch (_) { return false; } }
  drain() { this.q.length = 0; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, Math.max(ms, 0)); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  close() { this.input.onmidimessage = null; }
}


globalThis.DreamsBackup={PB,Link};
