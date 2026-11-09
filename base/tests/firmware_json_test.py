#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""tools/firmware_json.py: firmware.json for a few fm1.toml files (no Bluetooth; every part; a key left out; an
unknown tag or a long description refused), the keys a release needs (refused without them, a dev build warned),
the screenshot's checks (its path, a PNG, square, at least 240 x 240, at most 2 MiB), where a build writes it, the flash parts against device/fm1_flash_ok.h,
and the vocabularies the same in the code, docs/firmware.schema.json and docs/FIRMWARE-JSON.md. The output is
checked against the schema when jsonschema is there.
  python3 tests/firmware_json_test.py"""
import contextlib
import io
import json
import re
import struct
import zlib
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import firmware_json as F  # noqa: E402
import build as B  # noqa: E402

SCHEMA = json.loads((ROOT / "docs" / "firmware.schema.json").read_text())
DOC = (ROOT / "docs" / "FIRMWARE-JSON.md").read_text()
try:
    import jsonschema
except ImportError:
    jsonschema = None
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<72} {'ok' if cond else 'FAIL'}")
    failed += not cond


def refused(fn, words):
    try:
        fn()
    except SystemExit as e:
        return words in str(e)
    return False


def valid(d):
    if jsonschema is None:
        return True
    try:
        jsonschema.validate(d, SCHEMA)
        return True
    except jsonschema.ValidationError as e:
        print("   ", e.message)
        return False


def conf_of(text):
    with tempfile.TemporaryDirectory() as tmp:
        p = Path(tmp) / "fm1.toml"
        p.write_text(text)
        return B.read_conf(p)


# the flash parts: what fm1_flash_ok.h accepts
h = (ROOT / "device" / "fm1_flash_ok.h").read_text()
want = sorted(f"{int(x, 16):06X}" for x in re.findall(r"0x([0-9A-Fa-f]{6})u", h))
ok(F.flash_ids() == ["856014", "B36014"] and sorted(F.flash_ids()) == want, "flash parts: fm1_flash_ok.h's (856014, B36014)")

# no Bluetooth, USB audio off, a key missing: left out, nothing invented
c = conf_of('name = "Quiet"\nusb_manufacturer = "QUIET-1"\nusb_audio_in = false\nlicence = "MPL-2.0"\ncredits = ""\n')
have = F.parts(c, ble=False, own=False, wifi=False, cpu2=False, touch=False, words=False, env={})
d = F.make(c, "quiet", F.display_name(c, ROOT), have)
ok(d["hardware"]["features"] == [], "no Bluetooth, USB audio off: no features")
ok(list(d) == ["schema", "id", "name", "license", "hardware"], "missing keys (and an empty credits) left out")
ok(d["name"] == "QUIET-1" and d["id"] == "quiet" and d["schema"] == 1, "name from usb_manufacturer, id from name")
ok(valid(d), "  validates against the schema" + (" (skipped: no jsonschema)" if jsonschema is None else ""))
c2 = conf_of('name = "Radio"\nusb_manufacturer = "fm1-radio"\ntitle = "Radio"\n')
ok(F.display_name(c2, ROOT) == "Radio", "name from title, before usb_manufacturer")
ok(refused(lambda: F.display_name({"title": ["Radio"]}, ROOT), "title"), "a title that is not a name refused")

# every part, and the tags; the keys in order
c = conf_of('''name = "Full"
usb_manufacturer = "FULL-1"
ble = true
ble_stack = "own"
ble_central = true
bt_audio = true
usb_host = true
msc = true
cpu2 = true
mirror = true
backup = true
resources = "res.bin"
author = "Someone Else"
licence = "MPL-2.0"
tagline = "Everything at once"
description = "A test firmware with every part, café included."
tags = ["synth", "sequencer",
        "experimental"]
install = "https://example.org/full/install/"
site = "https://example.org/full/"
screenshot = "docs/img/screen.png"
repo = "https://github.com/example/full"
credits = "Built on fm1-base."
''')
have = F.parts(c, ble=True, own=True, wifi=True, cpu2=True, touch=True, words=True, env={})
d = F.make(c, "full", F.display_name(c, ROOT), have)
ok(d["hardware"]["features"] == list(F.FEATURE_NAMES), "every part: every feature, in the vocabulary's order")
ok(list(d) == list(F.KEYS), "every key, in the file's order")
ok(d["tags"] == ["synth", "sequencer", "experimental"] and d["source"] == "https://github.com/example/full",
   "tags (a list over lines), source from repo")
text = F.dumps(d)
ok(text.endswith("}\n") and "café" in text and '\n  "id": "full",' in text, "2-space indent, UTF-8, a trailing newline")
ok('\n  "tags": ["synth", "sequencer", "experimental"],\n' in text and '\n    "models": ["FM-1"],\n' in text,
   "a list of strings on one line")
ok(json.loads(text) == d and json.loads(F.dumps({"a": [{"b": []}], "c": {}})) == {"a": [{"b": []}], "c": {}},
   "reads back as it was")
ok(valid(d), "  validates against the schema" + (" (skipped: no jsonschema)" if jsonschema is None else ""))

# the environment's overrides and the dependent parts, as the build reads them
have = F.parts(c, ble=False, own=True, wifi=False, cpu2=False, touch=False, words=False,
               env={"FM1B_MIRROR": "0", "FM1B_UAC": "0"})
ok(not have["bluetooth-midi"] and not have["bluetooth-midi-devices"] and not have["bluetooth-audio"],
   "--no-ble: no Bluetooth part")
ok(not have["screen-mirror"] and not have["usb-audio-in"] and not have["usb-audio-out"],
   "FM1B_MIRROR=0, FM1B_UAC=0: without (and no USB audio out without in)")
have = F.parts({"ble": True, "bt_audio": True}, ble=True, own=False, wifi=False, cpu2=False, touch=False, words=False,
               env={})
ok(not have["bluetooth-audio"] and have["usb-audio-in"] and have["usb-audio-out"],
   "bt_audio only on the own stack; USB audio on by default")

# refused
base = {"name": "X"}
ok(refused(lambda: F.make({**base, "tags": ["synth", "techno"]}, "x", "X", {}), "'techno' not in the vocabulary"),
   "an unknown tag refused, by name")
ok(refused(lambda: F.make({**base, "tags": ["synth", "synth"]}, "x", "X", {}), "twice"), "a tag twice refused")
ok(refused(lambda: F.make({**base, "description": "x" * 601}, "x", "X", {}), "at most 600"),
   "a description over 600 characters refused")
ok(not refused(lambda: F.make({**base, "description": "x" * 600}, "x", "X", {}), "600"),
   "one of 600 taken")
ok(refused(lambda: F.make({**base, "install": "http://example.org/"}, "x", "X", {}), "https"),
   "an install page not https refused")

# what a release needs: refused without, by name; any other build warned on one line
full = {k: "x" for k in F.REQUIRED} | {"tags": ["synth"], "screenshot": "docs/img/screen.png"}


def warned(conf):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        gone = F.require(conf, False)
    return gone, out.getvalue()


ok(F.REQUIRED == ("author", "licence", "tagline", "description", "tags", "install", "repo", "screenshot"),
   "the keys a release needs")
ok(F.missing(full) == [] and F.require(full, True) == [], "a release with all of them: taken")
ok(F.missing({**full, "author": "", "authors": ["A", "B"]}) == [], "  authors (the .rfm1's) for author")
lacking = {k: v for k, v in full.items() if k not in ("tags", "install")} | {"description": ""}
ok(F.missing(lacking) == ["description", "tags", "install"], "left out or empty: missing, in order")
ok(refused(lambda: F.require(lacking, True), "a release needs description, tags, install") and
   refused(lambda: F.require(lacking, True), "FIRMWARE-JSON.md"), "a release without them refused, by name and doc")
gone, said = warned(lacking)
ok(gone == ["description", "tags", "install"] and said.count("\n") == 1 and "warning:" in said and
   "description, tags, install" in said, "a dev build: one warning line naming them, and on")
ok(warned(full) == ([], ""), "a dev build with all of them: no warning")
ok(warned({"name": "X"})[0] == list(F.REQUIRED), "  an fm1.toml with none: every one named")
tmpl = B.read_conf(ROOT / "template" / "fm1.toml")
ok(F.missing(tmpl) == [], "the template's fm1.toml: everything a release needs")
ok(F.screenshot_problem(tmpl, ROOT / "template") is None, "  its screenshot passes the checks (template/)")
ok(" and ".join([", ".join(f"`{k}`" + (" (or `authors`)" if k == "author" else "") for k in F.REQUIRED[:-1]),
                 f"`{F.REQUIRED[-1]}`"]) + "." in DOC, "doc: the keys a release needs")

# the screenshot: a path in the repository; a PNG, square, at least 240 x 240, at most 2 MiB


def png(path, w, h, pad=0):
    """a PNG of w x h (grey; its header is all the build reads), padded with an ancillary chunk to grow it"""
    def chunk(t, data):
        return struct.pack(">I", len(data)) + t + data + struct.pack(">I", zlib.crc32(t + data))
    raw = b"".join(b"\0" + b"\x80" * w for _ in range(h))
    body = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + (chunk(b"teXt", b"pad\0" + b"x" * pad) if pad else b"") +
            chunk(b"IEND", b""))
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(body)


for bad in ("../shot.png", "docs/../shot.png", "/abs/shot.png", "docs\\shot.png", "docs/shot.jpg", "./shot.png",
            "docs//shot.png", "docs/shot png.png", "shot.png\n"):
    ok(refused(lambda: F.screenshot_path({"screenshot": bad}), "relative to the repository's root") and
       refused(lambda: F.make({"name": "X", "screenshot": bad}, "x", "X", {}), "screenshot"),
       f"  a screenshot path {bad!r} refused")
ok(F.screenshot_path({"screenshot": "docs/img/screen-1_a.png"}) == "docs/img/screen-1_a.png" and
   F.screenshot_path({}) is None, "a screenshot path in the repository taken; none: None")
with tempfile.TemporaryDirectory() as tmp:
    repo = Path(tmp) / "fm1-thing"
    app = repo / "app"
    app.mkdir(parents=True)
    ok(F.repo_root(app) == repo.resolve() and F.repo_root(ROOT / "template") == (ROOT / "template").resolve(),
       "the repository's root: app/'s parent; the template's own folder")
    png(repo / "docs" / "good.png", 240, 240)
    png(repo / "docs" / "wide.png", 480, 240)
    png(repo / "docs" / "small.png", 120, 120)
    png(repo / "docs" / "big.png", 256, 256, pad=2 * 1024 * 1024)
    (repo / "docs" / "fake.png").write_bytes(b"GIF89a" + b"\0" * 64)
    (repo / "docs" / "short.png").write_bytes(b"\x89PNG\r\n\x1a\n")
    ok(F.png_size(repo / "docs" / "wide.png") == (480, 240), "a PNG's size from its header")

    def shot(name):
        return {"name": "X", "screenshot": f"docs/{name}"}
    ok(F.screenshot_problem(shot("good.png"), app) is None, "a square PNG of 240 x 240 passes")
    for name, words in (("none.png", "no such file"), ("fake.png", "not a PNG"), ("short.png", "not a PNG"),
                        ("wide.png", "480 x 240, not square"), ("small.png", "smaller than 240 x 240"),
                        ("big.png", "at most 2097152")):
        ok(words in (F.screenshot_problem(shot(name), app) or ""), f"  {name}: {words}")
        ok(refused(lambda: F.require({**full, **shot(name)}, True, "fm1.toml", app), words) and
           refused(lambda: F.require({**full, **shot(name)}, True, "fm1.toml", app), "a release needs a screenshot"),
           f"    a release refused")
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            gone = F.require({**full, **shot(name)}, False, "fm1.toml", app)
        ok(gone == [] and out.getvalue().count("\n") == 1 and "warning: fm1.toml screenshot:" in out.getvalue() and
           words in out.getvalue(), "    a dev build: one warning line, and on")
        ok("screenshot" not in F.make(shot(name), "x", "X", {}, flash=[], app_dir=app), "    left out of the file")
    ok(F.require({**full, **shot("good.png")}, True, "fm1.toml", app) == [], "a release with a good one: taken")
    d = F.make({**full, **shot("good.png"), "install": "https://e.org/", "repo": "https://e.org/x"}, "x", "X", {},
               flash=[], app_dir=app)
    ok(d["screenshot"] == "docs/good.png" and list(d).index("screenshot") == list(d).index("source") + 1,
       "in the file as fm1.toml has it, after source")
    ok(valid(d), "  validates against the schema" + (" (skipped: no jsonschema)" if jsonschema is None else ""))
    no_shot = {k: v for k, v in full.items() if k != "screenshot"}
    ok(refused(lambda: F.require(no_shot, True, "fm1.toml", app), "a release needs screenshot"),
       "no screenshot: a release refused, by name")
    gone, said = warned(no_shot)
    ok(gone == ["screenshot"] and said.count("\n") == 1 and "has no screenshot" in said,
       "  a dev build: warned on one line")

# the build: --release refuses before anything is built; --host, --web and --rfm1 do not ask
with tempfile.TemporaryDirectory() as tmp:
    app = Path(tmp) / "app"
    app.mkdir()
    (app / "fm1.toml").write_text('name = "Bare"\nid_prefix = "FM-1_99"\nlicence = "MPL-2.0"\n')
    argv = sys.argv
    try:
        sys.argv = ["build.py", str(app), "--release", "1.0"]
        ok(refused(B.main, "a release needs author, tagline, description, tags, install, repo") and
           not (app / "build").exists(), "build.py --release: refused, nothing written")
    finally:
        sys.argv = argv

# where it is written
with tempfile.TemporaryDirectory() as tmp:
    repo = Path(tmp) / "fm1-thing"
    (repo / "app").mkdir(parents=True)
    t = F.targets(repo / "app")
    ok(t == [(repo / "app" / "build" / "firmware.json").resolve(), (repo / "firmware.json").resolve()],
       "an app in a firmware's repository: build/ and the repository's root")
    ok(F.targets(repo / "app", no_ble=True) == [(repo / "app" / "build" / "firmware-usb.json").resolve()],
       "--no-ble: build/firmware-usb.json only")
    ok(F.targets(ROOT / "template") == [ROOT / "template" / "build" / "firmware.json"],
       "the template: template/build/ only, never fm1-base's root")
    f = repo / "firmware.json"
    ok(F.write(f, "a\n") and not F.write(f, "a\n") and F.write(f, "b\n"), "written only when changed")

# the vocabularies: the code's, the schema's, the doc's
props = SCHEMA["properties"]
ok(tuple(props["tags"]["items"]["enum"]) == F.TAGS, "schema: the tag vocabulary")
ok(tuple(props["hardware"]["properties"]["features"]["items"]["enum"]) == F.FEATURE_NAMES,
   "schema: the feature vocabulary")
ok(props["screenshot"]["pattern"] == F.SCREENSHOT_RE, "schema: the screenshot's pattern")
if jsonschema is not None:
    with contextlib.redirect_stdout(io.StringIO()):          # (valid() prints why: expected here)
        bad_shots = [valid({"schema": 1, "id": "x", "name": "X", "screenshot": p}) for p in ("../x.png", "x.jpg")]
    ok(not any(bad_shots), "  schema: a bad screenshot path invalid")
ok(", ".join(F.TAGS) + "." in " ".join(DOC.split()), "doc: the tag vocabulary")
feats_doc = DOC[DOC.index("## Features"):DOC.index("## Rules")]
ok(re.findall(r"^\| `([a-z0-9-]+)` \|", feats_doc, re.M) == list(F.FEATURE_NAMES), "doc: the feature vocabulary")

print("all ok" if not failed else f"{failed} FAILED")
sys.exit(1 if failed else 0)
