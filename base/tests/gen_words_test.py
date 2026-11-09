#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
"""tools/gen_words.py: a words file made and its link written; the same words, the same file; one over the screen
reader's limits (more than WORDS_MAX entries with the common ones, a side longer than WORD_MAX characters) refused
with a clear message, one at the limits taken.
  python3 tests/gen_words_test.py"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "tools" / "gen_words.py"
sys.path.insert(0, str(ROOT / "tools"))
import gen_words as G  # noqa: E402

failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<72} {'ok' if cond else 'FAIL'}")
    failed += not cond


def run(tmp, lines):
    tsv = Path(tmp) / "words.tsv"
    tsv.write_text("# url: https://example.org/fw/words/\n" + "".join(f"{a}\t{b}\n" for a, b in lines))
    r = subprocess.run([sys.executable, str(GEN), str(tsv), str(Path(tmp) / "words"), str(Path(tmp) / "ann_words.h")],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


with tempfile.TemporaryDirectory() as tmp:
    code, out = run(tmp, [("SEL", "select"), ("BPM #", "# B P M")])
    files = list((Path(tmp) / "words").glob("*.json"))
    h = (Path(tmp) / "ann_words.h").read_text()
    ok(code == 0 and len(files) == 1 and f'"https://example.org/fw/words/{files[0].name}"' in h,
       "a words file made, its link in ann_words.h")
    w = json.loads(files[0].read_text())["words"]
    ok(w[:2] == [["SEL", "select"], ["BPM #", "# B P M"]] and len(w) == 2 + len(G.COMMON), "  its own words, then the common ones")
    run(tmp, [("SEL", "select"), ("BPM #", "# B P M")])
    ok(len(list((Path(tmp) / "words").glob("*.json"))) == 1, "  the same words: the same file")

with tempfile.TemporaryDirectory() as tmp:
    code, out = run(tmp, [("A" * G.WORD_MAX, "b" * G.WORD_MAX)])
    ok(code == 0, f"a side of {G.WORD_MAX} characters: taken")
    code, out = run(tmp, [("SEL", "select"), ("A" * (G.WORD_MAX + 1), "a")])
    ok(code != 0 and "words.tsv:3:" in out and "the screen's text is 121 characters" in out,
       f"the screen's text over {G.WORD_MAX} characters: refused, the line named")
    code, out = run(tmp, [("A", "b" * (G.WORD_MAX + 1))])
    ok(code != 0 and "what is said is 121 characters" in out, f"what is said over {G.WORD_MAX}: refused")

with tempfile.TemporaryDirectory() as tmp:
    n = G.WORDS_MAX - len(G.COMMON)
    code, out = run(tmp, [(f"W{i}", f"w {i}") for i in range(n)])
    ok(code == 0, f"{n} entries of its own ({G.WORDS_MAX} with the common ones): taken")
    code, out = run(tmp, [(f"W{i}", f"w {i}") for i in range(n + 1)])
    ok(code != 0 and f"{n + 1} entries" in out and f"{G.WORDS_MAX} at most" in out,
       "  one more: refused")

print("FAILED" if failed else "ok")
sys.exit(1 if failed else 0)
