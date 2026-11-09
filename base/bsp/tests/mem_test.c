/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Compare chip/libc.c's memset and memcpy with the C library's over every
 * alignment of source and destination and lengths 0..300, checking that
 * bytes around the destination stay untouched.
 *
 *   cc -fno-builtin -Wall -Wno-unused-function -fsanitize=address,undefined \
 *      -o mem_test tests/mem_test.c
 *   ./mem_test
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build the BSP's routines under other names, beside the host's. */
#define memset bsp_memset
#define memcpy bsp_memcpy
#define memcmp bsp_memcmp
#include "../chip/libc.c"
#undef memset
#undef memcpy
#undef memcmp

#define PAD 16
#define MAXN 300

static int fails;

static void fail(const char *what, int a, int b, int n, int c)
{
    if (fails++ < 10)
        printf("FAIL %s dst_align=%d src_align=%d n=%d c=%d\n", what, a, b, n, c);
}

int main(void)
{
    static unsigned char dst[PAD + 8 + MAXN + PAD + 8], ref[sizeof dst];
    static unsigned char src[8 + MAXN + 8];
    static const int fills[] = {0, 1, 0x5a, 0xff, 0x1ff, -1};
    long runs = 0;
    int a, b, n, i, k;

    for (i = 0; i < (int)sizeof src; i++)
        src[i] = (unsigned char)(i * 7 + 3);

    for (a = 0; a < 8; a++)
        for (n = 0; n <= MAXN; n++)
            for (i = 0; i < 6; i++) {
                void *r;
                for (k = 0; k < (int)sizeof dst; k++)
                    dst[k] = ref[k] = (unsigned char)(k ^ 0xa5);
                r = bsp_memset(dst + PAD + a, fills[i], n);
                memset(ref + PAD + a, fills[i], n);
                runs++;
                if (r != dst + PAD + a || memcmp(dst, ref, sizeof dst))
                    fail("memset", a, 0, n, fills[i]);
            }

    for (a = 0; a < 8; a++)
        for (b = 0; b < 8; b++)
            for (n = 0; n <= MAXN; n++) {
                void *r;
                for (k = 0; k < (int)sizeof dst; k++)
                    dst[k] = ref[k] = (unsigned char)(k ^ 0xa5);
                r = bsp_memcpy(dst + PAD + a, src + b, n);
                memcpy(ref + PAD + a, src + b, n);
                runs++;
                if (r != dst + PAD + a || memcmp(dst, ref, sizeof dst))
                    fail("memcpy", a, b, n, 0);
            }

    printf("%ld cases, %d failures\n", runs, fails);
    return fails != 0;
}
