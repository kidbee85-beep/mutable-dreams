// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// An .rfm1 (tools/build.py APP_DIR --rfm1) driven as a host app drives it (RemoteFM1IOS's docs/RFM1-FORMAT.md, 7):
// the manifest, the descriptor and its layout, a boot from a fresh flash, the clock for a while (the screen drawn,
// the LEDs), a chord (sound), a button and a knob, the screen reader over SysEx, and the flash image kept: a second
// instance booted from the first one's image has the same saved objects.
//   node tests/rfm1_test.mjs FILE.rfm1
import { readFileSync } from "node:fs";

const file = process.argv[2];
const bytes = readFileSync(file);
let fails = 0;
const ok = (c, what) => { console.log(`  ${c ? "ok  " : "FAIL"} ${what}`); if (!c) fails++; };

const mod = new WebAssembly.Module(bytes);
const sect = (n) => WebAssembly.Module.customSections(mod, n).map((b) => new TextDecoder().decode(b));
const man = JSON.parse(sect("rfm1")[0]);
ok(man.rfm1 === 1 && man.abi === 6 && /^[a-z0-9_]{1,31}$/.test(man.id), `manifest: ${man.name} ${man.version} (${man.id}), ${man.license}`);
ok(sect("rfm1.credits").length === 1, "credits");
ok(sect("rfm1.license.0").length === 1, "licence texts");

function instance() {
    let mem;
    const wasi = {
        fd_write: (fd, iov, n, nw) => {
            const v = new DataView(mem.buffer);
            let t = 0;
            for (let i = 0; i < n; i++) t += v.getUint32(iov + 8 * i + 4, true);
            v.setUint32(nw, t, true);
            return 0;
        },
        fd_read: () => 8, fd_close: () => 8, fd_seek: () => 8,
        clock_time_get: (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, process.hrtime.bigint(), true); return 0; },
        proc_exit: (c) => { throw new Error(`proc_exit ${c}`); },
    };
    for (const i of WebAssembly.Module.imports(mod))
        if (i.module !== "wasi_snapshot_preview1" || !(i.name in wasi)) throw new Error(`import ${i.module}.${i.name}`);
    const x = new WebAssembly.Instance(mod, { wasi_snapshot_preview1: wasi }).exports;
    mem = x.memory;
    if (x._initialize) x._initialize();
    return x;
}

function boot(x, image) {
    const u8 = () => new Uint8Array(x.memory.buffer);
    const dv = () => new DataView(x.memory.buffer);
    const L = (i) => x.rfm1_layout(i);
    const d = x.rfm1_get(6);
    const panel = dv().getUint32(d + L(30), true);
    const cstr = (p) => { let e = p; while (u8()[e]) e++; return new TextDecoder().decode(u8().subarray(p, e)); };
    if (image) {
        const p = x.rfm1_alloc(image.length);
        u8().set(image, p);
        x.rfm1_flash_stage(p, image.length);
        x.rfm1_free(p);
    } else {
        x.rfm1_flash_stage(0, 0);
    }
    const s = x.rfm1_alloc(32);
    u8().set(new TextEncoder().encode("fm1core:staged\0"), s);
    x.rfm1_options(s, 0, 0, 1);
    x.rfm1_boot_options(-1, 0, -1);
    u8()[panel + L(16)] = 2;
    x.rfm1_init(0);
    const ms = { now: 0, due: 0 };
    const audio = x.rfm1_alloc(128 * 4);
    const run = (n, onBlock) => {
        for (let i = 0; i < n; i++, ms.now++) {
            x.rfm1_tick(ms.now);
            if (ms.now % 15 === 0) x.rfm1_frame(); else x.rfm1_idle();
            while (ms.due <= ms.now) {
                x.rfm1_audio(audio, 128);
                if (onBlock) onBlock(new Int16Array(x.memory.buffer, audio, 256));
                ms.due += 128 * 1000 / 44100;
            }
        }
    };
    const outs = () => {
        const p = x.rfm1_alloc(4), r = [];
        while (x.rfm1_midi_out_take(p)) r.push(dv().getUint32(p, true));
        x.rfm1_free(p);
        return r;
    };
    return { d, panel, L, u8, dv, cstr, run, outs };
}

