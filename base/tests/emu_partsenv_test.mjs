// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The emulator's parts keep the app's own worklet part whole (web/emu/parts/parts_worklet.js): its env, the imports
// app.wasm takes from it (worklet.js), is still there after the parts' fm1App takes the place of the app's.
//   node tests/emu_partsenv_test.mjs
import { fileURLToPath, pathToFileURL } from "node:url";
import path from "node:path";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
let failed = 0;
const ok = (c, what) => { console.log((c ? "  ok   " : "  FAIL ") + what); failed += !c; };

const said = [];
globalThis.registerProcessor = () => {};
globalThis.fm1App = { env: { rp_js_cur: () => 7 }, load: () => said.push("own load") };
await import(pathToFileURL(path.join(root, "web/emu/parts/parts_worklet.js")).href);
ok(globalThis.fm1App.env && globalThis.fm1App.env.rp_js_cur && globalThis.fm1App.env.rp_js_cur() === 7,
   "the app's env imports kept by the parts' fm1App");
globalThis.fm1App.load({ port: { postMessage() {} } });
ok(said.join() === "own load", "the app's own load still called");
console.log("emu_partsenv: " + (failed ? "FAILED" : "all passed"));
process.exit(failed ? 1 : 0);
