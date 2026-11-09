/* SPDX-License-Identifier: MPL-2.0 */
/* The boot's LZMA load images (device/boot_lzma.h, tools/boot_pack.py, after Melodee's boot_lzma_test.c): every record
 * of a packed file unpacks to exactly its raw bytes; a truncated stream, a flipped byte, a wrong size or a trailing byte
 * is refused. The records come from tests/boot_pack_test.sh (boot_pack.pack_images over real section images).
 *   boot_pack_test PACKED RAW1 [RAW2 ...] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../device/boot_lzma.h"

static uint16_t probs[BOOT_LZMA_PROBS];
static int bad;
static void check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint8_t *slurp(const char *path, long *n)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)*n + 1);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n)
        *n = -1;
    fclose(f);
    return b;
}
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int main(int argc, char **argv)
{
    long np, at = 0;
    uint8_t *packed = argc > 2 ? slurp(argv[1], &np) : 0;
    if (!packed || np < 16) {
        fprintf(stderr, "usage: boot_pack_test PACKED RAW1 [RAW2 ...]\n");
        return 2;
    }
    for (int i = 2; i < argc; i++) {
        long nr;
        uint8_t *raw = slurp(argv[i], &nr), *out;
        uint32_t magic = u32(packed + at), n = u32(packed + at + 4), size = u32(packed + at + 8);
        const uint8_t *s = packed + at + 16;
        char what[160];
        snprintf(what, sizeof what, "%s: %ld B from %u packed", argv[i], nr, n);
        out = malloc((size_t)size + 2);
        check(what, raw && magic == BOOT_LZMA_MAGIC && size == (uint32_t)nr &&
                    boot_lzma(out, size, s, n, probs) && !memcmp(out, raw, (size_t)size));
        if (n > 8) {
            check("  a truncated stream is refused", !boot_lzma(out, size, s, n - 1, probs));
            uint8_t *flip = malloc(n);
            memcpy(flip, s, n);
            flip[n / 2] ^= 0x5A;
            check("  a flipped byte is refused (or unpacks to other bytes)",
                  !boot_lzma(out, size, flip, n, probs) || memcmp(out, raw, (size_t)size));
            free(flip);
            check("  a wrong size is refused", size < 1 || !boot_lzma(out, size - 1, s, n, probs));
            uint8_t *more = malloc(n + 1);
            memcpy(more, s, n);
            more[n] = 0;
            check("  a trailing byte is refused", !boot_lzma(out, size, more, n + 1, probs));
            free(more);
        }
        at += 16 + n + (-(long)n & 3);
        free(out);
        free(raw);
    }
    check("every record read, nothing after them", at == np);
    printf(bad ? "BOOT PACK TESTS FAILED\n" : "boot pack tests passed\n");
    return bad != 0;
}
