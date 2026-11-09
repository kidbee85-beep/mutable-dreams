// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "midi" (fm1.toml emu = ["midi"]), the worklet's side: MIDI bytes from the page (whole messages, as
// Web MIDI gives them; running status too) into the device as its USB port takes them: channel and system messages
// as USB-MIDI packets (web_midi_in, PORT_USB), a SysEx frame whole to the app (web_sx_in, an app with APP_SYSEX;
// dropped otherwise, as a device without a SysEx handler drops it). Out, before each render quantum: what the app
// sent (web_out_pkts, as USB-MIDI packets; web_sx_out_*, its SysEx frames) as whole messages to the page,
// { msgs: [Uint8Array, ...] }. The page's side is midi.js.

const PORT_USB = 0;
const dataLen = (s) => (s >= 0xf8 || s === 0xf6 ? 0 : s === 0xf1 || s === 0xf3 || (s & 0xe0) === 0xc0 ? 1 : 2);
const CIN_BYTES = [0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1];   // a USB-MIDI packet's bytes by its code index

function packet(m) {
  const s = m[0];
  const cin = s < 0xf0 ? s >> 4 : s >= 0xf8 ? 0xf : s === 0xf2 ? 3 : s === 0xf6 ? 5 : 2;
  return (cin | s << 8 | (m[1] || 0) << 16 | (m[2] || 0) << 24) >>> 0;
}

// the bytes in: SysEx collected, channel messages with running status, real time anywhere
let sx = null, status = 0, cur = [];
function feed(fm1, bytes) {
  const ex = fm1.ex;
  for (const b of bytes) {
    if (b >= 0xf8) { ex.web_midi_in(PORT_USB, packet([b])); continue; }
    if (b === 0xf0) { sx = [b]; continue; }
    if (sx) {
      if (b < 0x80 || b === 0xf7) {
        sx.push(b);
        if (b === 0xf7) { sysexIn(ex, sx); sx = null; }
        continue;
      }
      sx = null;                            // (a status byte inside: the frame is lost, the status taken)
    }
    if (b & 0x80) {
      if (b === 0xf7) continue;
      status = b < 0xf0 ? b : 0;
      cur = [b];
    } else if (cur.length) {
      cur.push(b);
    } else if (status) {
      cur = [status, b];
    } else {
      continue;
    }
    if (cur.length === 1 + dataLen(cur[0])) { ex.web_midi_in(PORT_USB, packet(cur)); cur = []; }
  }
}
function sysexIn(ex, frame) {
  if (!ex.web_sx_in || frame.length > ex.web_sx_in_max()) return;
  new Uint8Array(ex.memory.buffer, ex.web_sx_in_buf(), frame.length).set(frame);
  ex.web_sx_in(frame.length);
}

// the bytes out: packets (a SysEx in packets too, CIN 4..7) and the app's frames, as whole messages
let osx = null;
function drain(fm1) {
  const ex = fm1.ex, msgs = [];
  const n = ex.web_out_n();
  if (n) {
    const pk = new Uint32Array(ex.memory.buffer, ex.web_out_pkts(), n);
    for (const p of pk) {
      const cin = p & 0xf, bytes = [p >> 8 & 0xff, p >> 16 & 0xff, p >>> 24].slice(0, CIN_BYTES[cin]);
      if (cin >= 4 && cin <= 7) {
        for (const b of bytes) {
          if (b === 0xf0) osx = [];
          if (osx) osx.push(b);
          if (b === 0xf7 && osx) { msgs.push(Uint8Array.from(osx)); osx = null; }
        }
      } else if (bytes.length) {
        msgs.push(Uint8Array.from(bytes));
      }
    }
    ex.web_out_clear();
  }
  if (ex.web_sx_out_len && ex.web_sx_out_len()) {
    const all = new Uint8Array(ex.memory.buffer, ex.web_sx_out_buf(), ex.web_sx_out_len());
    for (let at = 0; at < all.length;) {
      let k = at + 1;
      while (k < all.length && all[k] !== 0xf7 && all[k] !== 0xf0) k++;
      const end = k < all.length && all[k] === 0xf7 ? k + 1 : k;   // (a frame cut short: up to the next F0)
      if (all[end - 1] === 0xf7) msgs.push(all.slice(at, end));
      at = end;
    }
    ex.web_sx_out_clear();
  }
  if (msgs.length) fm1.part("midi", { msgs }, msgs.map((m) => m.buffer));
}

fm1Part({
  name: "midi",
  message(fm1, m) { if (m.data) feed(fm1, m.data); },
  process(fm1) { drain(fm1); },
});
