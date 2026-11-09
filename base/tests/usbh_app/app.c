/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The USB host test app: see app.h */
static int uh_on, uh_msg;
static uint32_t uh_prev, uh_lost, uh_in;

static void uh_input(void)
{
    uint32_t b = plat_buttons(), pkt;
    int m, port;
    if ((b & ~uh_prev) & (1u << B_SEL)) {
        uh_on = !uh_on;
        plat_usb_host(uh_on);
    }
    uh_prev = b;
    m = plat_usb_host_msg();
    if (m != USBH_MSG_NONE)
        uh_msg = m;
    if (plat_usb_lost())
        uh_lost++;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xF0u;
        if (port != PORT_USB || (st != 0x90u && st != 0x80u))
            continue;
        uh_in++;
        plat_midi_send(pkt + (12u << 16), TO_USB);   /* (an octave up: the notes the test sends stay under 116) */
    }
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
void app_frame(void) { uh_input(); }
void app_input_only(void) { uh_input(); }
void app_say(const char *a, const char *b) { (void)a, (void)b; }
int app_expect(const char *what, int *got)
{
    const char *nm;
    if (!strcmp(what, "app_msg"))
        *got = uh_msg;
    else if (!strcmp(what, "app_lost"))
        *got = (int)uh_lost;
    else if (!strcmp(what, "app_in"))
        *got = (int)uh_in;
    else if (!strcmp(what, "app_host"))
        *got = uh_on;
    else if (!strcmp(what, "app_name")) {
        for (nm = plat_usb_host_name(), *got = 0; nm[*got]; (*got)++)
            ;
    } else
        return 0;
    return 1;
}
