/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The trimmed fonts (tools/gen_font.py FM1_FONT_TRIM, fm1.toml font_trim) draw the same pixels as the full ones:
 * every face's every character, alone and in a line, in two colours over a patterned canvas (the blend), at positions
 * that clip each edge, in a band (cv_band: the rows drawing may touch, its y offset). Built twice, once against each
 * header (-I its directory); each writes its canvases to the file given and the two files are compared (tests/
 * run_tests.sh font-trim).
 *   cc -I DIR tests/font_trim_test.c && ./a.out OUT.bin */
#include <stdio.h>
#include <stdint.h>
#define PLAT_HOST 1
#define __attribute__(x)                              /* (gfx.c's .pool section: not on the host) */
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    (void)x; (void)y; (void)w; (void)h; (void)px;
}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    (void)x; (void)y; (void)w; (void)h; (void)c;
}
static void lcd_sync(void) {}
#include "../include/plat.h"
#include "../device/gfx.c"

static FILE *out;
static unsigned long drawn;

static void pattern(void)                             /* a background the blend shows against */
{
    for (uint32_t i = 0; i < cv_w * cv_h; i++)
        cv_px[i] = (uint16_t)(i * 2654435761u >> 16);
}

static void dump(void)
{
    fwrite(cv_px, 2, cv_w * cv_h, out);
    for (uint32_t i = 0; i < cv_w * cv_h; i++)
        drawn += cv_px[i] != (uint16_t)(i * 2654435761u >> 16);
}

static void face(const felucca_font_t *f)
{
    static const int32_t xs[] = {-9, 0, 3, 220, 236}, ys[] = {-20, -3, 0, 5, 30};
    char line[160];
    uint32_t n = 0;
    for (uint32_t ch = 32; ch < 127; ch++)
        line[n++] = (char)ch;
    line[n] = 0;
    for (uint32_t ch = 32; ch < 127; ch++) {          /* each character alone, at each clip */
        char one[2] = {(char)ch, 0};
        cv_begin(240, 48, C_BLACK);
        pattern();
        for (uint32_t i = 0; i < sizeof xs / sizeof xs[0]; i++)
            cv_text(xs[i], ys[i], f, one, i & 1 ? 0xF81Fu : C_WHITE);
        dump();
    }
    for (int32_t x = -40; x <= 8; x += 24) {          /* the whole set in a line, then in a band */
        cv_begin(240, 48, C_BLACK);
        pattern();
        cv_text(x, 2, f, line, 0x07E0u);
        cv_text(x + 1, 20, f, line + 33, C_WHITE);
        dump();
        cv_begin(240, 48, C_BLACK);
        pattern();
        cv_band(10, 20);
        cv_text(x, 4, f, line + 10, 0x001Fu);
        cv_text(x, -6, f, line + 50, C_WHITE);
        dump();
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || !(out = fopen(argv[1], "wb")))
        return 2;
    face(&FONT_T);
    face(&FONT_XS);
    face(&FONT_S);
    face(&FONT_B);
    face(&FONT_M);
    face(&FONT_L);
    fclose(out);
    printf("font_trim_test: %lu pixels drawn\n", drawn);
    return drawn ? 0 : 1;
}
