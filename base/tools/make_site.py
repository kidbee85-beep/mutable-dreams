#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal (from fm1-ctl and fm1-fomni)
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (product_of, strip_module: Felucca's)
"""Make a firmware's site (GitHub Pages) from its app directory and a release package:

  index.html                  the front page (APP_DIR/site/landing.md, with {{VERSION}} and {{PKG}} filled in; its
                              images from APP_DIR/site/img)
  install/index.html          the web installer (web/installer.html, with fm1ports.js, fm1pkg.js, fm1ota.js and the
                              package's metadata inlined; Chrome or Edge, Web MIDI)
  manual/index.html, img/     the manual (APP_DIR/docs/MANUAL.md, its images from APP_DIR/docs/img), if there is one
  emu/                        the firmware in the browser (APP_DIR/build/emu: tools/build.py APP_DIR --web)
  words/                      the screen reader's words files (APP_DIR/web/words), every one ever made
  mirror/index.html           the screen mirror (web/mirror/index.html, with fm1ports.js inlined), with mirror = true
  backup/index.html           backup and restore in the browser (web/backup/index.html, with fm1ports.js inlined), with
                              backup = true: its magic and version from APP_DIR/app.h (APP_BACKUP_MAGIC, APP_BACKUP_VERSION)
  firmware/NAME-VERSION.fwsc  the package the installer writes; also the download
  firmware/NAME-VERSION-res.bin  the resource region (fm1.toml resources), when PKG_STEM-res.bin is beside the
                              package (tools/build.py): the installer writes it after the firmware
  firmware/m-bootloader-X.Y.fwsc  m-bootloader (MIT), which the installer offers after the firmware

  tools/make_site.py APP_DIR APP_DIR/build/NAME-VERSION.fwsc VERSION OUT_DIR

The package must be a release build (tools/build.py APP_DIR --release VERSION): its identity, fm1.toml's id_prefix and
six digits, is what the installer checks the download against and what the FM-1 reports after the install. From
fm1.toml: name, tagline, repo (the source's URL), licence, credits (a sentence for the installer's footer); with
backup = true, backup_ext (the files' extension, default "bak") and backup_holds (what a backup holds, a phrase for
the page: "the songs and the settings"). tool_pages(APP_DIR) gives those two pages filled in, for a firmware's own
site tool."""
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
MBOOT_VERSION = "1.2"                             # web/m-bootloader/fm1-uboot-X.Y.fwsc: a new release is a new file and this
sys.path.insert(0, str(SRC / "tools"))
from build import read_conf  # noqa: E402

BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    """an ES module's text as plain script: no export, no import (to inline it in a page)"""
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def ports_js():
    """web/fm1ports.js (finding FM-1s on Web MIDI) as plain script, for a page's {{PORTS_JS}}"""
    return strip_module((SRC / "web" / "fm1ports.js").read_text(encoding="utf-8"))


def panel_js():
    """web/fm1panel.js (the panel's drawing and lights) as plain script, for a page's {{PANEL_JS}}"""
    return strip_module((SRC / "web" / "fm1panel.js").read_text(encoding="utf-8"))


def fill(html, values, what):
    """a page's {{KEY}}s filled in; every one must be"""
    for key, val in values.items():
        html = html.replace("{{" + key + "}}", val)
    left = sorted(set(re.findall(r"\{\{[A-Z_]+\}\}", html)))
    if left:
        raise SystemExit(f"{what}: {' '.join(left)} not filled in")
    return html


def backup_values(app, conf):
    """the backup page's values: the app's magic and version (app.h), the name, the identity, the files"""
    src = (Path(app) / "app.h").read_text(encoding="utf-8")
    m = re.search(r"#define\s+APP_BACKUP_MAGIC\s+0x([0-9A-Fa-f]{8})u?", src)
    v = re.search(r"#define\s+APP_BACKUP_VERSION\s+(\d+)u?", src)
    if not m or not v:
        raise SystemExit(f"{app}/app.h: backup = true needs APP_BACKUP_MAGIC 0x........u and APP_BACKUP_VERSION N")
    magic = int(m.group(1), 16).to_bytes(4, "little").decode("ascii")
    name = conf.get("name", Path(app).name)
    return {"NAME": name, "ID": conf.get("id_prefix", ""), "MAGIC": magic, "BACKUP_VERSION": v.group(1),
            "EXT": conf.get("backup_ext", "bak"), "HOLDS": conf.get("backup_holds", f"everything {name} saves"),
            "EXTRA": "", "EXTRAS_JS": "const EXTRAS = [];",
            "TERMINAL": "You can also back up from a terminal. Run <code>python3 tools/fm1_backup.py</code> in fm1-base's "
                        "repository. It does the same, and makes the same files."}


def tool_pages(app, conf=None, values=None):
    """{"mirror/index.html": html, "backup/index.html": html}, as fm1.toml asks (mirror, backup), filled in; values:
    the firmware's own, over the defaults (EXTRA: its notes; EXTRAS_JS: its own sections, web/backup/index.html)"""
    app = Path(app).resolve()
    conf = conf or read_conf(app / "fm1.toml")
    out = {}
    if conf.get("mirror"):
        out["mirror/index.html"] = fill((SRC / "web" / "mirror" / "index.html").read_text(encoding="utf-8"),
                                        dict({"NAME": conf.get("name", app.name), "PORTS_JS": ports_js(), "PANEL_JS": panel_js()},
                                             **(values or {})),
                                        "web/mirror")
    if conf.get("backup"):
        out["backup/index.html"] = fill((SRC / "web" / "backup" / "index.html").read_text(encoding="utf-8"),
                                        dict(backup_values(app, conf), PORTS_JS=ports_js(), **(values or {})),
                                        "web/backup")
    return out


def page(md, dst, conf, home):
    args = [sys.executable, str(SRC / "tools" / "manual_html.py"), str(md), str(dst), "--name", conf["name"],
            "--repo", conf.get("repo", ""), "--tagline", conf.get("tagline", "")]
    subprocess.run(args + (["--home"] if home else []), check=True, capture_output=True)


def main(app, pkg, version, out):
    app, pkg, out = Path(app).resolve(), Path(pkg), Path(out)
    conf = read_conf(app / "fm1.toml")
    conf.setdefault("name", app.name)
    file_name = conf["name"].lower()
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(re.escape(conf.get("id_prefix", "FM-1_0")) + r"\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (tools/build.py {app} --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    emu = app / "build" / "emu"
    if not (emu / "app.wasm").exists():
        raise SystemExit(f"no {emu}/app.wasm: run tools/build.py {app} --web (needs Emscripten)")
    name = f"{file_name}-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "firmware"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    # the resource region beside the package (tools/build.py: fm1.toml resources), checked as the FM-1 checks it
    res_src, res_name = pkg.with_name(pkg.stem + "-res.bin"), Path(name).stem + "-res.bin"
    if res_src.exists():
        from fm1_resources import ResError, payload_of
        try:
            payload_of(res_src.read_bytes())
        except ResError as e:
            raise SystemExit(f"{res_src}: {e}")
        shutil.copy(res_src, out / "firmware" / res_name)
    # m-bootloader (MIT), which the installer offers to install after the firmware, with its licence
    boot = SRC / "web" / "m-bootloader" / f"fm1-uboot-{MBOOT_VERSION}.fwsc"
    boot_name = f"m-bootloader-{MBOOT_VERSION}.fwsc"
    shutil.copy(boot, out / "firmware" / boot_name)
    shutil.copy(SRC / "web" / "m-bootloader" / "LICENSE", out / "firmware" / "m-bootloader-LICENSE.txt")
    for lic in sorted((SRC / "LICENSES").glob("*.txt")):   # the packages' parts under other licences (the JieLi SDK
        shutil.copy(lic, out / "firmware" / lic.name)      # files, an own-stack build's Apache / BSD parts): beside them
    # the installer
    html = (SRC / "web" / "installer.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((SRC / "web" / f).read_text(encoding="utf-8"))
                    for f in ("fm1ports.js", "fm1pkg.js", "fm1ota.js"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name,
                       "backup": bool(conf.get("backup")),
                       "res": "../firmware/" + res_name if res_src.exists() else None,
                       "boot": {"pkg": "../firmware/" + boot_name, "version": MBOOT_VERSION,
                                "sha256": hashlib.sha256(boot.read_bytes()).hexdigest()}})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"web/installer.html must contain {mark} once")
    repo = conf.get("repo", "")
    credits = conf.get("credits", "")
    for key, val in (("RES", conf.get("resources_what", "{{NAME}}'s data")),   # (fm1.toml: what the resource
                     ("RES_JA", conf.get("resources_what_ja", "{{NAME}} のデータ")),   # region holds, in the page's words)
                     ("NAME", conf["name"]), ("REPO", repo), ("REPO_SHORT", re.sub(r"^https?://", "", repo)),
                     ("LICENSE", conf.get("licence", "MPL-2.0")), ("CREDITS", " " + credits if credits else "")):
        html = html.replace("{{" + key + "}}", val)
    # the firmware's own steps around an install (fm1.toml install_app = "web/install_app.js": an expression, an
    # object with before / after; web/installer.html APP), or none
    app_js = (app / conf["install_app"]).read_text(encoding="utf-8") if conf.get("install_app") else "null"
    (out / "install" / "index.html").write_text(html.replace("/*LIB*/", lib).replace("/*META*/", meta)
                                                .replace("/*APP*/", app_js), encoding="utf-8")
    # the front page and the manual (complete pages, their images beside them)
    landing = app / "site" / "landing.md"
    if not landing.exists():
        raise SystemExit(f"no {landing}: the site's front page")
    filled = out / "_landing.md"                    # the version and the package's file name, known only now
    filled.write_text(landing.read_text(encoding="utf-8").replace("{{VERSION}}", version).replace("{{PKG}}", name),
                      encoding="utf-8")
    page(filled, out / "index.html", conf, True)
    filled.unlink()
    if (app / "site" / "img").is_dir():
        shutil.copytree(app / "site" / "img", out / "img")
    manual = app / "docs" / "MANUAL.md"
    if manual.exists():
        (out / "manual").mkdir()
        page(manual, out / "manual" / "index.html", conf, False)
        if (app / "docs" / "img").is_dir():
            shutil.copytree(app / "docs" / "img", out / "manual" / "img")
    shutil.copytree(emu, out / "emu")
    if (app / "web" / "words").is_dir():
        shutil.copytree(app / "web" / "words", out / "words")
    tools = tool_pages(app, conf)
    for p, h in tools.items():
        (out / p).parent.mkdir(parents=True, exist_ok=True)
        (out / p).write_text(h, encoding="utf-8")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, install/ ({product}), {'manual/, ' if manual.exists() else ''}emu/, words/, "
          f"{''.join(p.split('/')[0] + '/, ' for p in tools)}firmware/{name} ({len(raw)} B)"
          f"{f', firmware/{res_name}' if res_src.exists() else ''}")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:5])
