// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "reboot" (fm1.toml emu = ["reboot"]), the worklet's side: what the FM-1 does when the app calls
// plat_restart, or the page asks for a restart or the update mode. The wasm is started again from its own bytes (the
// page's side hands them over at load), so the app's RAM is forgotten as an FM-1's is; the saved objects (plat_store)
// and, with the part "data", the data area go into the new start, and so do the held buttons, MASTER and the screen
// reader's listening. REC held when it starts is the chip's update mode: the sound stopped, the screen black with
// UPDATE MODE (REC), nothing runs until a restart. The page's side is reboot.js.
// Messages to the page: { state: "running" | "restarting" | "update", why }.

let wasm = null, busy = false, carry = null, ready = 0;

function snapshot(fm1) {
  const ex = fm1.ex, store = {};
  for (let o = 0; o < ex.web_nobj(); o++) {
    const n = ex.web_store_len(o);
    if (n >= 0) store[o] = new Uint8Array(fm1.mem.buffer, ex.web_store(o), n).slice().buffer;
  }
  const data = ex.web_data ? new Uint8Array(fm1.mem.buffer, ex.web_data(), ex.web_data_len()).slice().buffer : null;
  return { store, data, master: ex.web_master_get(), held: ex.web_buttons_get(), listen: ex.web_listening() };
}

function restart(fm1, why) {
  if (busy || !wasm) return;
  busy = true;
  const s = snapshot(fm1);
  carry = { held: s.held, listen: s.listen };
  fm1.part("reboot", { state: "restarting", why });
  const parts = { ...(fm1.partData || {}), reboot: { wasm } };
  if (s.data) parts.data = s.data;
  fm1.onMessage({ type: "load", wasm: wasm.slice(0), store: s.store, master: s.master, parts });
}

fm1Part({
  name: "reboot",
  load(fm1, kept) {
    if (kept && kept.wasm) wasm = kept.wasm;
    busy = false;
    ready = 0;
    if (carry) fm1.ex.web_buttons(carry.held >>> 0);      // (before the device boots: REC held is the update mode)
  },
  message(fm1, m) {
    if (m.cmd === "restart") restart(fm1, "page");
    else if (m.cmd === "update" && !busy) {
      fm1.ex.web_update_enter(1);
      fm1.part("reboot", { state: "update", why: "UBOOT (USB)" });
    }
  },
  process(fm1) {
    const ex = fm1.ex;
    if (!ex.web_restarts || busy) return;
    if (!ready) {                                           // the first quantum of a start
      ready = 1;
      if (carry && carry.listen) ex.web_reader(1);
      carry = null;
      const u = ex.web_update();
      fm1.part("reboot", u ? { state: "update", why: u === 2 ? "UPDATE MODE (REC)" : "UBOOT (USB)" } : { state: "running" });
    }
    if (ex.web_restarts() > 0) restart(fm1, "app");
  },
});
