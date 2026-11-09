#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Henri David
"""The browser emulator's optional parts (fm1.toml emu, FM1B_EMU; tools/build.py --web): the template built with none
(FM1B_EMU="") makes no app.js and no parts/; built as it is (fm1.toml), it has the parts on without asking
(EMU_DEFAULT: resample) and the same page, worklet and app.wasm; "-resample" leaves it out; an unknown part is
refused with the list; with every part there is, app.js and app_worklet.js load each one and its files are there; a
build without them again leaves nothing of them and the same bytes.
  tests/emu_parts_test.py"""
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EMU = ROOT / "template" / "build" / "emu"
PARTS = ROOT / "web" / "emu" / "parts"
FAILS = 0


def check(ok, what):
    global FAILS
    print(f"  {'ok' if ok else 'FAIL'} {what}")
    FAILS += not ok


def build(parts):
    env = {k: v for k, v in os.environ.items() if k != "FM1B_EMU"}
    if parts is not None:
        env["FM1B_EMU"] = parts
    return subprocess.run([sys.executable, "tools/build.py", "template", "--web"], cwd=ROOT, env=env,
                          capture_output=True, text=True)


def sums():
    return {f: hashlib.sha256((EMU / f).read_bytes()).hexdigest() for f in ("index.html", "worklet.js", "app.wasm")}


r = build("")
check(r.returncode == 0, "the template builds for the web")
plain = sums()
check(not (EMU / "app.js").exists() and not (EMU / "parts").exists(), "FM1B_EMU=\"\": no app.js, no parts/")
r = build(None)
check(r.returncode == 0 and sums() == plain and (EMU / "parts" / "resample_worklet.js").exists()
      and '"./parts/resample_worklet.js"' in (EMU / "app_worklet.js").read_text(),
      "as the template is: resample on without asking, the same page, worklet and app.wasm")
with tempfile.TemporaryDirectory() as t:           # the template with fm1.toml emu = ["midi", "-resample"]
    app = Path(t) / "app"
    shutil.copytree(ROOT / "template", app, ignore=shutil.ignore_patterns("build"))
    (app / "fm1.toml").write_text((app / "fm1.toml").read_text() + '\nemu = ["midi", "-resample"]\n')
    env = {k: v for k, v in os.environ.items() if k != "FM1B_EMU"}
    r = subprocess.run([sys.executable, "tools/build.py", str(app), "--web"], cwd=ROOT, env=env, capture_output=True, text=True)
    out = app / "build" / "emu"
    check(r.returncode == 0 and (out / "parts" / "midi.js").exists() and not (out / "parts" / "resample_worklet.js").exists(),
          "fm1.toml emu = [\"midi\", \"-resample\"]: midi without resample")
r = build("-nosuch")
check(r.returncode != 0 and "no part 'nosuch'" in r.stdout + r.stderr, "leaving out an unknown part is refused")
r = build("nosuch")
check(r.returncode != 0 and "no part 'nosuch'" in r.stdout + r.stderr, "an unknown part is refused")
names = sorted({m[1] for f in PARTS.glob("*.js") if (m := re.fullmatch(r"([a-z0-9]+)(_worklet)?\.js", f.name))} - {"parts"})
if names:
    r = build(" ".join(names))
    check(r.returncode == 0, f"every part builds: {' '.join(names)}")
    page, work = (EMU / "app.js").read_text(), (EMU / "app_worklet.js").read_text()
    for n in names:
        check((PARTS / f"{n}.js").exists() <= (f'"./parts/{n}.js"' in page)
              and (PARTS / f"{n}_worklet.js").exists() <= (f'"./parts/{n}_worklet.js"' in work)
              and all((EMU / "parts" / f.name).exists() for f in PARTS.glob(f"{n}*.js")), f"{n}: loaded and copied")
    check(sums()["index.html"] == plain["index.html"] and sums()["worklet.js"] == plain["worklet.js"],
          "the page and worklet.js themselves unchanged with parts")
    r = build("")
    check(r.returncode == 0 and sums() == plain and not (EMU / "app.js").exists() and not (EMU / "parts").exists(),
          "built again without: the same bytes, the parts' files gone")
print("all passed" if not FAILS else f"{FAILS} failed")
sys.exit(1 if FAILS else 0)
