#!/usr/bin/env python3
"""Check target rejection and compatibility with the released FM-1 map.

No device is opened. The digest is of the 24 little-endian addresses from
the published 1.5 layout, not a newly accepted 2.0 or SMK flash map.
"""
import hashlib
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RELEASED_MAP_SHA256 = "95ecbc8254803d42fe3905cfb5c269d1884218d6362f06e554a652a636bf7a42"
SOURCE = '''#include <stdio.h>
#include "target_storage.h"
int main(void) {
    const unsigned map[] = MD_TARGET_STORE_MAP;
    const unsigned count = sizeof(map)/sizeof(map[0]);
    if (count != MD_TARGET_STORE_OBJECTS) return 2;
    for (unsigned i=0; i<count; ++i) {
        if (map[i] % 4096u || map[i]+MD_TARGET_STORE_PAIR_BYTES>MD_TARGET_FLASH_BYTES) return 3;
        for (unsigned j=0; j<i; ++j)
            if (map[i]<map[j]+MD_TARGET_STORE_PAIR_BYTES && map[j]<map[i]+MD_TARGET_STORE_PAIR_BYTES) return 4;
        printf("%u\\n", map[i]);
    }
    return 0;
}
'''


def main():
    compiler = shutil.which("cc")
    assert compiler, "A native C compiler is needed for the offline target test."
    cases = [([], True, None), (["MD_TARGET_FM1"], True, None),
             (["MD_TARGET_SMK37_ELITE"], False, "not qualified"),
             (["MD_TARGET_SMK37_PRO"], False, "not qualified"),
             (["MD_TARGET_FM1", "MD_TARGET_SMK37_ELITE"], False, "exactly one"),
             (["MD_TARGET_SMK37_ELITE", "MD_TARGET_SMK37_PRO"], False, "exactly one")]
    with tempfile.TemporaryDirectory(prefix="md20-target-") as temp:
        src = Path(temp)/"target.c"; src.write_text(SOURCE)
        out = Path(temp)/"target"
        for flags, accept, diagnostic in cases:
            result = subprocess.run([compiler, "-std=c99", "-Werror", "-I"+str(ROOT/"app"),
                                     *["-D"+flag for flag in flags], str(src), "-o", str(out)],
                                    capture_output=True, text=True)
            assert (result.returncode == 0) is accept, (flags, result.stderr)
            if accept:
                addresses = [int(v) for v in subprocess.check_output([str(out)],text=True).splitlines()]
                digest = hashlib.sha256(struct.pack("<"+"I"*len(addresses), *addresses)).hexdigest()
                assert digest == RELEASED_MAP_SHA256, "Published FM-1 storage layout changed."
            else:
                assert diagnostic in result.stderr, result.stderr
    print("PASS: default/explicit FM-1 retain the 1.5 map; both SMK models and conflicting targets are rejected.")


if __name__ == "__main__":
    main()
