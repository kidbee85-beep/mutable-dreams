#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""The installer page's script as tools/make_site.py builds it (fm1ports.js, fm1pkg.js and fm1ota.js inlined into one
module, the package's metadata after them) parses: two top-level names alike in those files (fm1pkg.js's crc16 and
the fast install's, 2026-11-05) stop the whole page with "already declared", and nothing else tests the inlined page.
Needs node (skipped without it)."""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import make_site  # noqa: E402

if not shutil.which("node"):
    print("site script: skipped (no node)")
    sys.exit(0)
html = (ROOT / "web" / "installer.html").read_text(encoding="utf-8")
lib = "\n".join(make_site.strip_module((ROOT / "web" / f).read_text(encoding="utf-8"))
                for f in ("fm1ports.js", "fm1pkg.js", "fm1ota.js"))
page = html.replace("/*LIB*/", lib).replace("/*META*/", '{"version": "0.1", "product": "FM-1_1100"}')
page = re.sub(r"/\*[A-Z_]+\*/", "null", page)   # make_site's other placeholders (/*APP*/, ...): a value each
scripts = re.findall(r'<script type="module">(.*?)</script>', page, re.S)
assert scripts, "installer.html: no module script"
fails = 0
with tempfile.TemporaryDirectory() as d:
    for i, s in enumerate(scripts):
        f = pathlib.Path(d) / f"page{i}.mjs"
        f.write_text(s, encoding="utf-8")
        r = subprocess.run(["node", "--check", str(f)], capture_output=True, text=True)
        if r.returncode:
            fails += 1
            print(f"installer.html script {i}: does not parse:\n{r.stderr.strip()[:600]}")
print("site script: the installer page's inlined script parses" if not fails else "site script: FAILED")
sys.exit(1 if fails else 0)
