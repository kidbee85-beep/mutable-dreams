/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The platform's own screens (boot, crash, safe mode, update, restore, calibration, the splash) write with these:
 * three faces, SYS_S (the body), SYS_B (a heading), SYS_BIG (APP_BIG_FONT: the splash's name, the panel's screens).
 * fm1-base's FONT_S, FONT_B and APP_BIG_FONT by default; an app with its own fonts sets APP_SYS_TEXT in app.h and
 * defines app_sys_h, app_sys_w and app_sys_text (static, anywhere in the app), and fm1-base's faces are not linked
 * (LOOM-8 saves 18 KB). Their text is in capitals, apart from the splash's tagline and APP_BACKUP_WHAT. */
enum { SYS_S, SYS_B, SYS_BIG };
#ifdef APP_SYS_TEXT
static int32_t app_sys_h(int face);                                 /* the face's line height */
static int32_t app_sys_w(int face, const char *s);                  /* the text's advance */
static void app_sys_text(int32_t x, int32_t y, int face, const char *s, uint16_t c);   /* on the canvas (cv_begin) */
#define sys_h app_sys_h
#define sys_w app_sys_w
#define sys_text app_sys_text
#else
static const felucca_font_t *sys_face(int face) { return face == SYS_BIG ? &APP_BIG_FONT : face == SYS_B ? &FONT_B : &FONT_S; }
static int32_t sys_h(int face) { return sys_face(face)->h; }
static int32_t sys_w(int face, const char *s) { return text_w(sys_face(face), s); }
static void sys_text(int32_t x, int32_t y, int face, const char *s, uint16_t c) { cv_text(x, y, sys_face(face), s, c); }
#endif

/* one-shot: text in a box, cleared to black, blitted (draw_text_box with a face) */
static void sys_box(uint32_t x, uint32_t y, uint32_t w, int face, const char *s, uint16_t c, int align)
{
    int32_t tw = sys_w(face, s), tx = 0;
    cv_begin(w, (uint32_t)sys_h(face), C_BLACK);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    sys_text(tx, 0, face, s, c);
    cv_blit(x, y);
    lcd_sync();
}
