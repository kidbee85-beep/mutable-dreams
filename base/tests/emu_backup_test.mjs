// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "backup" (web/emu/parts/backup*.js): tests/backup_app built with it. A backup taken in the emulator is
// the file the host simulator's `backup` writes (the same bytes) and tools/fm1_backup.py reads; one the host wrote
// restores in the emulator; a damaged, foreign or refused one changes nothing; the page's file name, extras and texts.
//   node tests/emu_backup_test.mjs
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { startEmu, buildEmu, check, done } from "./emu_harness.mjs";

const root = new URL("..", import.meta.url).pathname, app = "tests/backup_app";
const py = process.env.PYTHON || "python3";
const dir = buildEmu(app, "backup");
execFileSync(py, ["tools/build.py", app, "--host"], { cwd: root, stdio: ["ignore", "ignore", "inherit"] });
const out = `${root}${app}/build/out`;
mkdirSync(out, { recursive: true });
execFileSync(`${root}${app}/build/host/bktest_host`, [`${root}${app}/tests/backup.txt`, out], { stdio: ["ignore", "ignore", "inherit"] });
const hostFile = new Uint8Array(readFileSync(`${out}/a.bkt`));   // three SEL taps, then `backup`: value 3

const emu = await startEmu(dir);
const BTN = { SEL: 1 << 1, PLAY: 1 << 10 };
const tap = (mask) => { emu.send({ type: "buttons", mask }); emu.run(100); emu.send({ type: "buttons", mask: 0 }); emu.run(100); };
const value = () => (emu.run(100), emu.take("frame").at(-1).buttons & 0x3fff);
const ask = (m) => { emu.take("backup"); emu.send({ type: "app", part: "backup", ...m }); emu.run(50); return emu.take("backup").at(-1); };
const make = () => new Uint8Array(ask({ cmd: "make" }).backup);
const restore = (bytes) => ask({ cmd: "restore", image: Uint8Array.from(bytes).buffer }).restored;
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

emu.run(300);
tap(BTN.SEL); tap(BTN.SEL); tap(BTN.SEL);
const made = make();
check(new TextDecoder().decode(made.subarray(0, 4)) === "RBTB", "the backup starts with the app's magic");
check(same(made, hostFile), `the same bytes as the host simulator's backup (${made.length})`);
writeFileSync(`${out}/emu.bkt`, made);
const info = execFileSync(py, ["tools/fm1_backup.py", "info", `${out}/emu.bkt`], { cwd: root }).toString();
check(/RBTB, version 1/.test(info) && /1 of 1 parts hold something/.test(info), "tools/fm1_backup.py reads it");

// more taps, then the backup back: the count returns to 3, and the page is told the object changed
tap(BTN.SEL); tap(BTN.SEL);
check(value() === 5, "two more taps: 5");
emu.take("frame");
check(restore(made) === 0, "the backup is restored");
emu.run(200);
const frames = emu.take("frame");
check((frames.at(-1).buttons & 0x3fff) === 3, "the app reloaded: 3 again");
const st = frames.find((f) => f.store);
check(st && new DataView(st.store[0].buffer, st.store[0].byteOffset).getUint32(0, true) === 3, "the saved object is the backup's 3, sent to the page");

// a file the host wrote (and could have come from an FM-1) restores too
tap(BTN.SEL);
check(restore(hostFile) === 0 && value() === 3, "a backup written by the host simulator restores");

// refused or damaged: nothing changes
const flip = (i) => { const b = Uint8Array.from(made); b[i] ^= 1; return b; };
const before = value();
check(restore(flip(0)) === 3, "another app's magic: refused (3)");
check(restore(flip(4)) === 3, "another version: refused (3)");
check([4, 5, 6].includes(restore(flip(made.length - 8))), "a changed byte in an object: refused as damaged");
check(restore(made.subarray(0, made.length - 1)) !== 0, "a file cut short: refused");
check(value() === before, "none of them changed anything");
tap(BTN.PLAY);
check(restore(made) === 10, "playing: refused (stop first)");
check((make()[8] & 1) === 1, "a backup while playing says so (flag bit 0)");
tap(BTN.PLAY);

// the page side
const page = await import("../web/emu/parts/backup.js");
check(page.fileName("BKTEST", "bkt", new Date(2026, 9, 28)) === "bktest-backup-2026-10-28.bkt", "the file name is the backup page's");
const extras = Uint8Array.from([...made, ...new TextEncoder().encode("EXTR"), 3, 0, 0, 0, 1, 2, 3, 0, 0, 0, 0]);
check(page.imageLength(extras) === made.length, "the firmware's own sections after the backup are left out");
check((() => { try { page.imageLength(made.subarray(0, 20)); } catch { return true; } return false; })(), "a cut-short file is refused");
check(/playing/.test(page.restoreText(10, "BKTEST")) && /damaged/.test(page.restoreText(5, "BKTEST"))
  && /not a BKTEST backup/.test(page.restoreText(3, "BKTEST")) && /Restored/.test(page.restoreText(0, "BKTEST")), "the page's texts");
done();
