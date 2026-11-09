/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The restart test app: see app.h */
static uint32_t starts, value, held_before;

static void show(void) { plat_lights(value & 0x3FFFu, 1u << (starts % 27u), 0, 0, 0); }
static void input(void)
{
    uint32_t b = plat_buttons(), edge = b & ~held_before, rel = held_before & ~b;
    held_before = b;
    if (edge & (1u << B_SEL)) {
        value++;
        plat_store_save(OBJ_STATE, &value, sizeof value);
    }
    if (rel & (1u << B_FX))                    /* (on release: a held button would restart every start) */
        plat_restart();
    show();
}

void app_init(void)
{
    starts++;
    value = 0;
    plat_store_load(OBJ_STATE, &value, sizeof value);
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) {}
void app_ui_init(void) { lcd_fill(0, 0, 240, 240, C_BLACK); }
void app_frame(void) { input(); }
void app_input_only(void) { input(); }
void app_say(const char *a, const char *b) { (void)a, (void)b; }
