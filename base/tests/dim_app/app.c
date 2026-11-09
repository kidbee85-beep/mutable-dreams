/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The dim-levels test app: see app.h */
static void dim_input(void)
{
    uint32_t b = plat_buttons();
    if (b & (1u << B_FX))
        plat_dim_ns(500u);
    if (b & (1u << B_SEL))
        plat_dim_ns(1000u);
    if (b & (1u << B_ENV))
        plat_dim_ns(2000u);
    if (b & (1u << B_LFO))
        plat_dim_ns(9999u);
    if (b & (1u << B_EDIT))
        plat_dim_ns(10u);
    if (b & (1u << B_GLO))
        plat_dim_ns(0u);
    plat_lights(0, 0, (1u << NB) - 1u, (1u << NKEYS) - 1u, b & (1u << B_SAVE) ? 1 : 2);
}

void app_init(void) {}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) {}
void app_ui_init(void) { lcd_fill(0, 0, 240, 240, C_BLACK); }
void app_frame(void) { dim_input(); }
void app_input_only(void) { dim_input(); }
void app_say(const char *a, const char *b) { (void)a, (void)b; }
