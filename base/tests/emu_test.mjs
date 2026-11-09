// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// The template's browser emulator (tools/build.py template --web) under node, through its own worklet.js: it boots
// and draws, a key sounds and sends its note out, a note from MIDI sounds, the lights and the saved objects reach
// the page, the CPU load is said.
//   node tests/emu_test.mjs template/build/emu
import { startEmu, check, done } from "./emu_harness.mjs";

const dir = process.argv[2] || "template/build/emu";
const emu = await startEmu(dir);
check(emu.take("ready").length === 1, "the worklet says ready after the load");
emu.run(700);
const frames = emu.take("frame");
const fb = frames.filter((f) => f.fb).pop();
check(frames.length >= 15, `about 30 frames a second reach the page (${frames.length} in 0.7 s)`);
check(!!fb && fb.fb.some((v) => v !== 0), "the screen is drawn after boot");
const quiet = emu.run(200);
emu.send({ type: "keys", mask: 1 << 7 });
const loud = emu.run(300);
check(loud > 0.01 && loud > quiet * 4, `a key sounds (peak ${loud.toFixed(4)}, ${quiet.toFixed(4)} before)`);
const pkts = new Uint32Array(emu.ex.memory.buffer, emu.ex.web_out_pkts(), emu.ex.web_out_n());
check([...pkts].some((p) => (p >> 8 & 0xff) === 0x90 && (p >> 16 & 0xff) === 60), "its note goes out (USB-MIDI 90 3C ..)");
emu.ex.web_out_clear();
check(emu.take("frame").some((f) => f.keys & 1 << 7), "the key's light reaches the page");
emu.send({ type: "keys", mask: 0 });
emu.run(1500);
const after = emu.run(100);
emu.ex.web_midi_in(0, 0x09 | 0x90 << 8 | 64 << 16 | 100 << 24);
const midi = emu.run(300);
check(midi > 0.01 && midi > after * 4, `a note-on from MIDI sounds (peak ${midi.toFixed(4)})`);
emu.ex.web_midi_in(0, 0x08 | 0x80 << 8 | 64 << 16);
emu.run(1100);
check(emu.take("frame").some((f) => f.load !== undefined), "the share of the computer's time is said once a second");
done();
