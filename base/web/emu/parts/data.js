// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "data" (fm1.toml emu = ["data"]): the data area (plat_data and its windows, what an app keeps beyond
// its saved objects: projects, samples, logs) kept in this browser's IndexedDB across reloads, as the FM-1's flash
// keeps it across a power cycle. The page's saved objects stay where the page keeps them (localStorage). The page's
// Factory reset forgets both. The worklet's side is data_worklet.js.
// Other parts read and replace the kept state: saved(id), keep(id, buf), forget(id), current() below.

const DB = "fm1-emu", STORE = "state";

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
const key = (id) => `${id}:data`;
/** the data area kept for app id (an ArrayBuffer), or undefined */
export const saved = (id) => op("readonly", (s) => s.get(key(id)));
/** keep buf as app id's data area (what the next start finds) */
export const keep = (id, buf) => op("readwrite", (s) => s.put(buf, key(id)));
/** forget app id's data area */
export const forget = (id) => op("readwrite", (s) => s.delete(key(id)));

let post = null, stopped = false, dumpWait = null;
/** the running emulator's data area now ({ data, base }), or null when it is not running */
export function current() {
  if (!post) return Promise.resolve(null);
  return new Promise((resolve) => { dumpWait = resolve; post({ dump: 1 }); });
}

let appId = "", status = () => {};
export function message(m) {
  if (m.dump && dumpWait) { const w = dumpWait; dumpWait = null; w({ data: m.data, base: m.base }); }
  if (stopped || !m.data) return;
  keep(appId, m.data).catch((e) => status("The emulator's data could not be kept in this browser: " + (e && e.message || e)));
}

/** stop keeping what the emulator writes (the state is being replaced, the page reloads) */
export function stop() { stopped = true; }

export function attach(ctx) {
  appId = ctx.id;
  status = ctx.status || status;
  post = ctx.post;
  ctx.beforeLoad(async (msg) => {
    let kept;
    try { kept = await saved(appId); } catch (e) { status("The emulator's kept data could not be read: " + (e && e.message || e)); }
    return { ...msg, parts: { ...msg.parts, data: kept } };
  });
  addEventListener("visibilitychange", () => { if (document.visibilityState === "hidden" && post) post({ flush: 1 }); });
  // the page's Factory reset (its second press) forgets the data area too, before the page reloads
  const reset = document.getElementById("reset");
  if (reset) reset.addEventListener("click", (e) => {
    if (!reset.dataset.armed) return;
    e.stopImmediatePropagation();
    stop();
    try { localStorage.removeItem(`${appId}-emu-store`); } catch { /* private window */ }
    forget(appId).catch(() => {}).finally(() => location.reload());
  }, { capture: true });
}
