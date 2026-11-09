// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// SysEx with the app in the browser emulator (web/emu/emu_web.c web_sx_in, web_sx_out_*: APP_SYSEX): the SysEx test
// app (tests/sysex_app, tools/build.py tests/sysex_app --web) under node, as tests/sysex_app/tests/sysex.txt runs it in
// the host simulator: an echo comes back whole, a long one too; a frame of 641 bytes between F0 and F7, a status
// byte inside, or no frame at all never reach the app; read frames are cleared.
//   node tests/sysex_web_test.mjs tests/sysex_app/build/emu
import { readFileSync } from "node:fs";

const dir = process.argv[2] || "tests/sysex_app/build/emu";
let fails = 0;
const check = (ok, what) => { console.log(`  ${ok ? "ok" : "FAIL"} ${what}`); fails += !ok; };

let mem;
const wasi = new Proxy({
  clock_time_get: (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; },
}, { get: (t, k) => (k in t ? t[k] : () => 0) });
const { instance } = await WebAssembly.instantiate(readFileSync(`${dir}/app.wasm`),
  { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports;
mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_boot();
const run = (ms) => { for (let i = 0; i < Math.ceil(ms * 44.1 / 128); i++) ex.web_render(128); };
const hex = (s) => Uint8Array.from(s.split(/\s+/).filter(Boolean), (h) => parseInt(h, 16));
function send(bytes) {
  new Uint8Array(mem.buffer, ex.web_sx_in_buf(), bytes.length).set(bytes);
  return ex.web_sx_in(bytes.length);
}
function out() {
  const got = new Uint8Array(mem.buffer, ex.web_sx_out_buf(), ex.web_sx_out_len()).slice();
  ex.web_sx_out_clear();
  return got;
}
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

run(300);
check(ex.web_sx_in_max() >= 4104, `frames up to ${ex.web_sx_in_max()} bytes taken (a DX7 bank's 4104 for APP_SYSEX_BYTE)`);
const echo = hex("F0 7D 53 58 01 11 22 33 F7");
check(send(echo) === 1, "an echo request reaches the app");
run(20);
check(same(out(), echo), "its answer comes back whole");
check(out().length === 0, "read: cleared");
const long = Uint8Array.from([0xf0, 0x7d, 0x53, 0x58, 0x01, ...Array.from({ length: 300 }, (_, i) => i & 0x7f), 0xf7]);
send(long);
run(20);
check(same(out(), long), "a long echo (306 bytes) comes back whole");
const big = Uint8Array.from([0xf0, 0x7d, 0x53, 0x58, 0x01, ...new Array(637).fill(0x55), 0xf7]);
check(send(big) === 0, "641 bytes between F0 and F7: dropped, as the device drops it");
run(20);
check(out().length === 0, "and nothing answers");
check(send(hex("F0 7D 53 58 01 90 F7")) === 0, "a status byte inside: dropped");
check(send(hex("90 3C 64")) === -1 && send(hex("F0 7D 53")) === -1, "not a frame: refused");
run(20);
check(out().length === 0, "nothing answers them");
const fit = Uint8Array.from([0xf0, 0x7d, 0x53, 0x58, 0x02, ...new Array(636).fill(0x55), 0xf7]);
check(send(fit) === 1, "640 bytes between F0 and F7: the app's");
console.log(fails ? `${fails} failed` : "all passed");
process.exit(fails ? 1 : 0);