// the descriptor and its layout
const a = instance();
ok(a.rfm1_version() === 1, "rfm1_version 1");
const A = boot(a);
ok(A.d !== 0 && a.rfm1_get(5) === 0, "rfm1_get(6) a descriptor, (5) none");
ok(A.dv().getUint32(A.d + A.L(18), true) === 6, "descriptor ABI 6");
ok(A.cstr(A.dv().getUint32(A.d + A.L(19), true)) === man.id && A.cstr(A.dv().getUint32(A.d + A.L(21), true)) === man.version,
   "descriptor id and version = the manifest's");
ok(A.dv().getUint32(A.d + A.L(26), true) === 0x100000, "flash 1 MiB");
ok(a.rfm1_layout(34) === -1 && A.L(17) > A.L(33), "layout: 34 entries");

// the clock: the screen drawn, the LEDs
const lcdW = () => A.dv().getUint32(A.panel + A.L(12), true);
A.run(2000);
ok(lcdW() > 0, `the screen drawn (${lcdW()} writes)`);
const lcd = new Uint16Array(a.memory.buffer, A.panel + A.L(11), 240 * 240);
ok(lcd.some((v) => v !== lcd[0]), "the screen has a picture");

// a chord on keys 7, 11, 14 (C4 E4 G4 from F3), velocity 100: sound
const kv = A.panel + A.L(7);
for (const k of [7, 11, 14]) A.u8()[kv + k] = 100;
A.dv().setUint32(A.panel + A.L(1), (1 << 7) | (1 << 11) | (1 << 14), true);
let sq = 0, n = 0;
A.run(1000, (b) => { for (const v of b) { sq += v * v; n++; } });
A.dv().setUint32(A.panel + A.L(1), 0, true);
A.run(500);
const rms = Math.sqrt(sq / n) / 32768;
ok(rms > 0.001, `the chord sounds (rms ${rms.toFixed(4)})`);
const leds = A.u8().subarray(A.panel + A.L(9), A.panel + A.L(9) + 44);
console.log(`       LEDs lit: ${[...leds].filter((v) => v).length}`);

// a button tap and a knob: the app keeps running
const btnId = A.u8().subarray(A.panel + A.L(13), A.panel + A.L(13) + 14);
ok([...btnId].every((v) => v < 14) && new Set(btnId).size === 14, "btn_id: the 14 buttons");
A.dv().setUint32(A.panel + A.L(4), 1 << btnId[6], true);            // HOME tapped
A.dv().setInt32(A.panel + A.L(5) + 4 * A.u8()[A.panel + A.L(14) + 3], 3, true);   // K1, three detents
A.run(300);
ok(a.rfm1_halted() === 0, "running");

// the screen reader: HELLO in as SysEx, its answer out
const sx = [0xF0, 0x7D, 0x53, 0x52, 0x01, 0x01, 0xF7];
const pk = (cin, b) => cin | (b[0] << 8) | ((b[1] || 0) << 16) | ((b[2] || 0) << 24);
a.rfm1_midi_in(pk(4, sx.slice(0, 3)) >>> 0);
a.rfm1_midi_in(pk(4, sx.slice(3, 6)) >>> 0);
a.rfm1_midi_in(pk(5, sx.slice(6)) >>> 0);
A.run(100);
const said = A.outs().filter((p) => (p & 0xF) >= 4 && (p & 0xF) <= 7);
ok(said.length > 0 && (said[0] >>> 8 & 0xFF) === 0xF0 && (said[0] >>> 16 & 0xFF) === 0x7D, `the reader answered (${said.length} packets)`);

// the flash: kept and booted from
a.rfm1_flash_sync();
const img = new Uint8Array(a.memory.buffer, a.rfm1_flash(), 0x100000).slice();
const dirty = a.rfm1_flash_dirty();
const b = instance();
const B = boot(b, img);
B.run(200);
const img2 = new Uint8Array(b.memory.buffer, b.rfm1_flash(), 0x100000);
const hdr = (m) => new DataView(m.buffer, m.byteOffset).getUint32(0, true);
const storeLen = (m) => (hdr(m) === 0x31424D46 ? 16 + new DataView(m.buffer, m.byteOffset).getUint32(8, true) : 0);
console.log(`       flash: ${dirty} writes, ${storeLen(img)} B of saved objects`);
ok(storeLen(img) === 0 || img.subarray(0, storeLen(img)).every((v, i) => v === img2[i]), "a second instance booted from the image has its objects");
ok(img.subarray(0x93000, 0xFC000).every((v, i) => v === img2[0x93000 + i]), "and its data area");

console.log(fails ? `${fails} FAILED` : "rfm1: all ok");
process.exit(fails ? 1 : 0);
