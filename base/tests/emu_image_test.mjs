// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "image" (web/emu/parts/image.js): the image file's format. The template built with it (and data);
// an image packed from objects and a data area unpacks to the same; a byte changed anywhere, a file cut short, one
// with a byte more, another file are refused with the reason. Saving and loading through the page (IndexedDB,
// localStorage, a download) runs in a browser only.
//   node tests/emu_image_test.mjs
import { buildEmu, check, done } from "./emu_harness.mjs";

const dir = buildEmu("template", "image");
const { pack, unpack } = await import(`${dir}/parts/image.js`);
const data = new Uint8Array(0xfc000 - 0x93000).fill(0xff);
data.set([1, 2, 3], 0x0d000);
const store = { 0: Uint8Array.of(9, 8, 7), 3: new Uint8Array(3840).fill(0x42), 5: new Uint8Array(0) };
const file = pack({ id: "template", store, base: 0x93000, data });
const img = unpack(file);
check(img.id === "template" && img.base === 0x93000, "the app's id and the area's address come back");
check(img.data.length === data.length && img.data.every((v, i) => v === data[i]), "the data area comes back byte for byte");
check(Object.keys(img.store).join() === "0,3,5" && img.store[3].length === 3840 && img.store[0][2] === 7 && img.store[5].length === 0,
  "the saved objects come back, an empty one too");
const refused = (bytes) => { try { unpack(bytes); return ""; } catch (e) { return e.message; } };
let all = true;
for (const at of [0, 9, 20, 0x2000, file.length - 1]) {
  const bad = file.slice();
  bad[at] ^= 0x10;
  all &&= refused(bad) !== "";
}
check(all, "a byte changed anywhere (the name, an object, the area, the CRC): refused");
check(/short|damaged/.test(refused(file.subarray(0, file.length - 100))), "cut short: refused");
const more = new Uint8Array(file.length + 1);
more.set(file);
check(refused(more) !== "", "a byte more: refused");
check(/not an emulator image/.test(refused(new TextEncoder().encode("hello, not an image at all"))), "another file: not an image");
done();
