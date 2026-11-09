/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* the headphone jack (plat_headphones): its switch on PB5, measured on an FM-1 2026-10-29 with a probe of the input
 * registers: 0 with headphones in, 1 with nothing plugged. Read only (its direction,
 * pull and function as the boot left them: an input, enabled; no driver here or in the bsp claims it). The contact
 * bounces ~150 ms a plug (several flips within 50..150 ms): a state counts once the pin has held it HP_MS. Every main
 * loop pass (main_fm1.c); the first reading counts at once. Part of plat_fm1.c; on its own for tests/hp_test.c, which
 * gives it fm1_hp_in and fm1_ms over a simulated jack */
#define HP_MS 100u
static volatile int8_t hp_st = -1;
static uint8_t hp_raw;
static uint32_t hp_t;
static void plat_hp_read(void)
{
    uint8_t raw = (uint8_t)fm1_hp_in();           /* (felucca-bsp boards/fm1/fm1_jack.h: PB5) */
    if (hp_st < 0) {
        hp_raw = raw;
        hp_t = fm1_ms;
        hp_st = (int8_t)raw;
    } else if (raw != hp_raw) {
        hp_raw = raw;
        hp_t = fm1_ms;
    } else if (raw != (uint8_t)hp_st && fm1_ms - hp_t >= HP_MS)
        hp_st = (int8_t)raw;
}
int plat_headphones(void) { return hp_st; }
