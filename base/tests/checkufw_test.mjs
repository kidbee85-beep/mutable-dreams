// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// web/fm1pkg.js checkUfw: fm1-base's own package (every part checked), M-VAVE's stock file (optional; its
// file is not in this repository, pass its path), and a damaged byte in each part.
//   node tests/checkufw_test.mjs web/m-bootloader/fm1-uboot-1.2.fwsc [path/to/FM-1_015.fwsc]
import fs from "fs";
import { logicalImage, checkUfw, ufwEntries } from "../web/fm1pkg.js";

let fails = 0;
const check = (ok, msg) => { console.log(`${msg.padEnd(70)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };
const passes = (img) => { try { checkUfw(img); return true; } catch (_) { return false; } };
const load = (p) => logicalImage(new Uint8Array(fs.readFileSync(p)));
const flip = (img, at) => { const b = img.slice(); b[at] ^= 0x10; return b; };

// fm1-base's own package: it passes, and a damaged first, middle and last byte of every part fails
const own = load(process.argv[2]);
check(passes(own), "own package: passes");
const ownParts = ufwEntries(own);
check(Object.keys(ownParts).length >= 1, "own package: has parts (an installer package: ota.bin only)");
for (const [type, { off, len }] of Object.entries(ownParts)) {
  const all = [off, off + (len >> 1), off + len - 1].every((at) => !passes(flip(own, at)));
  check(len > 0 && all, `own package: part ${type}, a damaged byte at its start, middle, end fails`);
}
check(!passes(flip(own, 0)) && !passes(flip(own, 0x44)), "own package: a damaged header or part list fails");
const ownEnd = Math.max(...Object.values(ownParts).map((p) => p.off + p.len));
check(!passes(own.subarray(0, ownEnd - 1)), "own package: a file cut inside its last part fails");

// M-VAVE's stock file: it passes; the parts with a known checksum scheme still fail when damaged
if (process.argv[3]) {
  const stock = load(process.argv[3]);
  check(passes(stock), "stock file: passes");
  const parts = ufwEntries(stock);
  for (const type of [0, 100, 255]) {
    const { off, len } = parts[type];
    check(!passes(flip(stock, off)) && !passes(flip(stock, off + len - 1)), `stock file: part ${type}, a damaged byte fails`);
  }
  check(!passes(flip(stock, 0)) && !passes(flip(stock, 0x44)), "stock file: a damaged header or part list fails");
  check(!passes(stock.subarray(0, parts[255].off + parts[255].len - 1)), "stock file: a truncated file fails");
}
console.log(fails ? "CHECKUFW TEST FAILED" : "checkufw test passed");
process.exit(fails ? 1 : 0);
