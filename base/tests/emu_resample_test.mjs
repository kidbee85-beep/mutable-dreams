// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "resample" (web/emu/parts/resample_worklet.js): the template built with it, its SINE preset's note
// (key 7: middle C, 261.63 Hz) played in a context at 44.1, 48, 96 and 22.05 kHz: the pitch the page hears is the
// note's within 0.2 % at each, the device's clock keeps the page's time, and the sound is clean (no steps: the
// largest change between samples as at 44.1 kHz, scaled by the rate). Without the part, at 48 kHz, the pitch is
// 48/44.1 high (what the part is for).
//   node tests/emu_resample_test.mjs
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { startEmu, buildEmu, check, done } from "./emu_harness.mjs";

const C4 = 261.6256;
function pitch(x, rate) {                          // from the rising zero crossings, interpolated
  const t = [];
  for (let i = 1; i < x.length; i++) if (x[i - 1] < 0 && x[i] >= 0) t.push(i - 1 + x[i - 1] / (x[i - 1] - x[i]));
  return t.length > 2 ? (t.length - 1) * rate / (t.at(-1) - t[0]) : 0;
}
async function one(dir, rate) {
  const emu = await startEmu(dir, { sampleRate: rate });
  emu.run(700);
  emu.send({ type: "keys", mask: 1 << 7 });
  emu.run(200);
  const t0 = emu.ex.web_now();
  const x = emu.record(1000);
  return { f: pitch(x, rate), ms: emu.ex.web_now() - t0, jump: x.slice(1).reduce((m, v, i) => Math.max(m, Math.abs(v - x[i])), 0) };
}

// one emulator a process (a worklet's modules load once): node THIS --one DIR RATE prints the measures
if (process.argv[2] === "--one") {
  console.log(JSON.stringify(await one(process.argv[3], +process.argv[4])));
  process.exit(0);
}
const note = (dir, rate) => JSON.parse(execFileSync(process.execPath, [fileURLToPath(import.meta.url), "--one", dir, String(rate)]).toString());

const plain = buildEmu("template", "");
const off = note(plain, 48000);
check(Math.abs(off.f / C4 - 48 / 44.1) < 0.003, `without the part at 48 kHz the note is high: ${off.f.toFixed(2)} Hz`);
const dir = buildEmu("template", "resample");
const ref = note(dir, 44100);
check(Math.abs(ref.f / C4 - 1) < 0.002, `44.1 kHz: ${ref.f.toFixed(2)} Hz (the part does nothing)`);
for (const rate of [48000, 96000, 22050]) {
  const r = note(dir, rate);
  check(Math.abs(r.f / C4 - 1) < 0.002, `${rate} Hz: ${r.f.toFixed(2)} Hz`);
  check(Math.abs(r.ms - 1000) <= 12, `${rate} Hz: the device ran ${r.ms} ms in the page's 1000`);
  check(r.jump <= ref.jump * 44100 / rate * 1.1 + 1e-4, `${rate} Hz: no steps (largest change ${r.jump.toFixed(4)}, ${ref.jump.toFixed(4)} at 44.1)`);
}
done();
