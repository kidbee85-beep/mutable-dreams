/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The host simulator's whole-flash image (host/plat_host.c flash_dump / flash_load) against the device's own store
 * code: device/storage.c reads the template's settings out of the image the simulator wrote (tests/flash_dump.txt),
 * saves them again (to copy B, seq 2, as the device's next save does) and writes the image back out, which
 * tests/flash_load.txt then puts into a new run of the simulator. The data area's bytes stay where they were.
 *   flash_image_test IN OUT */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];

static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    for (i = 0; i < n; i++)                   /* NOR: bits only go from 1 to 0 */
        nor[off + i] &= s[i];
    return 0;
}
#include "../include/plat.h"
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_BLE, OBJ_NOBJ };   /* the template's objects and map (template/app.h) */
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0xDE000u}
#include "../device/storage.c"

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    static uint8_t set[ST_PAYLOAD_MAX], again[ST_PAYLOAD_MAX];
    st_hdr_t h;
    FILE *f;
    int bad = 0, n, m;
    if (argc < 3 || !(f = fopen(argv[1], "rb"))) {
        fprintf(stderr, "usage: flash_image_test IN OUT (IN: tests/flash_dump.txt's flash.bin)\n");
        return 2;
    }
    n = (int)fread(nor, 1, sizeof nor, f);
    fclose(f);
    bad += check("the image is the whole 1 MiB part", n == (int)sizeof nor);
    n = st_load(OBJ_SETTINGS, set, sizeof set);
    bad += check("storage.c loads the settings the simulator saved", n > 0);
    bad += check("  from copy A, seq 1 (a first save)", st_current(OBJ_SETTINGS, &h) == 0 && h.seq == 1u);
    bad += check("  copy B erased", nor[0xFD000] == 0xFF && !memcmp(nor + 0xFD000, nor + 0xFD001, 4095));
    bad += check("no other object (none was saved)", st_load(OBJ_PANEL, again, sizeof again) < 0 &&
                                                     st_load(OBJ_BLE, again, sizeof again) < 0);
    bad += check("the data area's bytes at 0x97000 (12 34 56 78)",
                 nor[0x97000] == 0x12 && nor[0x97001] == 0x34 && nor[0x97002] == 0x56 && nor[0x97003] == 0x78);
    bad += check("the rest of 0x97000's sector erased", nor[0x97004] == 0xFF && nor[0x97FFF] == 0xFF);
    bad += check("the app area left erased (not the simulator's)", nor[0x4000] == 0xFF && nor[0x92FFF] == 0xFF);
    bad += check("storage.c saves them again", n > 0 && st_save(OBJ_SETTINGS, set, (uint32_t)n) == 0);
    m = st_load(OBJ_SETTINGS, again, sizeof again);
    bad += check("  now copy B, seq 2, the same bytes", st_current(OBJ_SETTINGS, &h) == 1 && h.seq == 2u &&
                                                        m == n && !memcmp(set, again, (size_t)n));
    if (!(f = fopen(argv[2], "wb")) || fwrite(nor, 1, sizeof nor, f) != sizeof nor) {
        fprintf(stderr, "flash_image_test: %s not written\n", argv[2]);
        return 2;
    }
    fclose(f);
    printf(bad ? "FAILED\n" : "all ok\n");
    return bad ? 1 : 0;
}
