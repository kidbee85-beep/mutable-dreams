#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""firmware.json: a firmware's description for firmware hubs (The Firmware Hub, RemoteFM1, M-Flasher), written by
tools/build.py from fm1.toml and the parts the build turns on. docs/FIRMWARE-JSON.md is the format,
docs/firmware.schema.json its JSON Schema; the tag and feature vocabularies are here, and only here (the test checks
the schema and the doc against them)."""
import json
import os
import re
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]            # fm1-base
SCHEMA = 1
DESCRIPTION_MAX = 600

# the screenshot: a square PNG in the firmware's repository, at least the FM-1's screen (240 x 240), at most 2 MiB;
# its path relative to the repository's root, / between folders, no "." or ".." (the schema has the same pattern)
SCREENSHOT_RE = r"^(?!(.*/)?\.\.?(/|$))[A-Za-z0-9._-]+(/[A-Za-z0-9._-]+)*\.png$"
SCREENSHOT_MIN = 240
SCREENSHOT_MAX = 2 * 1024 * 1024

# what a firmware is: fm1.toml tags, from this list only
TAGS = ("synth", "drum-machine", "groovebox", "sequencer", "sampler", "controller", "midi", "chords", "arranger",
        "ambient", "generative", "fm", "game", "utility", "general-midi", "experimental")

# what a build has: (feature, the build's words for it), in the order the file lists them
FEATURES = (
    ("bluetooth-midi", "Bluetooth MIDI"),                 # ble
    ("bluetooth-midi-devices", "Bluetooth MIDI devices"),  # ble_central
    ("bluetooth-audio", "Bluetooth audio"),               # bt_audio (ble_stack = "own")
    ("usb-audio-in", "USB audio in"),                     # usb_audio_in (the default: on)
    ("usb-audio-out", "USB audio out"),                   # usb_audio_out (the default: on, with usb_audio_in)
    ("usb-midi-host", "USB MIDI host"),                   # usb_host
    ("usb-disk", "USB disk mode"),                        # msc
    ("wifi", "WiFi"),                                     # wifi
    ("second-core", "the second core"),                   # cpu2
    ("velocity", "velocity"),                             # APP_TOUCH in app.h (lib/touch)
    ("pressure", "pressure"),                             # the same
    ("screen-reader", "the screen reader"),               # words.tsv with its "# url:" line (fm1-reader)
    ("screen-mirror", "screen mirroring"),                # mirror
    ("backup", "backup and restore"),                     # backup
    ("resources", "a resource region"),                   # resources
)
FEATURE_NAMES = tuple(f for f, _ in FEATURES)

# the models a firmware on fm1-base runs on
MODELS = ("FM-1",)

# the fm1.toml keys a release build needs (a dev build only warns without them); author may be the .rfm1's authors
REQUIRED = ("author", "licence", "tagline", "description", "tags", "install", "repo", "screenshot")

# the order of the file's keys
KEYS = ("schema", "id", "name", "author", "license", "tagline", "description", "tags", "install", "site", "source",
        "screenshot", "hardware", "credits")


def flash_ids(header=SRC / "device" / "fm1_flash_ok.h"):
    """the flash parts fm1-base's storage accepts (FM1B_FLASH_JEDEC_OK), as upper-case hex: "856014" """
    m = re.search(r"#define\s+FM1B_FLASH_JEDEC_OK\(id\)(.*)", Path(header).read_text())
    if not m:
        raise SystemExit(f"firmware.json: no FM1B_FLASH_JEDEC_OK in {header}")
    return [f"{int(h, 16):06X}" for h in re.findall(r"0x([0-9A-Fa-f]+)u?", m[1])]


def parts(conf, ble, own, wifi, cpu2, touch, words, env=None):
    """{feature: built in}, the build's parts (tools/build.py prints them and the file lists them). ble, own, wifi and
    cpu2 as the build has decided them (--no-ble, FM1B_CPU2); the others from fm1.toml and the environment as the
    build reads them"""
    env = os.environ if env is None else env

    def on(key, flag, default=False):                 # FM1B_X=0 / 1 in the environment: without / with
        v = env.get(flag)
        return v == "1" if v in ("0", "1") else bool(conf.get(key, default))

    usb_in = on("usb_audio_in", "FM1B_UAC", True)
    have = {
        "bluetooth-midi": bool(ble),
        "bluetooth-midi-devices": bool(ble) and bool(conf.get("ble_central")),
        "bluetooth-audio": bool(ble) and bool(own) and bool(conf.get("bt_audio")),
        "usb-audio-in": usb_in,
        "usb-audio-out": usb_in and on("usb_audio_out", "FM1B_UAC_OUT", True),
        "usb-midi-host": on("usb_host", "FM1B_USB_HOST"),
        "usb-disk": on("msc", "FM1B_MSC"),
        "wifi": bool(wifi),
        "second-core": bool(cpu2),
        "velocity": bool(touch),
        "pressure": bool(touch),
        "screen-reader": bool(words),
        "screen-mirror": on("mirror", "FM1B_MIRROR"),
        "backup": on("backup", "FM1B_BACKUP"),
        "resources": on("resources", "FM1B_RES"),
    }
    return {f: have[f] for f in FEATURE_NAMES}


