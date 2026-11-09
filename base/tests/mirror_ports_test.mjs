// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The mirror page (web/mirror/index.html, the part between its "ports" markers, with web/fm1ports.js inlined as
// tools/make_site.py does) finding the FM-1 on a fake Web MIDI: a port named after the firmware or with a usual name is
// taken; the saved pick is kept; an input's output found by fm1ports.js's rule; an FM-1 under another device's name found
// by its answer to the screen reader's HELLO, while a loopback (an IAC bus sending the HELLO back) and other gear get at
// most three HELLOs while plugged in and are never taken.
//   node tests/mirror_ports_test.mjs
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), "..");
const html = fs.readFileSync(path.join(root, "web/mirror/index.html"), "utf8");
const m = /\/\/ --- ports: begin[^\n]*\n([\s\S]*?)\/\/ --- ports: end/.exec(html);
if (!m) { console.log("FAIL no ports markers in web/mirror/index.html"); process.exit(1); }
const PORTS_JS = fs.readFileSync(path.join(root, "web/fm1ports.js"), "utf8").replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
if (!m[1].includes("{{PORTS_JS}}")) { console.log("FAIL the ports part has no {{PORTS_JS}}"); process.exit(1); }
const { mirrorPorts, Asking } =
  new Function(`"use strict";\n${m[1].replace("{{PORTS_JS}}", () => PORTS_JS)}\nreturn { mirrorPorts, Asking };`)();

let fails = 0;
const check = (ok, msg) => { console.log(`${msg.padEnd(72)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };

let ids = 0;
// a port pair; answer(bytes, hear) is the device behind it
function port(name, answer, outName = name) {
  const ears = new Set(), sent = [];
  const input = { id: "i" + ++ids, name, state: "connected", open: async () => {},
                  addEventListener: (t, f) => ears.add(f), removeEventListener: (t, f) => ears.delete(f) };
  const hear = (b) => setTimeout(() => ears.forEach((f) => f({ data: Uint8Array.from(b), target: input })), 1);
  const output = { id: "o" + ids, name: outName, state: "connected", open: async () => {},
                   send: (b) => { sent.push(Array.from(b)); answer && answer(Array.from(b), hear); } };
  return { input, output, sent };
}
const midi = (ps) => ({ inputs: new Map(ps.map((p) => [p.input.id, p.input])), outputs: new Map(ps.map((p) => [p.output.id, p.output])) });
const HELLO = [0xF0, 0x7D, 0x53, 0x52, 1, 1, 0xF7].join();
const fm1 = (firmware) => (b, hear) => {
  if (b.join() === HELLO) hear([0xF0, 0x7D, 0x53, 0x52, 1, 1, ...Buffer.from(firmware), 0, 0xF7]);
};
const loopback = (b, hear) => hear(b);

const MP = mirrorPorts("Template");
{
  // by name: the firmware's name first, then a usual one; never the loader's "ota", never other gear
  const other = port("Arturia KeyStep"), usual = port("FM-1 MIDI 1"), named = port("Template");
  let c = MP.choose(midi([other, usual, named]));
  check(c && c.input === named.input && c.output === named.output, "a port named after the firmware: taken first");
  c = MP.choose(midi([other, usual]));
  check(c && c.input === usual.input, "  else one with a usual name");
  check(MP.choose(midi([other, port("OTA Loader")])) === null, "  not the update loader, not other gear");
  check(other.sent.length === 0, "  nothing sent by choosing");
}
{
  // the saved pick stays (the user's own), with its output
  const a = port("Arturia KeyStep"), b = port("Template");
  const c = MP.choose(midi([a, b]), { in: "Arturia KeyStep", out: "Arturia KeyStep" });
  check(c && c.input === a.input && c.output === a.output, "the saved pick: kept");
}
{
  // an input's output: the same name but for its number (Windows numbers each port)
  const p = port("FM-1 MIDI 1", null, "FM-1 MIDI 2"), q = port("Launchkey Mini");
  const c = MP.choose(midi([p, q]));
  check(c && c.input === p.input && c.output === p.output, "an output named the same but for its number");
}
{
  // an FM-1 under another device's name: found by its HELLO answer; the loopback and other gear never taken
  const iac = port("IAC Bus 1", loopback), other = port("Launchkey Mini"), dev = port("Arturia KeyStep", fm1("Template 1.0"));
  const acc = midi([iac, other, dev]), asking = new Asking(), heard = new Set();
  check(MP.choose(acc, {}, heard) === null, "an FM-1 on an unusual port: not taken by its name");
  const got = await MP.seek(acc, asking, heard, 20);
  check(got && got.input === dev.input && got.output === dev.output && got.firmware === "Template 1.0",
        "  found by its answer to the HELLO");
  check(heard.has("Arturia KeyStep") && MP.choose(acc, {}, heard)?.input === dev.input, "  taken by its name from then on");
  check(dev.sent.length === 1 && dev.sent.every((b) => b.join() === HELLO), "  the FM-1: one HELLO");
  check(iac.sent.length === 1 && other.sent.length === 1, "  the loopback and the other gear: one HELLO each so far");
  // asked again (the ports changed): at most three HELLOs while plugged in, and only HELLOs
  for (let i = 0; i < 4; i++) await MP.seek(acc, asking, heard, 20);
  check(iac.sent.length === 3 && other.sent.length === 3 && [...iac.sent, ...other.sent].every((b) => b.join() === HELLO),
        "  then three HELLOs at most, nothing else");
  check(dev.sent.length === 1, "  the FM-1 found: not asked again");
  asking.forget(other.output.id);                     // unplugged and plugged in again: asked again
  await MP.seek(acc, asking, heard, 20);
  check(other.sent.length === 6 && iac.sent.length === 3, "  a port plugged in again: asked again (three times)");
}
{
  // two FM-1s answering: the one running this firmware first; nobody answering: null after three tries
  const x0x = port("Port A", fm1("X0X 1.1")), tpl = port("Port B", fm1("Template 1.0"));
  const got = await MP.seek(midi([x0x, tpl]), new Asking(), new Set(), 20);
  check(got && got.input === tpl.input, "two answering: the one running this firmware");
  const lone = port("IAC Bus 2", loopback), asking = new Asking();
  check(await MP.seek(midi([lone]), asking, new Set(), 20) === null && lone.sent.length === 3, "only a loopback: none, after three HELLOs");
}

{
  // the whole page as tools/make_site.py fills it: every {{KEY}} filled, its script parses
  const PANEL_JS = fs.readFileSync(path.join(root, "web/fm1panel.js"), "utf8").replace(/^export\s+/gm, "");
  const page = html.replace(/\{\{([A-Z_]+)\}\}/g, (x, k) => ({ NAME: "Template", PORTS_JS, PANEL_JS }[k] ?? x));
  const left = page.match(/\{\{[A-Z_]+\}\}/g);
  check(!left, `the page filled: ${left ? left.join(" ") : "no {{KEY}} left"}`);
  const js = /<script>([\s\S]*?)<\/script>/.exec(page);
  let ok = !!js;
  try { new Function(js[1]); } catch (e) { ok = false; console.log(e.message); }
  check(ok, "the page's script parses");
}

console.log(fails ? `FAILED ${fails}` : "ok");
process.exit(fails ? 1 : 0);
