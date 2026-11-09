/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The battery test app: see app.h */
static int32_t raw_read = -1, frames;              /* the last plat_battery_raw the app read; frames that read it */

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
void app_frame(void)
{
    raw_read = plat_battery_raw();
    frames++;
}
void app_input_only(void) {}
void app_say(const char *a, const char *b) { (void)a, (void)b; }
#ifdef PLAT_HOST                                   /* (the simulator's checks: not in the device build) */
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "app_battery_raw"))           /* what the app read last (-1: never) */
        *got = (int)raw_read;
    else if (!strcmp(what, "app_frames_read"))
        *got = (int)frames;
    else
        return 0;
    return 1;
}
#endif