def words_published(path):
    """a words file that says where its words are published ("# url: https://..."): the screen reader says them"""
    p = Path(path)
    return p.is_file() and any(re.match(r"# url:\s*\S", line) for line in p.read_text(encoding="utf-8").splitlines())


def display_name(conf, app_dir):
    """the product's name as people see it: fm1.toml title, else usb_manufacturer (the name the computer shows:
    "CTL-1"), else app.h's APP_NAME, else fm1.toml name"""
    if conf.get("title"):
        if not isinstance(conf["title"], str) or "\n" in conf["title"]:
            raise SystemExit("fm1.toml title: a name on one line")
        return conf["title"]
    if conf.get("usb_manufacturer"):
        return conf["usb_manufacturer"]
    for h in sorted(Path(app_dir).glob("*.h")):
        m = re.search(r'^#define APP_NAME "([^"]*)"', h.read_text(errors="ignore"), re.M)
        if m:
            return m[1]
    return conf.get("name", Path(app_dir).name)


def missing(conf):
    """the REQUIRED keys fm1.toml leaves out or leaves empty, in REQUIRED's order"""
    have = {k for k in REQUIRED if conf.get(k) not in (None, "", [])}
    if conf.get("authors"):
        have.add("author")
    return [k for k in REQUIRED if k not in have]


def require(conf, release, toml="fm1.toml", app_dir=None):
    """a release build (release true) refuses an fm1.toml without the REQUIRED keys, or (given app_dir) with a
    screenshot that fails screenshot_problem; any other build prints one warning line for each and goes on. Returns
    the missing keys"""
    gone = missing(conf)
    if gone and release:
        raise SystemExit(f"{toml}: a release needs {', '.join(gone)} (firmware.json, for firmware hubs: "
                         f"fm1-base's docs/FIRMWARE-JSON.md says what each is)")
    if gone:
        print(f"warning: {toml} has no {', '.join(gone)}: firmware.json goes without them, and a release build "
              f"refuses (docs/FIRMWARE-JSON.md)")
    bad = screenshot_problem(conf, app_dir) if app_dir is not None else None
    if bad and release:
        raise SystemExit(f"{toml}: a release needs a screenshot that is a square PNG of at least {SCREENSHOT_MIN} x "
                         f"{SCREENSHOT_MIN}, at most 2 MiB: {bad} (firmware.json, for firmware hubs: fm1-base's "
                         f"docs/FIRMWARE-JSON.md)")
    if bad:
        print(f"warning: {toml} screenshot: {bad}: firmware.json goes without it, and a release build refuses "
              f"(docs/FIRMWARE-JSON.md)")
    return gone


def repo_root(app_dir, src=SRC):
    """the firmware's repository: the parent of an APP_DIR named app (fm1-ctl/app), else APP_DIR itself (fm1-base's
    template)"""
    app = Path(app_dir).resolve()
    return app.parent if app.name == "app" and app.parent != Path(src).resolve() else app


def screenshot_path(conf):
    """fm1.toml screenshot, None when left out; refuses one that is not a path in the repository ending .png"""
    v = conf.get("screenshot")
    if v is None or v == "":
        return None
    if not isinstance(v, str) or "\n" in v or not re.match(SCREENSHOT_RE, v):
        raise SystemExit(f"fm1.toml screenshot {v!r}: a path relative to the repository's root, with / between the "
                         f"folders and no '..', of letters, digits, '.', '_' and '-', ending .png "
                         f'(screenshot = "docs/img/screen.png")')
    return v


def png_size(path):
    """(width, height) from a PNG's header (its signature and IHDR), None when the file is not a PNG"""
    with open(path, "rb") as f:
        head = f.read(24)
    if len(head) < 24 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    return int.from_bytes(head[16:20], "big"), int.from_bytes(head[20:24], "big")


def screenshot_problem(conf, app_dir, src=SRC):
    """what is wrong with fm1.toml's screenshot, or None (also None when there is none: missing names that): it must
    be a file in the repository (repo_root), a PNG, square, at least SCREENSHOT_MIN on a side, at most
    SCREENSHOT_MAX bytes"""
    rel = screenshot_path(conf)
    if rel is None:
        return None
    f = repo_root(app_dir, src) / rel
    if not f.is_file():
        return f"{rel}: no such file in {repo_root(app_dir, src)}"
    size = f.stat().st_size
    if size > SCREENSHOT_MAX:
        return f"{rel}: {size} bytes, at most {SCREENSHOT_MAX} (2 MiB)"
    wh = png_size(f)
    if wh is None:
        return f"{rel}: not a PNG"
    w, h = wh
    if w != h:
        return f"{rel}: {w} x {h}, not square"
    if w < SCREENSHOT_MIN:
        return f"{rel}: {w} x {h}, smaller than {SCREENSHOT_MIN} x {SCREENSHOT_MIN}"
    return None


