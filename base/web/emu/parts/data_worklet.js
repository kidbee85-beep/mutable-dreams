// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "data" (fm1.toml emu = ["data"]), the worklet's side: the data area (plat_data and its windows,
// web_data: what an app keeps beyond its saved objects, as on the FM-1's flash) put back before the device boots
// from what the page kept, and sent to the page when it changed, looked at every 2 s of the page's time and when the
// page asks ({ flush: 1 }); { dump: 1 } sends it as it is, changed or not ({ dump: 1 } back). The page's side is
// data.js.

const EVERY_S = 2;
let snap = null, lastLook = 0;

function area(ex) {
  return new Uint8Array(ex.memory.buffer, ex.web_data(), ex.web_data_len());
}
function changed(now) {                  // (a word at a time where it can)
  if (now.byteOffset & 3 || now.length & 3) return now.some((v, i) => v !== snap[i]);
  const a = new Uint32Array(now.buffer, now.byteOffset, now.length >> 2), b = new Uint32Array(snap.buffer);
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return true;
  return false;
}
function send(fm1, always, extra = {}) {
  const now = area(fm1.ex);
  if (!always && !changed(now)) return;
  snap.set(now);
  const copy = now.slice();
  fm1.part("data", { ...extra, data: copy.buffer, base: fm1.ex.web_data_base() }, [copy.buffer]);
}

fm1Part({
  name: "data",
  load(fm1, kept) {
    if (!fm1.ex.web_data) return;        // (built without FM1B_EMU_DATA)
    const now = area(fm1.ex);
    if (kept instanceof ArrayBuffer && kept.byteLength === now.length) now.set(new Uint8Array(kept));
    snap = now.slice();
  },
  message(fm1, m) {
    if (snap && (m.flush || m.dump)) send(fm1, !!m.dump, m.dump ? { dump: 1 } : {});
  },
  process(fm1) {
    if (!snap || currentTime - lastLook < EVERY_S) return;
    lastLook = currentTime;
    send(fm1, false);
  },
});
