// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// Emulator part "reboot" (fm1.toml emu = ["reboot"]): the page's side. It hands the worklet the wasm's own bytes at load
// (the worklet starts it again on a restart), adds "Restart" and "Update mode" to the bar, and says what the FM-1 is
// doing: running, restarting (the app called plat_restart; the saved data is kept), or in update mode (the sound
// stopped, nothing runs until Restart; hold REC while pressing Restart for the chip's own update mode). The worklet's
// side is reboot_worklet.js.

const TEXT = {
  running: "",
  restarting: (why) => (why === "app" ? "Restarting: the app asked. What it saved is kept." : "Restarting. What was saved is kept."),
  update: (why) => `Update mode (${why}): the sound is stopped and nothing runs. Press Restart to leave it.`,
};

export function describe(m) {
  const t = TEXT[m.state];
  return typeof t === "function" ? t(m.why) : t || "";
}

let post = () => {}, line = null;

export function attach(ctx) {
  post = ctx.post;
  ctx.beforeLoad((msg) => {
    msg.parts.reboot = { wasm: msg.wasm.slice(0) };
    return msg;
  });
  if (!ctx.bar) return;
  line = document.createElement("span");
  line.id = "reboot-state";
  line.setAttribute("role", "status");
  const mk = (text, title, cmd) => {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = text;
    b.title = title;
    b.addEventListener("click", () => post({ cmd }));
    return b;
  };
  const anchor = ctx.bar.querySelector("#reset");
  for (const el of [mk("Restart", "Start the FM-1 again: its RAM is cleared, what it saved is kept", "restart"),
    mk("Update mode", "What a computer's update request does: the FM-1 stops and waits", "update"), line]) {
    ctx.bar.insertBefore(el, anchor);
  }
}

export function message(m) {
  if (line) line.textContent = describe(m);
}