def _url(conf, key):
    v = conf[key]
    if not isinstance(v, str) or not re.fullmatch(r"https://[^\s\"<>]+", v):
        raise SystemExit(f"fm1.toml {key} {v!r}: an https:// address")
    return v


def make(conf, fid, name, have, flash=None, app_dir=None):
    """the file's content: conf (fm1.toml), fid (the build's NAME), name (display_name), have (parts). A key fm1.toml
    leaves out (or leaves empty) is left out, and so is a screenshot that fails screenshot_problem (given app_dir).
    Refuses a tag outside TAGS, a description that is not one paragraph of at most DESCRIPTION_MAX characters and a
    screenshot that is not a path in the repository"""
    d = {"schema": SCHEMA, "id": fid, "name": name}
    if not conf.get("author") and conf.get("authors"):     # the .rfm1's authors = ["..."], when there is no author
        conf = {**conf, "author": ", ".join(conf["authors"])}
    for out, key in (("author", "author"), ("license", "licence"), ("tagline", "tagline"),
                     ("description", "description"), ("source", "repo"), ("credits", "credits")):
        v = conf.get(key)
        if v is None or v == "":
            continue
        if not isinstance(v, str):
            raise SystemExit(f"fm1.toml {key}: a string")
        d[out] = v
    desc = d.get("description")
    if desc is not None:
        if len(desc) > DESCRIPTION_MAX:
            raise SystemExit(f"fm1.toml description: {len(desc)} characters, at most {DESCRIPTION_MAX}")
        if "\n" in desc:
            raise SystemExit("fm1.toml description: one paragraph")
    if conf.get("tags"):
        tags = conf["tags"]
        if not isinstance(tags, list):
            raise SystemExit('fm1.toml tags: a list, tags = ["synth", "sequencer"]')
        bad = [t for t in tags if t not in TAGS]
        if bad:
            raise SystemExit(f"fm1.toml tags: {', '.join(map(repr, bad))} not in the vocabulary "
                             f"({', '.join(TAGS)}; docs/FIRMWARE-JSON.md)")
        if len(set(tags)) != len(tags):
            raise SystemExit("fm1.toml tags: a tag twice")
        d["tags"] = list(tags)
    for key in ("install", "site"):
        if conf.get(key):
            d[key] = _url(conf, key)
    if d.get("source"):
        d["source"] = _url({"repo": d["source"]}, "repo")
    shot = screenshot_path(conf)
    if shot and (app_dir is None or screenshot_problem(conf, app_dir) is None):
        d["screenshot"] = shot
    d["hardware"] = {"models": list(MODELS), "flash": flash_ids() if flash is None else list(flash),
                     "features": [f for f in FEATURE_NAMES if have.get(f)]}
    return {k: d[k] for k in KEYS if k in d}


def dumps(d, indent=""):
    """2-space indent, a list of strings or numbers on one line (as in the doc), UTF-8 as it is, a trailing newline"""
    def val(v, ind):
        if isinstance(v, dict):
            if not v:
                return "{}"
            inner = ind + "  "
            return "{\n" + ",\n".join(f"{inner}{json.dumps(k, ensure_ascii=False)}: {val(x, inner)}"
                                       for k, x in v.items()) + f"\n{ind}}}"
        if isinstance(v, list) and not any(isinstance(x, (dict, list)) for x in v):
            return "[" + ", ".join(json.dumps(x, ensure_ascii=False) for x in v) + "]"
        if isinstance(v, list):
            inner = ind + "  "
            return "[\n" + ",\n".join(inner + val(x, inner) for x in v) + f"\n{ind}]"
        return json.dumps(v, ensure_ascii=False)
    return val(d, indent) + "\n"


def targets(app_dir, no_ble=False, src=SRC):
    """where a build writes it: APP_DIR/build/firmware.json (firmware-usb.json for a --no-ble build), and for an app
    in a firmware's repository (APP_DIR named app, as fm1-ctl/app) the repository's firmware.json too, as fm1.toml
    has it (not from a --no-ble build). Never into fm1-base itself (its template)"""
    app = Path(app_dir).resolve()
    out = [app / "build" / ("firmware-usb.json" if no_ble else "firmware.json")]
    if app.name == "app" and not no_ble and app.parent != Path(src).resolve():
        out.append(app.parent / "firmware.json")
    return out


def write(path, text):
    """write text unless the file has it already (no new mtime for nothing); True when written"""
    p = Path(path)
    if p.is_file() and p.read_text(encoding="utf-8") == text:
        return False
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding="utf-8")
    return True
