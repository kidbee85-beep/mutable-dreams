/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The USB serial console for an app (fm1.toml usb_console = true: FM1B_CDC 1): felucca-bsp's CDC-ACM function (chip/usb.c,
 * FELUCCA_CDC: its two rings, the TIMER5 interrupt moving them over EP3) as plat.h's plat_console_* calls. What the
 * console says and does is the app's: the platform only carries the bytes. Main loop only (one reader, one writer: the
 * rings' other ends are the interrupt's). Part of unity.c, after usb.c. */
#if FM1B_CDC
int plat_console_open(void) { return usb.config && usb_cdc_on && cdc.dtr; }
int plat_console_read(void)
{
    uint8_t c;
    if (ci_r == ci_w)
        return -1;
    RING_PUBLISH();                                     /* (the interrupt wrote the byte before it moved ci_w) */
    c = cdc_in[ci_r % CI_N];
    RING_PUBLISH();
    ci_r++;
    return c;
}
uint32_t plat_console_room(void) { return plat_console_open() ? CO_N - (co_w - co_r) : 0u; }
uint32_t plat_console_write(const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i, room = plat_console_room();
    if (n > room)
        n = room;
    for (i = 0; i < n; i++)
        cdc_out[(co_w + i) % CO_N] = s[i];
    RING_PUBLISH();                                     /* (the bytes before the index the interrupt reads) */
    co_w += n;
    return n;
}
void plat_console_present(int on) { usb_cdc_switch((uint32_t)(on != 0)); }
#else
int plat_console_open(void) { return 0; }
int plat_console_read(void) { return -1; }
uint32_t plat_console_room(void) { return 0; }
uint32_t plat_console_write(const void *p, uint32_t n) { (void)p, (void)n; return 0; }
void plat_console_present(int on) { (void)on; }
#endif
