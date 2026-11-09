/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* plat_console_* (device/console.c) over the CDC rings as felucca-bsp's chip/usb.c keeps them (cdc_in 256 B, the
 * interrupt writing it and moving ci_w; cdc_out 2,048 B, the interrupt reading it and moving co_r; free-running
 * indexes), the interrupt's side played here: never open until the computer has the port (configured, the console
 * presented, DTR); every byte read in order, 0xFF as 255 (not "none"); a write takes at most the room and never waits;
 * the indexes wrap; present switches the function. Built twice: FM1B_CDC 1, and 0 (never open, nothing taken). Not
 * the USB function itself: that is felucca-bsp's (its usb_host_test device mode). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef FM1B_CDC
#define FM1B_CDC 1
#endif
#if FM1B_CDC
#define CI_N 256u                                 /* (chip/usb.c) */
#define CO_N 2048u
static uint8_t cdc_in[CI_N], cdc_out[CO_N];
static volatile uint32_t ci_w, ci_r, co_w, co_r;
static struct {
    uint32_t config;
} usb;
static struct {
    volatile uint8_t dtr;
} cdc;
static uint8_t usb_cdc_on = 1;
static unsigned publishes, switches;
#define RING_PUBLISH() (publishes++)
static void usb_cdc_switch(uint32_t on)
{
    switches++;
    usb_cdc_on = (uint8_t)on;
    cdc.dtr = 0;                                  /* (as chip/usb.c: the console stops writing) */
}
#endif
#include "console.c"

static int fails;
static void want(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

#if FM1B_CDC
/* the interrupt's side: a packet from the computer into cdc_in (as ep3_rx), the bytes it sends (as ep3_tx) */
static uint32_t isr_rx(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (CI_N - (ci_w - ci_r) < n)
        return 0;
    for (i = 0; i < n; i++)
        cdc_in[(ci_w + i) % CI_N] = p[i];
    ci_w += n;
    return n;
}
static uint32_t isr_tx(uint8_t *p, uint32_t max)
{
    uint32_t n = 0;
    while (co_r != co_w && n < max)
        p[n++] = cdc_out[co_r++ % CO_N];
    return n;
}
#endif

int main(void)
{
#if FM1B_CDC
    static uint8_t big[3000], got[3000];
    uint32_t i, n;
    for (i = 0; i < sizeof big; i++)
        big[i] = (uint8_t)(i * 7u + 1u);
    want("not configured: not open, no room, nothing written", !plat_console_open() && plat_console_room() == 0u &&
                                                                 plat_console_write("hi", 2) == 0u && co_w == 0u);
    usb.config = 1;
    want("configured, no DTR: not open", !plat_console_open() && plat_console_write("hi", 2) == 0u);
    cdc.dtr = 1;
    usb_cdc_on = 0;
    want("the console not presented: not open", !plat_console_open() && plat_console_room() == 0u);
    usb_cdc_on = 1;
    want("configured, presented, DTR: open, the whole ring free", plat_console_open() && plat_console_room() == CO_N);
    want("nothing from the computer: -1", plat_console_read() == -1);

    isr_rx((const uint8_t *)"\x00\x41\xFF\x80", 4);
    want("bytes read in order, 0x00 and 0xFF as values",
         plat_console_read() == 0x00 && plat_console_read() == 0x41 && plat_console_read() == 0xFF &&
         plat_console_read() == 0x80 && plat_console_read() == -1);

    n = plat_console_write("hello", 5);
    want("a write takes its bytes, the room shrinks", n == 5u && plat_console_room() == CO_N - 5u);
    n = isr_tx(got, 63);
    want("the interrupt sends them as written", n == 5u && !memcmp(got, "hello", 5) && plat_console_room() == CO_N);

    n = plat_console_write(big, sizeof big);
    want("a write over the room takes the room only (never waits)", n == CO_N && plat_console_room() == 0u);
    want("a full ring takes nothing", plat_console_write(big, 1) == 0u);
    n = isr_tx(got, sizeof got);
    want("what it took is sent, in order", n == CO_N && !memcmp(got, big, CO_N));

    co_w = co_r = 0xFFFFFF00u;                   /* the indexes about to wrap */
    ci_w = ci_r = 0xFFFFFFF0u;
    n = plat_console_write(big, 1000);
    want("across the wrap: the room right", n == 1000u && plat_console_room() == CO_N - 1000u);
    n = isr_tx(got, sizeof got);
    want("across the wrap: the bytes right", n == 1000u && !memcmp(got, big, 1000));
    isr_rx(big, 64);
    for (i = 0, n = 0; i < 64u; i++)
        n += plat_console_read() == big[i];
    want("reading across the wrap", n == 64u && plat_console_read() == -1);
    want("each move published (a barrier around the ring's index)", publishes > 0u);

    cdc.dtr = 0;
    want("the computer closes the port: not open, nothing written", !plat_console_open() &&
                                                                    plat_console_write("x", 1) == 0u);
    switches = 0;
    plat_console_present(0);
    want("present(0) switches the function off", switches == 1u && usb_cdc_on == 0u);
    plat_console_present(5);
    want("present(non-zero) switches it on", switches == 2u && usb_cdc_on == 1u);
#else
    plat_console_present(1);
    want("FM1B_CDC 0: never open, nothing read, no room, nothing written (present changes nothing)",
         !plat_console_open() && plat_console_read() == -1 && plat_console_room() == 0u &&
         plat_console_write("hi", 2) == 0u && !plat_console_open());
#endif
    printf(fails ? "console: %d FAILED\n" : "console: all passed\n", fails);
    return fails != 0;
}
