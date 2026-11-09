/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The SysEx test app: see app.h */
static uint32_t sx_frames, sx_other;
static volatile uint32_t sx_bytes, sx_starts;
void app_sysex_byte(uint8_t b)
{
    sx_bytes++;
    sx_starts += b == 0xF0u;
}
static uint8_t sx_reply[700];
static uint32_t sx_sent, sx_stream;                             /* the last flood's frames sent whole */
static void sx_put21(uint8_t *b, uint32_t v)         /* 3 x 7 bits, the lowest first */
{
    b[0] = (uint8_t)(v & 127u), b[1] = (uint8_t)(v >> 7 & 127u), b[2] = (uint8_t)(v >> 14 & 127u);
}

static uint32_t sx_boots;
void app_init(void) { sx_boots++; }
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}
void app_panic(void) {}
void app_ui_init(void) { lcd_fill(0, 0, 240, 240, C_BLACK); }
void app_frame(void) {}
void app_input_only(void) {}
void app_say(const char *a, const char *b) { (void)a, (void)b; }

int app_sysex(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    if (n < 4u || p[0] != 0x7Du || p[1] != 0x53u || p[2] != 0x58u) {
        sx_other++;
        return 0;
    }
    sx_frames++;
    if (p[3] == 1u && n + 2u <= sizeof sx_reply) {   /* echo */
        sx_reply[0] = 0xF0;
        for (i = 0; i < n; i++)
            sx_reply[1u + i] = p[i];
        sx_reply[1u + n] = 0xF7;
        plat_sysex_send(sx_reply, n + 2u);
    } else if (p[3] == 2u) {                         /* room */
        uint32_t v[3] = {plat_sysex_room(), plat_sysex_queued(), plat_usb_session()}, k = 5;
        sx_reply[0] = 0xF0, sx_reply[1] = 0x7D, sx_reply[2] = 0x53, sx_reply[3] = 0x58, sx_reply[4] = 2;
        for (i = 0; i < 3u; i++, k += 3u)
            sx_put21(sx_reply + k, v[i]);
        sx_reply[k++] = 0xF7;
        plat_sysex_send(sx_reply, k);
    } else if (p[3] == 3u && n >= 5u) {              /* flood */
        uint32_t sent = 0;
        sx_reply[0] = 0xF0, sx_reply[1] = 0x7D, sx_reply[2] = 0x53, sx_reply[3] = 0x58, sx_reply[4] = 3;
        for (i = 5; i < 62u; i++)
            sx_reply[i] = 0;
        sx_reply[62] = 0xF7;
        for (i = 0; i < p[4]; i++)
            sent += plat_sysex_send(sx_reply, 63u) == 0;
        sx_sent = sent;
    } else if (p[3] == 4u && n >= 5u) {              /* stream */
        static const uint8_t HEAD[5] = {0xF0, 0x7D, 0x53, 0x58, 4}, END = 0xF7;
        uint8_t b[7];
        uint32_t k, at = 0;
        int rc = plat_sysex_begin() || plat_sysex_put(HEAD, 5u);
        for (i = 0; i < p[4] && !rc; i++) {
            for (k = 0; k < 7u; k++)
                b[k] = (uint8_t)(at++ & 127u);
            rc = plat_sysex_put(b, 7u);
        }
        rc |= rc ? 0 : plat_sysex_put(&END, 1u);
        rc |= plat_sysex_end();
        sx_stream = rc != 0;
    } else if (p[3] == 5u) {                         /* restart */
        plat_restart();
    }
    return 1;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "frames"))
        *got = (int)sx_frames;
    else if (!strcmp(what, "bytes"))
        *got = (int)sx_bytes;
    else if (!strcmp(what, "starts"))
        *got = (int)sx_starts;
    else if (!strcmp(what, "boots"))
        *got = (int)sx_boots;
    else if (!strcmp(what, "other"))
        *got = (int)sx_other;
    else if (!strcmp(what, "sent"))
        *got = (int)sx_sent;
    else if (!strcmp(what, "stream"))
        *got = (int)sx_stream;
    else if (!strcmp(what, "room"))
        *got = (int)plat_sysex_room();
    else if (!strcmp(what, "queued"))
        *got = (int)plat_sysex_queued();
    else if (!strcmp(what, "session"))
        *got = (int)plat_usb_session();
    else
        return 0;
    return 1;
}
#endif
