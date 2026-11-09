// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The FM-1's panel drawn in SVG, and its lights: one drawing for every page that shows the panel (the emulator,
// web/emu/index.html or an app's own; the mirror, web/mirror/index.html). A page inlines it (its PANEL_JS placeholder:
// tools/build.py fills it in for the emulator, tools/make_site.py for the mirror) and adds its own controls, held states and names on top.
//
//   const P = fm1Panel(svg)      the body, the knobs, the buttons, the keys and the screen's bezel into svg
//                                (viewBox 0 0 1000 640); the canvas over the bezel is the page's
//   P.knobs[label]               { g, ind, ring, x, y, r, angle, role, label, name }: MASTER, SELECT, PRESETS,
//                                ALGORITHM, KNOB1..KNOB4 (role: ENC, or "master"); ring for a page's feedback (class
//                                "ring", hidden unless the page shows it)
//   P.byRole[ENC.X]              the same knobs by encoder
//   P.buttons[B_*]               { g, bit, label ("PLAY STOP"), name ("PLAY", "OCT minus") }; P.buttonsByLabel["OCT-"]
//   P.keys[k]                    { g, name ("White key 1") }, k = 0 (F3) .. 26
//   P.lights(b, k, db, dk)       lit (class "lit") and dim (class "dim": plat_lights' dim ones and plat_lights_mid's;
//                                lit wins) buttons (bit B_*) and keys (bit k)
//   P.el(tag, attrs, parent)     P.text(parent, x, y, s, size, fill, weight)   (for a page's own marks)
//
// The colours of the lights are CSS custom properties with these defaults (fm1-base's coral and teal); a page or an
// app sets them on :root for its own: --panel-lit, --panel-lit-ink (a lit button's label), --panel-lit-white,
// --panel-lit-black, --panel-dim, --panel-dim-white, --panel-dim-black. Its rules go first in <head>, so a page's own
// (held, a green PLAY) win over them.
export const PANEL_BTN = { FX: 0, SEL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11,
                           "OCT-": 12, "OCT+": 13 };
export const PANEL_ENC = { SELECT: 0, ALGORITHM: 1, PRESETS: 2, KNOB1: 3, KNOB2: 4, KNOB3: 5, KNOB4: 6 };
export const PANEL_WHITE_K = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
export const PANEL_BLACK_K = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
export const PANEL_ROW1 = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"], PANEL_ROW2 = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
const PANEL_CSS = `
.btnface, .keyface { transition: fill .06s; }
.lit .btnface { fill: var(--panel-lit, #f28a5a); }
.lit text.btn { fill: var(--panel-lit-ink, #2a140a); }
.lit .keyface.white { fill: var(--panel-lit-white, #f6b48e); }
.lit .keyface.black { fill: var(--panel-lit-black, #4cc3b6); }
.dim .btnface { fill: var(--panel-dim, #6e4636); }
.dim .keyface.white { fill: var(--panel-dim-white, #e8c9b6); }
.dim .keyface.black { fill: var(--panel-dim-black, #8ac6bf); }
@media (prefers-reduced-motion: reduce) { .btnface, .keyface { transition: none; } }`;

