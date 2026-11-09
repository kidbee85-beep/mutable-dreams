// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The web installer's step 2 (web/fm1ota.js Updater.resume -> write) against felucca-bsp's real update loader
// (loader/ldr_core.c, chip/ota.c) on a pipe (bsp/tests/ldr_pipe.c, built here: CC, default cc), on a fake WebMIDI:
// the fast install (the table, only the changed sectors, the bar never back), the classic install (Updater.classic),
// a loader from before the fast install, a power cut and the install resumed, a damaged package refused before anything
// is erased, and the vectors the other hosts share.
//   node tests/installer_fast_test.mjs
import { spawn, execFileSync } from "child_process";
import fs from "fs";
import os from "os";
import path from "path";
import { Updater, FAST_HELLO, sectorTable, parseFrame, unpack7 } from "../web/fm1ota.js";

let fails = 0;
const check = (ok, msg) => { console.log(`${msg.padEnd(78)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };
const hex = (a) => Array.from(a, (x) => x.toString(16).padStart(2, "0")).join("");
const APP_LO = 0x4000, APP_HI = 0x93000, FL_OFF = 0x400, CHUNK = 4096;
const TMP = fs.mkdtempSync(path.join(os.tmpdir(), "fm1-fast-"));

function build(name, fast) {
  const exe = path.join(TMP, name);
  execFileSync(process.env.CC || "cc", ["-std=c11", "-D_DEFAULT_SOURCE", "-O1", "-w", "-Ibsp/chip",
    ...(fast ? ["-DLDR_FAST_CHUNK=4096u", "-DOTA_FRAME_MAX=4200u"] : []), "bsp/tests/ldr_pipe.c", "-o", exe]);
  return exe;
}
function device(exe, name) {
  const flash = path.join(TMP, name + ".nor"), img = path.join(TMP, name + ".img");
  execFileSync(exe, ["init", flash, img]);
  return { flash, image: new Uint8Array(fs.readFileSync(img)) };
}
const app = (flash) => fs.readFileSync(flash).subarray(APP_LO, APP_HI);
const done = (flash, image) => {
  const nor = fs.readFileSync(flash);
  return Buffer.compare(nor.subarray(APP_LO, APP_HI), Buffer.from(image.subarray(FL_OFF + APP_LO, FL_OFF + APP_HI))) === 0 &&
    nor.subarray(0xE4000, 0xE5000).every((b) => b === 0xFF);
};
const changed = (cur, image) => {
  let k = 0;
  for (let a = 0; a < APP_HI - APP_LO; a += CHUNK)
    if (Buffer.compare(cur.subarray(a, a + CHUNK), Buffer.from(image.subarray(FL_OFF + APP_LO + a, FL_OFF + APP_LO + a + CHUNK)))) k++;
  return k;
};

// one FM-1 in its update loader on a WebMIDI port "Felucca Update": the pipe process
function loader(exe, flash, cutAfter) {
  // (a read's try 3 s: longer than the classic install's 2 s pause after the upgrade command, as the FM-1's 5 s)
  const proc = spawn(exe, ["run", flash], { stdio: ["pipe", "pipe", "pipe"], env: { ...process.env, LDR_PIPE_TRY_MS: "3000" } });
  const L = { proc, reqs: [], checks: [], hellos: 0, exited: null, err: "" };
  const input = { name: "Felucca Update", state: "connected", open: async () => {}, onmidimessage: null };
  const output = { name: "Felucca Update", state: "connected", open: async () => {},
    send: (b) => { if (output.state !== "connected") throw new Error("gone"); proc.stdin.write(Buffer.from(b)); } };
  proc.on("exit", (c) => { L.exited = c; input.state = output.state = "disconnected"; });
  proc.stdin.on("error", () => {});
  proc.stderr.on("data", (d) => { L.err += d; });
  let buf = [];
  proc.stdout.on("data", (d) => {
    for (const x of d) {
      if (x === 0xF0) buf = [];
      buf.push(x);
      if (x === 0xF7 && buf[0] === 0xF0) { got(buf); buf = []; }
    }
  });
  function got(pkt) {
    const u = unpack7(pkt.slice(1, -1)), f = parseFrame(pkt);
    if (u.length === 15 && u[2] === 0x30) {
      const addr = (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len = u[11] | (u[12] << 8);
      L.reqs.push([addr, len]);
      if (cutAfter != null && L.reqs.filter(([a, n]) => n === CHUNK && a < 0xD0000000).length > cutAfter) { proc.kill("SIGKILL"); return; }
    } else if (f && f.cmd === 0x32) L.checks.push([f.body[0] | (f.body[1] << 8), f.body[2] | (f.body[3] << 8)]);
    else if (f && f.cmd === 0x31) L.hellos++;
    const e = { data: Uint8Array.from(pkt), target: input };
    input.onmidimessage && input.onmidimessage(e);
  }
  L.access = { inputs: new Map([["i0", input]]), outputs: new Map([["o0", output]]) };
  L.sectors = () => L.reqs.filter(([a, n]) => n === CHUNK && a < 0xD0000000).map(([a]) => a);
  L.wait = () => new Promise((r) => { if (L.exited !== null) r(L.exited); else { const t = setTimeout(() => { proc.kill(); r(null); }, 5000); proc.on("exit", (c) => { clearTimeout(t); r(c); }); } });
  return L;
}

async function write(L, image, classic = false) {
  const up = new Updater(L.access);
  up.classic = classic;
  up.pause = 0;
  const steps = [];
  let ok = false, code = null;
  try { ok = await up.resume(image, (k, a, b, pct) => steps.push([k, a, b, pct])); } catch (e) { code = e.code || String(e); }
  return { ok, code, steps, bar: steps.filter((s) => s[0] === "write" && s[3] != null).map((s) => s[3]) };
}

const fastExe = build("ldr_fast", true), oldExe = build("ldr_old", false);
const nApp = (APP_HI - APP_LO) / CHUNK;

{
  const { flash, image } = device(fastExe, "fast");
  const want = changed(app(flash), image);
  const L = loader(fastExe, flash);
  const r = await write(L, image);
  const ex = await L.wait();
  check(r.ok && ex === 0 && done(flash, image) && L.hellos === 1 && L.sectors().length === want &&
    L.reqs.some(([a]) => a >= 0xD0000000 && a < 0xE0000000) && r.steps.some((s) => s[0] === "mode" && s[1] === "fast"),
    `fast install: the table, then only the ${want} changed sectors of ${nApp}, 4 KiB each`);
  check(L.checks.length > 0 && L.checks.at(-1)[0] === nApp && r.bar.length > 0 &&
    r.bar.every((v, i) => i === 0 || v > r.bar[i - 1]) && r.bar.at(-1) <= 99,
    `fast install: the loader's 0x32 frames (${L.checks.length}), the bar from them, never back`);
  execFileSync(fastExe, ["rearm", flash]);
  const L2 = loader(fastExe, flash);
  const r2 = await write(L2, image);
  check(r2.ok && (await L2.wait()) === 0 && done(flash, image) && L2.sectors().length === 0,
    "the same package again: the table only, no sector sent");
}
{
  const { flash, image } = device(fastExe, "classic");
  const L = loader(fastExe, flash);
  const r = await write(L, image, true);
  check(r.ok && (await L.wait()) === 0 && done(flash, image) && L.hellos === 0 && Math.max(...L.reqs.filter(([a]) => a < 0xE0000000).map(([, n]) => n)) <= 512 &&
    r.steps.some((s) => s[0] === "write" && s[3] == null && s[2] > 0),
    `Updater.classic, new loader: the classic install (${L.reqs.length} reads of 512 B at most)`);
}
{
  const { flash, image } = device(oldExe, "old");
  const L = loader(oldExe, flash);
  const r = await write(L, image);
  check(r.ok && (await L.wait()) === 0 && done(flash, image) && L.hellos === 0 &&
    r.steps.some((s) => s[0] === "mode" && s[1] === "classic"),
    "a loader from before the fast install: no answer, the classic install");
}
{
  const { flash, image } = device(fastExe, "noloader");
  const plain = image.slice();
  plain.fill(0, 0x200, 0x210);                         // no Felucca loader's mark: as M-VAVE's firmware
  const L = loader(fastExe, flash);
  const r = await write(L, plain);
  check(r.ok && (await L.wait()) === 0 && L.hellos === 0 && !L.reqs.some(([a]) => a >= 0xD0000000 && a < 0xE0000000),
    "a package without a Felucca loader: no hello, the classic install");
}
{
  const { flash, image } = device(fastExe, "cut");
  const all = changed(app(flash), image);
  const L = loader(fastExe, flash, 30);
  const r = await write(L, image);
  await L.wait();
  const rest = changed(app(flash), image);
  check(!r.ok && !done(flash, image) && rest < all, `power cut after 30 sectors: stopped, ${rest} of ${all} left`);
  const L2 = loader(fastExe, flash);                   // the next power-on: the SPL starts the loader again
  const r2 = await write(L2, image);
  check(r2.ok && (await L2.wait()) === 0 && done(flash, image) && L2.sectors().length === rest,
    `... resumed: only the ${rest} sectors still different are sent`);
}
{
  const { flash, image } = device(fastExe, "damaged");
  const before = fs.readFileSync(flash);
  const bad = image.slice();
  bad[FL_OFF + 0x7005] ^= 0x10;                        // flash.bin damaged: its table no longer gives the UFW's CRC16
  const L = loader(fastExe, flash);
  const up = new Updater(L.access);
  up.pause = 0;
  const t0 = Date.now();
  const p = up.resume(bad, () => {});
  await new Promise((r) => setTimeout(r, 3000));
  L.proc.kill();
  await p.catch(() => {});
  check(/session -12/.test(L.err) && Buffer.compare(fs.readFileSync(flash), before) === 0 && Date.now() - t0 < 10000,
    "a damaged package: the table refused (-12), nothing erased or written");
}
check(hex(FAST_HELLO) === "f0003245190000400000203807f7", "the hello: the vector shared with the other hosts");
{
  const d = new Uint8Array(8192);
  for (let i = 0; i < 8192; i++) d[i] = ((i * 31) ^ (i >> 8)) & 0xFF;
  check(hex(sectorTable(d, 0, 2)) === "6204a25f925f5cc59b283583", "the table: the vector shared with the other hosts");
}
fs.rmSync(TMP, { recursive: true, force: true });
console.log(fails ? `FAST INSTALLER TESTS FAILED (${fails})` : "fast installer tests passed");
process.exit(fails ? 1 : 0);
