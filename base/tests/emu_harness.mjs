// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// The browser emulator's worklet (web/emu/worklet.js, with app_worklet.js and the parts a build has) under node, as an
// AudioWorklet runs it: registerProcessor, AudioWorkletProcessor with a port, currentTime and sampleRate, a render
// quantum of 128 frames. For the emu tests (tests/emu_*_test.mjs): no browser, no audio device.
//   const emu = await startEmu("template/build/emu", { sampleRate: 44100, parts: {...} });
//   emu.run(ms); emu.send(msg); emu.msgs (what the worklet posted); emu.out (the last quantum's samples)
import { execFileSync } from "node:child_process";
import { readFileSync, existsSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

export async function startEmu(dir, { sampleRate = 44100, parts, store = {}, wasm } = {}) {
  const procs = {};
  globalThis.sampleRate = sampleRate;
  globalThis.currentTime = 0;
  globalThis.registerProcessor = (name, Proc) => { procs[name] = Proc; };
  const msgs = [];
  globalThis.AudioWorkletProcessor = class {
    constructor() {
      this.port = { onmessage: null, postMessage: (m) => msgs.push(m) };
    }
  };
  const abs = resolve(dir);
  if (existsSync(`${abs}/app_worklet.js`)) await import(pathToFileURL(`${abs}/app_worklet.js`).href);
  await import(pathToFileURL(`${abs}/worklet.js`).href);
  const proc = new procs.fm1();
  const bin = wasm || readFileSync(`${abs}/app.wasm`);
  const buf = bin.buffer.slice(bin.byteOffset, bin.byteOffset + bin.byteLength);
  await proc.port.onmessage({ data: { type: "load", wasm: buf, store, master: 4096, ...(parts ? { parts } : {}) } });
  const out = [new Float32Array(128), new Float32Array(128)];
  const emu = {
    proc, msgs, out, sampleRate,
    get ex() { return proc.ex; },
    send: (m) => proc.port.onmessage({ data: m }),
    quantum() {
      proc.process([], [out]);
      globalThis.currentTime += 128 / sampleRate;
    },
    // ms of the page's time; peak: the loudest sample meanwhile
    run(ms) {
      let peak = 0;
      const n = Math.ceil(ms * sampleRate / 1000 / 128);
      for (let i = 0; i < n; i++) {
        emu.quantum();
        for (const ch of out) for (const v of ch) peak = Math.max(peak, Math.abs(v));
      }
      return peak;
    },
    // the samples of ms of the page's time, left channel
    record(ms) {
      const n = Math.ceil(ms * sampleRate / 1000 / 128), got = new Float32Array(n * 128);
      for (let i = 0; i < n; i++) { emu.quantum(); got.set(out[0], i * 128); }
      return got;
    },
    // the worklet's messages of one kind since the last take (type "frame", or a part's name)
    take(kind) {
      const got = msgs.filter((m) => m.type === kind || m.part === kind);
      for (let i = msgs.length - 1; i >= 0; i--) if (msgs[i].type === kind || msgs[i].part === kind) msgs.splice(i, 1);
      return got;
    },
  };
  return emu;
}

// tools/build.py APP --web with these parts (FM1B_EMU), from fm1-base's root; the build's emu directory
export function buildEmu(app, parts = "", env = {}) {
  const root = resolve(new URL("..", import.meta.url).pathname);
  execFileSync(process.env.PYTHON || "python3", ["tools/build.py", app, "--web"],
    { cwd: root, env: { ...process.env, FM1B_EMU: parts, ...env }, stdio: ["ignore", "ignore", "inherit"] });
  return `${root}/${app}/build/emu`;
}

let fails = 0;
export function check(ok, what) {
  console.log(`  ${ok ? "ok" : "FAIL"} ${what}`);
  if (!ok) fails++;
}
export function done() {
  console.log(fails ? `${fails} failed` : "all passed");
  process.exit(fails ? 1 : 0);
}
