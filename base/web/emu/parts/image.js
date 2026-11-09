// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// emu-needs: data
// Emulator part "image" (fm1.toml emu = ["image"], with data): the emulated FM-1's whole saved state to a file and
// back, from the page's bar: "Save image" writes the saved objects (plat_store, as the page keeps them) and the data
// area (plat_data, as it is now) to NAME.fm1emu; "Load image…" reads one, checks it whole, and starts the emulator
// again on it (the page reloads). An image from another app is loaded only if asked twice.
// The file (little-endian): "FM1EMU1" and a zero; the app's id (u32 length, UTF-8); the objects (u32 count, then each
// u32 object, u32 length, the bytes); the data area (u32 base address, u32 length, the bytes); a CRC-32 of all before.
import { saved, keep, current, stop } from "./data.js";

const MAGIC = "FM1EMU1\0";

function crc32(u8) {
  let c = ~0;
  for (const b of u8) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  }
  return ~c >>> 0;
}

/** { id, store: { obj: Uint8Array }, base, data: Uint8Array } -> the file's bytes */
export function pack({ id, store, base, data }) {
  const idb = new TextEncoder().encode(id), objs = Object.entries(store || {});
  const size = 8 + 4 + idb.length + 4 + objs.reduce((n, [, b]) => n + 8 + b.length, 0) + 8 + data.length + 4;
  const out = new Uint8Array(size), dv = new DataView(out.buffer);
  let at = 0;
  out.set(new TextEncoder().encode(MAGIC), at); at += 8;
  dv.setUint32(at, idb.length, true); out.set(idb, at + 4); at += 4 + idb.length;
  dv.setUint32(at, objs.length, true); at += 4;
  for (const [o, b] of objs) {
    dv.setUint32(at, +o, true); dv.setUint32(at + 4, b.length, true); out.set(b, at + 8); at += 8 + b.length;
  }
  dv.setUint32(at, base >>> 0, true); dv.setUint32(at + 4, data.length, true); out.set(data, at + 8); at += 8 + data.length;
  dv.setUint32(at, crc32(out.subarray(0, at)), true);
  return out;
}

/** the file's bytes -> what pack took; throws with the reason for a file that is not one, or is damaged */
export function unpack(u8) {
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
  const need = (at, n) => { if (at + n > u8.length) throw new Error("not an emulator image (too short)"); };
  need(0, 12);
  if (new TextDecoder().decode(u8.subarray(0, 8)) !== MAGIC) throw new Error("not an emulator image");
  if (dv.getUint32(u8.length - 4, true) !== crc32(u8.subarray(0, u8.length - 4))) throw new Error("the image is damaged (CRC)");
  let at = 8;
  const n = dv.getUint32(at, true); need(at + 4, n);
  const id = new TextDecoder().decode(u8.subarray(at + 4, at + 4 + n)); at += 4 + n;
  need(at, 4);
  const count = dv.getUint32(at, true), store = {}; at += 4;
  for (let i = 0; i < count; i++) {
    need(at, 8);
    const o = dv.getUint32(at, true), len = dv.getUint32(at + 4, true);
    need(at + 8, len);
    store[o] = u8.slice(at + 8, at + 8 + len); at += 8 + len;
  }
  need(at, 8);
  const base = dv.getUint32(at, true), len = dv.getUint32(at + 4, true);
  need(at + 8, len + 4);
  const data = u8.slice(at + 8, at + 8 + len); at += 8 + len;
  if (at + 4 !== u8.length) throw new Error("the image is damaged (its length)");
  return { id, store, base, data };
}

// the page's saved objects (index.html: localStorage NAME-emu-store, { obj: base64 })
const storeKey = (id) => `${id}-emu-store`;
function readStore(id) {
  try {
    const raw = JSON.parse(localStorage.getItem(storeKey(id)) || "{}"), out = {};
    for (const [o, b64] of Object.entries(raw)) out[o] = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    return out;
  } catch { return {}; }
}
function writeStore(id, store) {
  const raw = {};
  for (const [o, u8] of Object.entries(store)) { let s = ""; for (const c of u8) s += String.fromCharCode(c); raw[o] = btoa(s); }
  localStorage.setItem(storeKey(id), JSON.stringify(raw));
}

async function saveImage(id, status) {
  const now = await current();
  const data = now ? new Uint8Array(now.data) : new Uint8Array((await saved(id)) || new ArrayBuffer(0));
  const file = pack({ id, store: readStore(id), base: now ? now.base : 0x93000, data });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([file], { type: "application/octet-stream" }));
  a.download = `${id}.fm1emu`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  status(`Saved ${id}.fm1emu (${Math.round(file.length / 1024)} KiB).`);
}

let other = "";
async function loadImage(id, file, status) {
  let img;
  try { img = unpack(new Uint8Array(await file.arrayBuffer())); } catch (e) { status(`${file.name}: ${e.message}.`); return; }
  if (img.id !== id && other !== file.name) {
    other = file.name;
    status(`${file.name} is ${img.id}'s, not ${id}'s: load it again to use it anyway.`);
    return;
  }
  stop();
  await keep(id, img.data.buffer);
  writeStore(id, img.store);
  location.reload();
}

export function attach(ctx) {
  if (!ctx.bar) return;
  const status = ctx.status || (() => {});
  const save = document.createElement("button"), load = document.createElement("button"), pick = document.createElement("input");
  save.type = load.type = "button";
  save.textContent = "Save image";
  save.title = "The FM-1's saved state (objects and data area) to a file";
  load.textContent = "Load image…";
  load.title = "Start again on a saved image";
  pick.type = "file";
  pick.accept = ".fm1emu";
  pick.hidden = true;
  save.addEventListener("click", () => saveImage(ctx.id, status).catch((e) => status("The image could not be saved: " + e.message)));
  load.addEventListener("click", () => pick.click());
  pick.addEventListener("change", () => {
    const f = pick.files[0];
    pick.value = "";
    if (f) loadImage(ctx.id, f, status).catch((e) => status("The image could not be loaded: " + e.message));
  });
  ctx.bar.insertBefore(save, ctx.bar.querySelector("#reset"));
  ctx.bar.insertBefore(load, ctx.bar.querySelector("#reset"));
  ctx.bar.appendChild(pick);
}
