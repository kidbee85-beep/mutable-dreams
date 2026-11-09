// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "data" (web/emu/parts/data*.js): the template built with it under node. The data area the page
// kept goes in before the device boots; a change in it reaches the page within the 2 s,
// a flush at once, nothing while it stays; a dump comes always. The page's side (IndexedDB) runs in a browser only.
//   node tests/emu_data_test.mjs
import { startEmu, buildEmu, check, done } from "./emu_harness.mjs";

const dir = buildEmu("template", "data");
// what the page kept: a pattern in the first window's first sector
const LEN = 0xfc000 - 0x93000;
const kept = new Uint8Array(LEN).fill(0xff);
for (let i = 0; i < 4096; i++) kept[i] = i & 0xff;
const emu = await startEmu(dir, { parts: { data: kept.buffer } });
const len = emu.ex.web_data_len(), base = emu.ex.web_data_base();
check(len === LEN && base === 0x93000, `the data area: ${len} bytes from ${base.toString(16)}`);
const area = () => new Uint8Array(emu.ex.memory.buffer, emu.ex.web_data(), len);
check(area()[1] === 1 && area()[4095] === 0xff && area()[4096] === 0xff, "the kept data is there when the device boots");
emu.take("data");
emu.run(2500);
check(emu.take("data").length === 0, "nothing sent while it stays");
area()[100] = 0x12;
emu.run(2200);
const got = emu.take("data");
check(got.length === 1 && new Uint8Array(got[0].data)[100] === 0x12 && got[0].base === 0x93000, "a change reaches the page within 2 s, whole");
area()[200] = 0x34;
emu.send({ type: "app", part: "data", flush: 1 });
const fl = emu.take("data");
check(fl.length === 1 && new Uint8Array(fl[0].data)[200] === 0x34, "a flush sends it at once");
emu.send({ type: "app", part: "data", flush: 1 });
check(emu.take("data").length === 0, "a flush with nothing new sends nothing");
emu.send({ type: "app", part: "data", dump: 1 });
const d = emu.take("data");
check(d.length === 1 && d[0].dump === 1 && d[0].data.byteLength === len, "a dump comes always");
done();