export function fm1Panel(svg) {
  const NS = "http://www.w3.org/2000/svg";
  const BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14];
  const BODY = "#3a3d42", TRAY = "#26282c", KEY_W = "#d9dadc", KEY_B = "#c9cacd", KNOB = "#1f2124", BTNC = "#2e3135",
        LABEL = "#e8e8ea";
  if (!document.getElementById("fm1panel-css")) {
    const st = document.createElement("style");
    st.id = "fm1panel-css";
    st.textContent = PANEL_CSS;
    document.head.prepend(st);
  }
  function el(tag, attrs, parent = svg) {
    const e = document.createElementNS(NS, tag);
    for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
    parent.appendChild(e);
    return e;
  }
  function text(parent, x, y, s, size, fill = LABEL, weight = 600) {
    const t = el("text", { x, y, "text-anchor": "middle", "font-family": "'Barlow Semi Condensed', 'Arial Narrow', Arial, sans-serif",
                           "font-size": size, "font-weight": weight, fill, "letter-spacing": ".05em", class: "btn",
                           "pointer-events": "none" }, parent);
    t.textContent = s;
    return t;
  }
  el("rect", { x: 4, y: 4, width: 992, height: 632, rx: 44, fill: BODY });
  const knobs = {}, byRole = [];
  function knob(x, y, r, label, role, name) {
    const g = el("g", { class: "ctl knob" });
    el("circle", { cx: x, cy: y, r: r + 5, fill: TRAY }, g);
    const ring = el("circle", { cx: x, cy: y, r: r + 9, class: "ring", fill: "none", opacity: 0 }, g);
    el("circle", { cx: x, cy: y, r, fill: KNOB, stroke: "#44474c", "stroke-width": 2, class: "knobface" }, g);
    const ind = el("line", { x1: x, y1: y - r + 5, x2: x, y2: y - r + 15, stroke: LABEL, "stroke-width": 3, "stroke-linecap": "round" }, g);
    text(g, x, y - r - 12, label, 17);
    el("circle", { cx: x, cy: y, r: r + 14, fill: "transparent" }, g);           // (the knob's target: a finger's size)
    const k = { g, ind, ring, x, y, r, angle: 0, role, label, name: name || label };
    knobs[label] = k;
    if (typeof role === "number") byRole[role] = k;
    return k;
  }
  knob(105, 100, 24, "MASTER", "master");
  knob(215, 100, 24, "SELECT", PANEL_ENC.SELECT);
  knob(105, 205, 24, "PRESETS", PANEL_ENC.PRESETS);
  knob(215, 205, 24, "ALGORITHM", PANEL_ENC.ALGORITHM);
  for (let i = 0; i < 4; i++) knob(610 + i * 104, 100, 22, `KNOB${i + 1}`, PANEL_ENC[`KNOB${i + 1}`], `KNOB ${i + 1}`);

  const buttons = [], buttonsByLabel = {};
  function button(x, y, w, h, label, two) {
    const g = el("g", { class: "ctl" });
    el("rect", { x, y, width: w, height: h, rx: 9, fill: BTNC, stroke: "#4a4d52", "stroke-width": 1.5, class: "btnface" }, g);
    if (two) { text(g, x + w / 2, y + h / 2 - 1, label, 12); text(g, x + w / 2, y + h / 2 + 12, two, 12); }
    else text(g, x + w / 2, y + h / 2 + 5, label, 15);
    const b = { g, bit: PANEL_BTN[label], label: two ? `${label} ${two}` : label,
                name: { "OCT-": "OCT minus", "OCT+": "OCT plus" }[label] || label };
    buttons[b.bit] = buttonsByLabel[label] = b;
  }
  el("rect", { x: 58, y: 258, width: 210, height: 58, rx: 14, fill: TRAY });
  button(72, 268, 82, 38, "OCT-"); button(170, 268, 82, 38, "OCT+");
  el("rect", { x: 300, y: 44, width: 236, height: 236, rx: 26, fill: "#111214" });
  el("rect", { x: 568, y: 160, width: 400, height: 140, rx: 18, fill: TRAY });
  for (let i = 0; i < 6; i++) button(582 + i * 63, 174, 54, 48, PANEL_ROW1[i]);          // row by row: the reading order
  for (let i = 0; i < 6; i++) button(582 + i * 63, 236, 54, 48, PANEL_ROW2[i], PANEL_ROW2[i] === "PLAY" ? "STOP" : undefined);

  const keys = [];
  (function keybed(x0, y0, w, h) {
    el("rect", { x: x0, y: y0, width: w, height: h, rx: 18, fill: TRAY });
    const pitch = (w - 24) / 16, kw = pitch * 0.78;
    BLACK_AFTER.forEach((a, k) => {                                                   // the black keys: the upper row
      const g = el("g", { class: "ctl" });
      el("rect", { x: x0 + 12 + (a + 1) * pitch - kw / 2, y: y0 + h * 0.06, width: kw, height: h * 0.33, rx: kw / 2, fill: KEY_B,
                   class: "keyface black" }, g);
      keys[PANEL_BLACK_K[k]] = { g, name: `Black key ${k + 1}` };
    });
    for (let i = 0; i < 16; i++) {
      const g = el("g", { class: "ctl" });
      el("rect", { x: x0 + 12 + i * pitch + (pitch - kw) / 2, y: y0 + h * 0.43, width: kw, height: h * 0.52, rx: kw / 2, fill: KEY_W,
                   class: "keyface white" }, g);
      keys[PANEL_WHITE_K[i]] = { g, name: `White key ${i + 1}` };
    }
  })(36, 340, 928, 270);

  function lights(b, k, db = 0, dk = 0) {
    for (const x of buttons) {
      if (!x) continue;
      const lit = !!(b & (1 << x.bit));
      x.g.classList.toggle("lit", lit);
      x.g.classList.toggle("dim", !lit && !!(db & (1 << x.bit)));
    }
    keys.forEach((x, i) => {
      const lit = !!(k & (1 << i));
      x.g.classList.toggle("lit", lit);
      x.g.classList.toggle("dim", !lit && !!(dk & (1 << i)));
    });
  }
  return { knobs, byRole, buttons, buttonsByLabel, keys, lights, el, text };
}
