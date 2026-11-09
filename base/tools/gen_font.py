#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Render the bitmap fonts to a C header (assets/fonts/):
  T / XS / S / B / M / L: Terminus 12 (T: digits and capitals), 12, 16, 16 bold, 24 bold, 32 bold (BDF, SIL OFL 1.1); see SIZES. TTF fonts also work through render()
(anti-aliased, tabular figures via the OpenType `tnum` feature).

Glyph format: per glyph an advance width, a bitmap width and an offset; the
bitmap starts FONT_PAD pixels left of the pen position (room for side
bearings); rows top to bottom, 2 pixels per byte (high nibble first),
alpha 0..15. With FM1_FONT_TRIM=1 (fm1.toml font_trim) a glyph is only its lit box: its left
column, top row and row count (a byte each), then those rows, the bitmap width the box's. With FM1_FONT_PACK=1 too
(fm1.toml font_pack) those rows' alphas are Huffman-coded (see PACK below).
"""
import heapq
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONTS = Path(__file__).resolve().parents[1] / "assets" / "fonts"
# (name, file, px, scale, pixel, first, last): pixel fonts are rendered without
# anti-aliasing at their design size and enlarged by an integer factor
# X0X: six roles, T (footers), XS (grid labels, units, hints), S (body), B (headers, names, the selected
# row), M (knob values) and L (the big readout). Each set gives them a face and a pixel size,
# chosen so the line height stays near the layout's 12 / 16 / 24 / 32. X0X_FONTSET picks one.
# A TTF entry "file.ttf@wght=600" sets a variable font's axis.
FONTSETS = {
    "dreams": [("T", "BarlowSemiCondensed-Bold.ttf", 11, 1, False, 32, 95),
               ("XS", "BarlowSemiCondensed-Bold.ttf", 12, 1, False, 32, 126),
               ("H", "BarlowSemiCondensed-Bold.ttf", 14, 1, False, 32, 126),
               ("S", "Nunito[wght].ttf@Weight=650", 14, 1, False, 32, 126),
               ("B", "Nunito[wght].ttf@Weight=800", 16, 1, False, 32, 126),
               ("M", "Nunito[wght].ttf@Weight=700", 22, 1, False, 32, 95),
               ("L", "Nunito[wght].ttf@Weight=750", 30, 1, False, 32, 95)],
    "terminus": [("T", "ter-u12n.bdf", 12, 1, True, 32, 95),
                 ("XS", "ter-u12n.bdf", 12, 1, True, 32, 126),
                 ("S", "ter-u16n.bdf", 16, 1, True, 32, 126),
                 ("B", "ter-u16b.bdf", 16, 1, True, 32, 126),
                 ("M", "ter-u24b.bdf", 24, 1, True, 32, 95),
                 ("L", "ter-u32b.bdf", 32, 1, True, 32, 95)],
    "barlow": [("T", "BarlowSemiCondensed-SemiBold.ttf", 10, 1, False, 32, 95),   # footers (from X0X)
               ("XS", "BarlowSemiCondensed-SemiBold.ttf", 12, 1, False, 32, 126),
               ("S", "BarlowSemiCondensed-Medium.ttf", 15, 1, False, 32, 126),
               ("B", "BarlowSemiCondensed-Bold.ttf", 15, 1, False, 32, 126),
               ("M", "BarlowSemiCondensed-SemiBold.ttf", 23, 1, False, 32, 95),
               ("L", "BarlowSemiCondensed-Bold.ttf", 31, 1, False, 32, 95)],    # capitals and digits, as the others
                                                                                  # (the splash, the panel: 14 KB less)
    "inter": [("T", "Inter[opsz,wght].ttf@wght=600", 9, 1, False, 32, 95),
              ("XS", "Inter[opsz,wght].ttf@wght=600", 11, 1, False, 32, 126),
              ("S", "Inter[opsz,wght].ttf@wght=500", 14, 1, False, 32, 126),
              ("B", "Inter[opsz,wght].ttf@wght=700", 14, 1, False, 32, 126),
              ("M", "Inter[opsz,wght].ttf@wght=650", 21, 1, False, 32, 95),
              ("L", "Inter[opsz,wght].ttf@wght=700", 29, 1, False, 32, 95)],
    "rajdhani": [("T", "Rajdhani-SemiBold.ttf", 12, 1, False, 32, 95),
                 ("XS", "Rajdhani-Bold.ttf", 14, 1, False, 32, 126),
                 ("S", "Rajdhani-SemiBold.ttf", 17, 1, False, 32, 126),
                 ("B", "Rajdhani-Bold.ttf", 17, 1, False, 32, 126),
                 ("M", "Rajdhani-Bold.ttf", 26, 1, False, 32, 95),
                 ("L", "Rajdhani-Bold.ttf", 35, 1, False, 32, 95)],
    "chakra": [("XS", "ChakraPetch-SemiBold.ttf", 11, 1, False, 32, 126),
               ("S", "ChakraPetch-Medium.ttf", 14, 1, False, 32, 126),
               ("B", "ChakraPetch-Bold.ttf", 14, 1, False, 32, 126),
               ("M", "ChakraPetch-SemiBold.ttf", 21, 1, False, 32, 95),
               ("L", "ChakraPetch-Bold.ttf", 29, 1, False, 32, 95)],
}
import os  # noqa: E402
FONTSET = os.environ.get("X0X_FONTSET", "barlow")    # the others need X0X_FONT_DIRS (not in assets/)
FONTDIRS = [Path(p) for p in os.environ.get("X0X_FONT_DIRS", "").split(":") if p]
SIZES = FONTSETS[FONTSET]
# FM1_FONT_FULL="L M": those roles with every printable character (lower case too), for an app that writes words in
# them (fm1.toml font_full; the others keep the set's range)
FULL = set(os.environ.get("FM1_FONT_FULL", "").split())
# FM1_FONT_TRIM=1: each glyph only its lit box (fm1.toml font_trim): the box's left column, top row and row count
# first (3 bytes), then its rows; BW the box's width. device/gfx.c draws it so (FONT_TRIM, in the header)
TRIM = os.environ.get("FM1_FONT_TRIM") == "1"
# FM1_FONT_PACK=1 (with FM1_FONT_TRIM; fm1.toml font_pack): each trimmed glyph's alphas coded, w a row, its rows in
# turn, by a canonical Huffman code of three: the one for the alpha after a 0, after a 15, after anything else (a
# glyph starts after a 0), its bits from the high one down, the glyph padded to a byte. The face's data starts with the
# three codes (HUF_CODES, FONT_HUF_SZ bytes each: the count of codes of each length 1..HUF_LEN, then the alphas in code
# order); the offsets count from the data's start, as before. device/gfx.c decodes it as it draws (FONT_PACK)
PACK = os.environ.get("FM1_FONT_PACK") == "1"
if PACK and not TRIM:
    raise SystemExit("gen_font: FM1_FONT_PACK needs FM1_FONT_TRIM (fm1.toml font_pack needs font_trim)")
HUF_LEN = 12                    # the longest code (a longer one: the counts halved until none is)
HUF_CODES = 3
SIZES = [(n, f, px, sc, pix, first, 126 if n in FULL else last) for n, f, px, sc, pix, first, last in SIZES]
PAD = 2


def upscale(px_, w, h, scale):
    """integer nearest-neighbour enlargement of a w x h glyph"""
    if scale == 1:
        return px_
    big = []
    for y in range(h):
        row = []
        for x in range(w):
            row += [px_[y * w + x]] * scale
        big += row * scale
    return big


def render_bdf(path, scale, first, last):
    """BDF bitmap font (Terminus 8x16): exact pixels, cell = the font bounding box."""
    lines = path.read_text(errors="replace").splitlines()
    fbb = next(l for l in lines if l.startswith("FONTBOUNDINGBOX")).split()
    cw, ch, fx, fy = int(fbb[1]), int(fbb[2]), int(fbb[3]), int(fbb[4])
    base = ch + fy                                     # rows above the baseline
    glyphs, i = {}, 0
    while i < len(lines):
        if lines[i].startswith("ENCODING"):
            code = int(lines[i].split()[1])
            j = i
            while not lines[j].startswith("BBX"):
                j += 1
            bw, bh, bx, by = map(int, lines[j].split()[1:5])
            dw = cw
            k = i
            while not lines[k].startswith("BITMAP"):
                if lines[k].startswith("DWIDTH"):
                    dw = int(lines[k].split()[1])
                k += 1
            rows = [int(r, 16) for r in lines[k + 1:k + 1 + bh]]
            nbits = ((bw + 7) // 8) * 8
            cell = [0] * (cw * ch)
            top = base - (by + bh)
            for r, bits in enumerate(rows):
                for c in range(bw):
                    if bits >> (nbits - 1 - c) & 1:
                        x, y = bx + c, top + r
                        if 0 <= x < cw and 0 <= y < ch:
                            cell[y * cw + x] = 15
            glyphs[code] = (dw, cell)
            i = k + bh
        i += 1
    if last >= 0xDC and 0xDC not in glyphs and ord("U") in glyphs:      # Ü for "HÜGELTON": U with dots on its top row
        dw, cell = glyphs[ord("U")]
        cell = cell[:]
        lit = [x for y in range(ch) for x in range(cw) if cell[y * cw + x]]
        if lit:
            top = min(y for y in range(ch) for x in range(cw) if cell[y * cw + x])
            for x in range(cw):
                cell[top * cw + x] = 15 if x in (min(lit), max(lit)) else 0
            for x in range(cw):
                cell[(top + 1) * cw + x] = 0
            glyphs[0xDC] = (dw, cell)
    missing = [chr(c) for c in range(first, min(last, 126) + 1) if c not in glyphs]
    if missing:
        print(f"font {path.name}: no glyph for {''.join(missing)!r} (drawn as '?')")
    out = []
    for c in range(first, last + 1):
        dw, cell = glyphs.get(c, glyphs[ord("?")])
        bw = cw + 2 * PAD                              # keep the PAD convention of the TTF path
        px_ = []
        for y in range(ch):
            px_ += [0] * PAD + cell[y * cw:(y + 1) * cw] + [0] * PAD
        out.append((dw * scale, bw * scale, upscale(px_, bw, ch, scale)))
    return ch * scale, out


def font_path(name):
    for d in [FONTS, *FONTDIRS]:
        if (d / name).exists():
            return d / name
    raise SystemExit(f"gen_font: {name} not in {FONTS} or X0X_FONT_DIRS")


def render(ttf, px, scale, pixel, first, last):
    if ttf.endswith(".bdf"):
        return render_bdf(font_path(ttf), scale, first, last)
    name, _, var = ttf.partition("@")
    font = ImageFont.truetype(str(font_path(name)), px)
    if var:                                            # a variable font: "wght=600"
        axes = dict(kv.split("=") for kv in var.split(","))
        names = [a["name"] if isinstance(a["name"], str) else a["name"].decode() for a in font.get_variation_axes()]
        font.set_variation_by_axes([float(axes.get(n.lower()[:4], axes.get(n, 0)) or a["default"])
                                    for n, a in zip(names, font.get_variation_axes())])
    # The local build font renderer has no libraqm; the digits in the bundled
    # Barlow and Nunito faces are already tabular in this font set.
    feats = None
    asc, desc = font.getmetrics()
    top = max(0, font.getbbox("A8|(", features=feats)[1] - 1)
    h = asc + desc - top
    glyphs = []
    for c in range(first, last + 1):
        ch = chr(c)
        adv = int(round(font.getlength(ch, features=feats)))
        bw = adv + 2 * PAD
        img = Image.new("L", (bw, asc + desc), 0)
        ImageDraw.Draw(img).text((PAD, 0), ch, font=font, fill=255, features=feats)
        img = img.crop((0, top, bw, top + h))
        px_ = [(15 if v >= 128 else 0) if pixel else min(15, (v + 8) // 17) for v in img.tobytes()]
        glyphs.append((adv * scale, bw * scale, upscale(px_, bw, h, scale)))
    return h * scale, glyphs


def lit_box(g, bw, h):
    """a glyph's lit box: (left column, top row, width, rows); (0, 0, 0, 0) for a blank one"""
    rows = [y for y in range(h) if any(g[y * bw:(y + 1) * bw])]
    cols = [x for x in range(bw) if any(g[y * bw + x] for y in range(h))]
    if not rows:
        return 0, 0, 0, 0
    return cols[0], rows[0], cols[-1] - cols[0] + 1, rows[-1] - rows[0] + 1


