// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// The dim lights' brightness in the browser emulator (APP_DIM_LEVELS, plat_dim_ns): the frame messages carry dimOp, the
// dim controls' opacity, only for an app that has the levels; the page (web/emu/index.html, which already draws the dim
// lights) sets --dimop from it. The masks themselves are the page's own and are not tested here.
//   node tests/emu_dim_test.mjs
import { startEmu, buildEmu, check, done } from "./emu_harness.mjs";

const plain = await startEmu(buildEmu("template"));
plain.run(500);
check(plain.take("frame").every((f) => f.dimOp === undefined), "the template's frames carry no dimOp");

const lv = await startEmu(buildEmu("tests/dim_app"));
const op = () => lv.take("frame").at(-1)?.dimOp;
lv.run(500);
check(op() === 0.829, "the glow's own pulse (3,200 ns) before the app sets one");
for (const [bit, want] of [[0, 0.25], [1, 0.357], [2, 0.571], [3, 1], [4, 0.25]]) {
  lv.send({ type: "buttons", mask: 1 << bit });
  lv.run(200);
  lv.send({ type: "buttons", mask: 0 });
  lv.run(200);
  const got = op();
  check(got === want, `button ${bit}: dimOp ${want} (${got})`);
}
lv.send({ type: "buttons", mask: 1 << 5 });
lv.run(200);
check(op() === 0.829, "handed back: the glow's pulse again");
done();
