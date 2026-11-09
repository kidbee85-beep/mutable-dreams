// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "wasm" (fm1.toml emu = ["wasm"]): run another build of the app on this page: "Run a .wasm…" in the
// page's bar takes an app.wasm from this computer (tools/build.py APP --web makes one: APP/build/emu/app.wasm), checks
// it is an emulator's, keeps it in this browser (IndexedDB) and starts again on it; the page then says which file
// runs, and "Back to the built one" goes back to the page's own app.wasm. The saved state stays as it is: a build that
// cannot read what another left handles it as the FM-1 would.

const DB = "fm1-emu", STORE = "state";
const NEEDS = ["web_boot", "web_render", "web_out_l", "web_out_r", "web_fb", "memory"];   // (web/emu/emu_web.c)

function db() {
  return new Promise((resolve, reject) => {
    const r = indexedDB.open(DB, 1);
    r.onupgradeneeded = () => r.result.createObjectStore(STORE);
    r.onsuccess = () => resolve(r.result);
    r.onerror = () => reject(r.error);
  });
}
async function op(mode, fn) {
  const d = await db();
  return new Promise((resolve, reject) => {
    const t = d.transaction(STORE, mode), q = fn(t.objectStore(STORE));
    t.oncomplete = () => { d.close(); resolve(q && q.result); };
    t.onerror = t.onabort = () => { d.close(); reject(t.error); };
  });
}

/** "" when bytes are an emulator's app.wasm, else why not */
export async function why(bytes) {
  let mod;
  try { mod = await WebAssembly.compile(bytes); } catch { return "not a WebAssembly module"; }
  const have = new Set(WebAssembly.Module.exports(mod).map((e) => e.name));
  const missing = NEEDS.filter((n) => !have.has(n));
  return missing.length ? `not an fm1-base emulator's app.wasm (no ${missing.join(", ")})` : "";
}

export function attach(ctx) {
  const key = `${ctx.id}:wasm`, status = ctx.status || (() => {});
  let running = null;
  ctx.beforeLoad(async (msg) => {
    try {
      running = await op("readonly", (s) => s.get(key));
    } catch { running = null; }
    return running && running.bytes ? { ...msg, wasm: running.bytes.slice(0) } : msg;
  });
  if (!ctx.bar) return;
  const pick = document.createElement("input"), run = document.createElement("button"), back = document.createElement("button");
  const what = document.createElement("span");
  pick.type = "file";
  pick.accept = ".wasm,application/wasm";
  pick.hidden = true;
  run.type = back.type = "button";
  run.textContent = "Run a .wasm…";
  run.title = "Another build of this app, from this computer (APP/build/emu/app.wasm)";
  back.textContent = "Back to the built one";
  back.hidden = true;
  run.addEventListener("click", () => pick.click());
  pick.addEventListener("change", async () => {
    const f = pick.files[0];
    pick.value = "";
    if (!f) return;
    const bytes = await f.arrayBuffer(), no = await why(bytes);
    if (no) { status(`${f.name}: ${no}.`); return; }
    try {
      await op("readwrite", (s) => s.put({ name: f.name, size: bytes.byteLength, bytes }, key));
    } catch (e) { status(`${f.name} could not be kept in this browser: ${e && e.message || e}`); return; }
    location.reload();
  });
  back.addEventListener("click", async () => {
    try { await op("readwrite", (s) => s.delete(key)); } catch { /* nothing kept */ }
    location.reload();
  });
  ctx.bar.insertBefore(run, ctx.bar.querySelector("#reset"));
  ctx.bar.insertBefore(back, ctx.bar.querySelector("#reset"));
  ctx.bar.insertBefore(what, ctx.bar.querySelector("#reset"));
  ctx.bar.appendChild(pick);
  // what runs: said once the load message went (beforeLoad above ran by then)
  const show = () => {
    if (!running) return;
    what.textContent = `Running ${running.name} (${Math.round(running.size / 1024)} KiB, from this computer).`;
    back.hidden = false;
  };
  ctx.node.port.addEventListener("message", (e) => { if (e.data && e.data.type === "ready") show(); });
}