def huff_lengths(freq):
    """{alpha: code length} of a Huffman code for the counts freq (none longer than HUF_LEN)"""
    freq = {k: v for k, v in freq.items() if v}
    while True:
        if len(freq) == 1:
            return {k: 1 for k in freq}
        heap = [(v, i, [k]) for i, (k, v) in enumerate(sorted(freq.items()))]
        heapq.heapify(heap)
        depth = dict.fromkeys(freq, 0)
        n = len(heap)
        while len(heap) > 1:
            a, b = heapq.heappop(heap), heapq.heappop(heap)
            for k in a[2] + b[2]:
                depth[k] += 1
            heapq.heappush(heap, (a[0] + b[0], n, a[2] + b[2]))
            n += 1
        if max(depth.values()) <= HUF_LEN:
            return depth
        freq = {k: (v + 1) // 2 for k, v in freq.items()}


def huff_code(lengths):
    """-> (the table's bytes: counts of lengths 1..HUF_LEN, then the alphas in code order, padded to 16), {alpha: (code,
    length)}: the canonical code (by length, then alpha)"""
    order = sorted(lengths, key=lambda k: (lengths[k], k))
    counts = [sum(1 for k in order if lengths[k] == n) for n in range(1, HUF_LEN + 1)]
    codes, code, at = {}, 0, 0
    for n in range(1, HUF_LEN + 1):
        for _ in range(counts[n - 1]):
            codes[order[at]] = (code, n)
            code += 1
            at += 1
        code <<= 1
    return counts + order + [0] * (16 - len(order)), codes


def ctx_of(prev):
    return 0 if prev == 0 else 1 if prev == 15 else 2


def pack_face(boxes):
    """boxes: per glyph (its 3 header bytes, its alphas row by row) -> (data, offsets): the codes, then each glyph's
    header and its bits"""
    freq = [dict.fromkeys(range(16), 0) for _ in range(HUF_CODES)]
    for _, alphas in boxes:
        prev = 0
        for a in alphas:
            freq[ctx_of(prev)][a] += 1
            prev = a
    tables, codes = [], []
    for c in range(HUF_CODES):
        t, k = huff_code(huff_lengths(freq[c]) if any(freq[c].values()) else {})
        tables += t
        codes.append(k)
    data, offs = list(tables), []
    for head, alphas in boxes:
        offs.append(len(data))
        data += head
        acc, nb, prev = 0, 0, 0
        for a in alphas:
            code, n = codes[ctx_of(prev)][a]
            acc, nb, prev = (acc << n) | code, nb + n, a
            while nb >= 8:
                nb -= 8
                data.append((acc >> nb) & 0xFF)
        if nb:
            data.append((acc << (8 - nb)) & 0xFF)
    return data, offs


def main(out):
    lines = [f"/* generated by tools/gen_font.py: font set {FONTSET} */",
             "#pragma once", "#include <stdint.h>", f"#define FONT_PAD {PAD}  /* x scale for L, see below */", ""]
    if TRIM:
        lines[-1:] = ["#define FONT_TRIM 1  /* each glyph its lit box: left, top, rows, then the rows (FM1_FONT_TRIM) */", ""]
    if PACK:
        lines[-1:] = [f"#define FONT_PACK 1  /* the rows' alphas Huffman-coded, {HUF_CODES} codes first (FM1_FONT_PACK) */",
                      f"#define FONT_HUF_LEN {HUF_LEN}", f"#define FONT_HUF_SZ {HUF_LEN + 16}", ""]
    box_max = 0                 # the largest glyph box's alphas (FONT_BOX_MAX: device/gfx.c APP_FONT_CACHE holds one)
    for name, ttf, px, scale, pixel, first, last in SIZES:
        h, glyphs = render(ttf, px, scale, pixel, first, last)
        data, offs = [], []
        bws, boxes = [], []
        for adv, bw, g in glyphs:
            offs.append(len(data))
            x0, y0, w, n = 0, 0, bw, h
            if TRIM:
                x0, y0, w, n = lit_box(g, bw, h)
                data += [x0, y0, n]
            bws.append(w)
            box_max = max(box_max, w * n)
            boxes.append(([x0, y0, n], [a for y in range(y0, y0 + n) for a in g[y * bw + x0:y * bw + x0 + w]]))
            for y in range(y0, y0 + n):
                row = g[y * bw + x0:y * bw + x0 + w] + [0]
                for x in range(0, w, 2):
                    data.append((row[x] << 4) | row[x + 1])
        if PACK:
            raw = len(data)
            data, offs = pack_face(boxes)
            print(f"font {name}: packed {raw} B -> {len(data)} B")
        assert len(data) < 65536
        lines.append(f"static const uint8_t FONT_{name}_DATA[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
        lines.append("};")
        lines.append(f"static const uint16_t FONT_{name}_OFF[{len(offs)}] = {{" + ", ".join(map(str, offs)) + "};")
        lines.append(f"static const uint8_t FONT_{name}_ADV[{len(glyphs)}] = {{" +
                     ", ".join(str(a) for a, _, _ in glyphs) + "};")
        lines.append(f"static const uint8_t FONT_{name}_BW[{len(glyphs)}] = {{" +
                     ", ".join(map(str, bws)) + "};")
        lines.append(f"static const felucca_font_t FONT_{name} = {{ {h}, {PAD * scale}, {first}, {last}, "
                     f"FONT_{name}_ADV, FONT_{name}_BW, FONT_{name}_OFF, FONT_{name}_DATA }};")
        lines.append("")
        print(f"font {name}: {ttf} {px}px h {h}, digit adv {glyphs[ord('0') - first][0]}, {len(data)} B")
    if PACK:
        lines.append(f"#define FONT_BOX_MAX {box_max}  /* the largest glyph's alphas (device/gfx.c APP_FONT_CACHE: at least this) */")
        lines.append("")
    print(f"font set {FONTSET}: {sum(len(l) for l in lines if l.startswith('    0x')) // 6} B (approx)")
    Path(out).write_text("\n".join(lines))


if __name__ == "__main__":
    main(sys.argv[1])
