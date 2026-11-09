// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "wasm" (web/emu/parts/wasm.js): the template built with it; its check of a picked file takes the
// template's own app.wasm and refuses another module (an empty one) and a file that is not WebAssembly, each with
// the reason. The page's side (the file, IndexedDB, the reload) runs in a browser only.
//   node tests/emu_wasm_test.mjs
import { readFileSync } from "node:fs";
import { buildEmu, check, done } from "./emu_harness.mjs";

const dir = buildEmu("template", "wasm");
const { why } = await import(`${dir}/parts/wasm.js`);
check(await why(readFileSync(`${dir}/app.wasm`)) === "", "the template's app.wasm is an emulator's");
const empty = Uint8Array.of(0, 0x61, 0x73, 0x6d, 1, 0, 0, 0);
check(/no web_boot, web_render/.test(await why(empty)), `another module: ${await why(empty)}`);
check(await why(new TextEncoder().encode("not wasm")) === "not a WebAssembly module", "a file that is not WebAssembly: refused");
done();
