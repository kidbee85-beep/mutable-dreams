// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// The web installer's stock file (web/fm1pkg.js): M-VAVE's firmware chosen by the user is checked by its checksum when
// the version is one whose SHA-256 is pinned (STOCK_FILES): a file that says it is that version but is not that very file
// is refused ("modified"), the pinned file passes, and a version without a pinned checksum is neither pinned nor refused
// (checkUfw and the FM-1 check it). Synthetic files: M-VAVE's own is not part of this repository.
//   node tests/installer_stock_test.mjs
import { productOf, sha256hex, stockChecksum, STOCK_FILES } from "../web/fm1pkg.js";

let fails = 0;
const check = (ok, msg) => { console.log(`${msg.padEnd(72)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };
const code = (f) => { try { f(); } catch (e) { return e.code; } return null; };

// a .fwsc of a given size that says it is `product`: one marker byte after each of the first 20 blocks (productOf)
function fwsc(product, size, fill = 0x11) {
  const b = new Uint8Array(size).fill(fill);
  for (let i = 0; i < 20; i++) b[i * 0x30 + 0x2F] = i < product.length ? (product.charCodeAt(i) + i + 1) & 0xFF : 0x7D;
  return b;
}

check(await sha256hex(new TextEncoder().encode("abc")) === "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256hex: the digest of \"abc\"");
check(productOf(fwsc("FM-1_015", 2000)) === "FM-1_015", "the synthetic file says FM-1_015");

// V15 is pinned: the right size with another content, the right content cut short, are refused
{
  const pin = STOCK_FILES["FM-1_015"];
  check(!!pin && /^[0-9a-f]{64}$/.test(pin.sha256) && pin.size === 699956, "FM-1_015 is pinned (size and SHA-256)");
  const other = fwsc("FM-1_015", pin.size);
  check(code(() => stockChecksum("FM-1_015", other.length, "0".repeat(64))) === "modified", "FM-1_015 with another checksum: refused");
  check(code(() => stockChecksum("FM-1_015", pin.size - 1, pin.sha256)) === "modified", "FM-1_015 with the right checksum, another size: refused");
  check(stockChecksum("FM-1_015", pin.size, pin.sha256) === true, "FM-1_015 with the pinned size and checksum: passes");
}
// V16 is pinned too (M-VAVE's FM-1_016, as felucca-bsp tools/fm1_rescue.py's STOCK)
{
  const pin = STOCK_FILES["FM-1_016"];
  check(!!pin && pin.size === 718196 && pin.sha256.startsWith("d017fb65"), "FM-1_016 is pinned (size and SHA-256)");
  check(code(() => stockChecksum("FM-1_016", pin.size, "0".repeat(64))) === "modified", "FM-1_016 with another checksum: refused");
  check(stockChecksum("FM-1_016", pin.size, pin.sha256) === true, "FM-1_016 with the pinned size and checksum: passes");
}
// a version without a pinned checksum: not refused, not vouched for
check(stockChecksum("FM-1_017", 700000, "0".repeat(64)) === false, "a version with no pinned checksum: false, not refused");
check(stockChecksum("FM-1_099", 1000, "0".repeat(64)) === false, "another unpinned product: false");
// the whole path with a synthetic file whose digest is pinned for the test
{
  const f = fwsc("FM-1_017", 5000);
  const hex = await sha256hex(f);
  STOCK_FILES["FM-1_017"] = { size: f.length, sha256: hex };
  check(productOf(f) === "FM-1_017" && stockChecksum(productOf(f), f.length, await sha256hex(f)) === true, "a file with its pinned digest passes");
  const g = f.slice(); g[1000] ^= 1;
  const hexG = await sha256hex(g);
  check(hexG !== hex && code(() => stockChecksum(productOf(g), g.length, hexG)) === "modified", "one changed byte: refused");
  delete STOCK_FILES["FM-1_017"];
}
console.log(fails ? "INSTALLER STOCK TEST FAILED" : "installer stock test passed");
process.exit(fails ? 1 : 0);
