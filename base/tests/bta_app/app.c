/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* Bluetooth audio's calls, driven by the buttons: PLAY scans, SAVE connects found device 0, OCT+ remembered device 1,
 * REC hangs up, OCT- forgets remembered device 0, FX switches the output (the device or the FM-1's own) while it
 * streams. A sine plays all along. */
#include "app.h"
#include <string.h>

static uint32_t held, phase;
static uint32_t mem_block[18432u / 4u];            /* (lent when the build asks for one: fm1.toml bt_audio_mem = "app") */
static int lost_seen;                             /* the last plat_bta_lost answer that was not NONE */

void app_init(void) {}
int app_ble_load(struct plat_ble_rec *r) { return plat_store_load(OBJ_BTREC, r, sizeof *r) == (int)sizeof *r; }
int app_ble_save(const struct plat_ble_rec *r) { return plat_store_save(OBJ_BTREC, r, sizeof *r); }
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    for (i = 0; i < n; i++, phase += 0x01000000u) {
        int32_t s = (int32_t)(phase >> 8) - 0x800000;
        s = (s < 0 ? -s : s) - 0x400000;          /* (a triangle) */
        lr[2 * i] = lr[2 * i + 1] = (int32_t)(((int64_t)s * master_q12) >> 14);
    }
}
void app_panic(void) {}
void app_ui_init(void)
{
    uint32_t n = plat_bta_mem_bytes();
    if (n && n <= sizeof mem_block)
        (void)plat_bta_mem(mem_block);            /* (before plat_bta_on(1), which does nothing without it) */
    plat_bta_on(1);
}
void app_say(const char *a, const char *b) { (void)a, (void)b; }
static void input(void)
{
    uint32_t b = plat_buttons(), down = b & ~held;
    int w;
    held = b;
    if (down & (1u << B_PLAY))
        plat_bta_scan(1);
    if (down & (1u << B_SAVE))
        plat_bta_pick(0);
    if (down & (1u << B_OCTUP))
        (void)plat_bta_reconnect(1);
    if (down & (1u << B_REC))
        plat_bta_hangup();
    if (down & (1u << B_OCTDN))
        plat_bta_forget(0);
    if (down & (1u << B_FX))
        plat_bta_output(!plat_bta_out_device());
    if ((w = plat_bta_lost()) != PLAT_BTA_LOST_NONE)
        lost_seen = w;
}
void app_input_only(void) { input(); }
void app_frame(void) { input(); }

int app_expect(const char *what, int *got)
{
    char nm[17];
    if (!strcmp(what, "lost"))                    /* PLAT_BTA_LOST_* last said */
        *got = lost_seen;
    else if (!strcmp(what, "out_device"))
        *got = plat_bta_out_device();
    else if (!strcmp(what, "latency_ms"))
        *got = plat_bta_latency_us() < 0 ? -1 : plat_bta_latency_us() / 1000;
    else if (!strcmp(what, "mem_bytes"))          /* the block the build wants (0: static buffers) */
        *got = (int)plat_bta_mem_bytes();
    else if (!strcmp(what, "mem_take"))           /* taking it back: 0 done or none lent, -1 still in use */
        *got = plat_bta_mem(0);
    else if (!strcmp(what, "found"))
        *got = plat_bta_found(0, nm) + plat_bta_found(1, nm);
    else if (!strcmp(what, "last_is_b"))          /* remembered device 0 is "B" */
        *got = plat_bta_known(0, nm) && !strcmp(nm, "B");
    else
        return 0;
    return 1;
}
