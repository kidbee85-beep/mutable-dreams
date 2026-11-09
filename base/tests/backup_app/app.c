/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The backup test app: see app.h */
static uint32_t value, saved, held_before, playing, hold;

static void load(void)
{
    value = 0;
    plat_store_load(OBJ_STATE, &value, sizeof value);
    saved = value;
}
static void input(void)
{
    uint32_t b = plat_buttons(), edge = b & ~held_before;
    held_before = b;
    if (edge & (1u << B_SEL))
        value++;                                   /* (saved on the next SAVE, or by a backup's flush) */
    if (edge & (1u << B_SAVE))
        app_backup_flush();
    if (edge & (1u << B_PLAY))
        playing = !playing;
    plat_lights(value & 0x3FFFu, 0, 0, 0, 0);
}

void app_init(void) { load(); }
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

int app_backup_playing(void) { return (int)playing; }
void app_backup_flush(void)
{
    if (!hold && value != saved) {
        plat_store_save(OBJ_STATE, &value, sizeof value);
        saved = value;
    }
}
void app_backup_hold(int on) { hold = (uint32_t)on; }
void app_backup_restored(void) { load(); }

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "value"))
        *got = (int)value;
    else
        return 0;
    return 1;
}
#endif
