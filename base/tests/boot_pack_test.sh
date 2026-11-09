#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The boot's LZMA load images: tools/boot_pack.py packs real section images (an app's build/ ramtext.bin and data.bin if
# given, else made-up ones), device/boot_lzma.h unpacks them on the host (tests/boot_pack_test.c).
#   tests/boot_pack_test.sh [BUILD_DIR]
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
T=build/tests/boot_pack
mkdir -p "$T"
if [ -n "${1:-}" ] && [ -f "$1/ramtext.bin" ]; then
    cp "$1/ramtext.bin" "$1/data.bin" "$T/"
else                                   # code-like bytes: repeats and noise, and a small .data
    "$PY" -c "import random; r=random.Random(7); open('$T/ramtext.bin','wb').write(bytes(r.choice([0,1,2,0x24,0x80,r.randrange(256)]) for _ in range(20000)))"
    "$PY" -c "open('$T/data.bin','wb').write(bytes(range(256))*12)"
fi
"$PY" -c "import sys; sys.path.insert(0, 'tools'); import boot_pack; open('$T/packed.bin','wb').write(boot_pack.pack_images([open('$T/' + s, 'rb').read() for s in boot_pack.SECTIONS]))"
$CC -O2 -std=c99 -Wall -Wextra -Werror -o "$T/boot_pack_test" tests/boot_pack_test.c
"$T/boot_pack_test" "$T/packed.bin" "$T/ramtext.bin" "$T/data.bin"
