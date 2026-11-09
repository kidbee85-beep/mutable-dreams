/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of bsp/chip/ota.c: a fake M-UPGRADE serves a real .fwsc over
 * the SysEx protocol (pack7, cmd 0x30 reads, 0xE0000000 -> "success") and a
 * simulated NOR plays the flash.
 *   cc -o ota_test tests/ota_test.c && ./ota_test FILE.fwsc [MBOOT.fwsc [STOCK.fwsc...]]
 * MBOOT.fwsc: an m-bootloader installer package (ota.bin only, no flash.bin); its bootloader
 * (the plain .boot container) is read from the same name with .boot for .fwsc, for the boot area rule.
 * STOCK.fwsc... (optional, never committed): M-VAVE's FM-1 packages (V15, V16), staged over any head,
 * refused with a damaged boot area
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FELUCCA_ID "FM-1_9001613"   /* a release's (id_prefix + XXYYZZ): the plain handshake answers FM-1_900 */
#define FELUCCA_OTA_DRYRUN 0

static uint8_t nor[0x100000];
static uint8_t *logical;
static size_t logical_len;
static uint8_t rx[1024];                 /* one pending host frame (7-bit bytes) */
static uint32_t rx_len, rx_full;
static uint32_t now_ms, requests, ident_replies, mute_after = 0xFFFFFFFFu, bad_erase;
static int committed;
static uint8_t committed_parm[112];
static int32_t last_code;
static uint8_t *corrupt_at;              /* flip a served byte (loader body) */

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}

static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static void host_reply(const uint8_t *msg, uint32_t n)       /* queue msg as a device-bound frame */
{
    rx_len = pack7(msg, n, rx);
    rx_full = 1;
}

/* ---- hooks ---- */
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t d, addr, len, i, s = 0;
    if (p[0] != 0xF0 || p[n - 1] != 0xF7) { printf("bad wire framing\n"); exit(1); }
    d = unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) { ident_replies++; return 0; }
    if (d != 15 || u[2] != 0x30) { printf("unexpected device msg cmd %02x len %u\n", u[2], d); exit(1); }
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    if (++requests > mute_after)
        return 0;                                         /* host gone */
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0;
    m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr == 0xE0000000u) {
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
        if (corrupt_at && corrupt_at >= logical + addr && corrupt_at < logical + addr + len)
            m[14 + (corrupt_at - (logical + addr))] ^= 0x01;
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    host_reply(m, 15 + len);
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!rx_full) return 0;
    *p = rx; *n = rx_len;
    return 1;
}
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms += 1; }
static int ota_erase(uint32_t off)
{
    if (off < 0xE0000u || off >= 0xE5000u || (off & 0xFFFu)) { bad_erase++; return -8; }
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int ota_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off < 0xE0000u || off + n > 0xE5000u) { bad_erase++; return -8; }
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static int ota_fread(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { if (step == 9) last_code = code; }
static void ota_commit(const uint8_t *parm) { committed = 1; memcpy(committed_parm, parm, 112); }

#include "../bsp/chip/ota.c"

static uint32_t ota_body_off(void)                  /* ota.bin data + 0x20 in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, logical, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 100) return ota_rd32(e + 8) + 0x20u;
    }
    return 0;
}

static int check(const char *what, int ok)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static int area_erased(void)
{
    uint32_t i;
    for (i = 0xE0000; i < 0xE5000; i++) if (nor[i] != 0xFF) return 0;
    return 1;
}

static void reset_dev(void)
{
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, logical + 0x400, 0x4000);                 /* device flash head = package flash.bin head */
    rx_full = 0; requests = 0; ident_replies = 0; committed = 0; last_code = 0;
    mute_after = 0xFFFFFFFFu; corrupt_at = NULL; bad_erase = 0;
}

static int load(const char *path)                  /* the package's logical image -> logical */
{
    FILE *f;
    uint8_t *raw;
    size_t n, i;
    f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    raw = malloc(0x200000);
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    free(logical);
    logical = malloc(n);
    for (i = 0; i < 20; i++) memcpy(logical + i * 47, raw + i * 48, 47);   /* drop the marker bytes */
    memcpy(logical + 20 * 47, raw + 20 * 48, n - 20 * 48);
    logical_len = n - 20;
    free(raw);
    return 0;
}

static uint8_t b_scratch[512];

int main(int argc, char **argv)
{
    int bad = 0, rc;
    size_t i;
    if (argc < 2) { printf("usage: ota_test FILE.fwsc [MBOOT.fwsc [STOCK.fwsc...]]\n"); return 2; }
    if (load(argv[1]))
        return 2;

    /* handshake outside a session */
    reset_dev();
    {
        uint8_t m[7] = {0, 0x59, 0x11, 0, 0, 0, 0xFF};
        host_reply(m, 7);
        ota_service();
        bad += check("handshake answered", ident_replies == 1);
        bad += check("plain query: FM-1_900 (M-UPGRADE 3.11: NAME_ + 1-3 digits)",
                     ota_msg[2] == 0x11 && !memcmp(ota_msg + 6, "FM-1_900", 8) && ota_msg[6 + 8] == 0);
    }
    reset_dev();
    {
        uint8_t m[8] = {0, 0x59, 0x11, 1, 0, 0, 1, 0xFE};   /* body 01: the whole identity */
        host_reply(m, 8);
        ota_service();
        bad += check("asked with 01: answered", ident_replies == 1);
        bad += check("asked with 01: the whole identity " FELUCCA_ID,
                     ota_msg[2] == 0x11 && !memcmp(ota_msg + 6, FELUCCA_ID, sizeof FELUCCA_ID - 1) &&
                     ota_msg[6 + sizeof FELUCCA_ID - 1] == 0);
    }

    /* full session */
    reset_dev();
    rc = ota_session();
    printf("  %u requests\n", requests);
    bad += check("session commits", rc == 0 && committed);
    bad += check("headers + loader + confirm, no head reads", requests >= 10 && requests <= 30);
    {
        uint8_t *h = nor + 0xE0000, *p = committed_parm;
        bad += check("staged body == the package's loader", !memcmp(nor + 0xE0020, logical + ota_body_off(), 64));
        bad += check("LOADER.BIN head CRC",
                     (h[0] | h[1] << 8) == ota_crc16(h + 2, 30, 0) && !memcmp(h + 16, "LOADER.BIN", 10));
        bad += check("record at 0xE4F00 == RAM record", !memcmp(nor + 0xE4F00, p, 112));
        bad += check("record CRC / type / magic / ota_addr",
                     (p[0] | p[1] << 8) == ota_crc16(p + 2, 78, 0) && (p[2] | p[3] << 8) == 0x5A0D &&
                     (p[6] | p[7] << 8) == 0x5441 && ota_rd32(p + 72) == 0xE0000);
        bad += check("nothing written outside 0xE0000..0xE4FFF", bad_erase == 0);
    }
    /* boot cleanup erases it */
    bad += check("boot cleanup erases the area", ota_boot_cleanup() == 1 && area_erased());
    bad += check("second boot: nothing to do", ota_boot_cleanup() == 0);

    /* host disappears in the middle */
    reset_dev();
    mute_after = 4;
    rc = ota_session();
    bad += check("host gone -> no commit, area clean", rc < 0 && !committed && area_erased());

    /* host never confirms */
    reset_dev();
    mute_after = 0xFFFFFFFFu;
    {
        uint32_t total;
        rc = ota_session();
        total = requests;                                  /* requests of a full run incl. confirm */
        reset_dev();
        mute_after = total - 1;                            /* answer everything but the confirmation */
        rc = ota_session();
        bad += check("no 'success' -> -13, no commit, area clean", rc == -13 && !committed && area_erased());
    }

    /* any well-made writer: m-bootloader's installer (no flash.bin), staged as it is */
    if (argc > 2) {
        static uint8_t tpl[0x4000], img[0x4000], boot[0x4000];
        char path[1024];
        FILE *f;
        size_t blen, plen = strlen(argv[2]);
        memcpy(tpl, logical + 0x400, sizeof tpl);          /* our package's head: a flash header to build on */
        if (load(argv[2]))
            return 2;
        reset_dev();
        rc = ota_session();
        bad += check("m-bootloader installer commits", rc == 0 && committed);
        bad += check("staged body == its installer", !memcmp(nor + 0xE0020, logical + ota_body_off(), 64));
        bad += check("m-bootloader: nothing outside 0xE0000..0xE4FFF", bad_erase == 0);
        reset_dev();
        corrupt_at = logical + ota_body_off() + 0x100;     /* a body byte: CRC mismatch */
        rc = ota_session();
        bad += check("corrupt writer body -> no commit, area clean", rc < 0 && !committed && area_erased());
        reset_dev();
        corrupt_at = logical + ota_body_off() - 0x20 + 8;  /* its head: not where the SPL runs it */
        rc = ota_session();
        bad += check("malformed writer head -> -7", rc == -7 && !committed && area_erased());

        /* the boot area rule, on heads built here: m-bootloader's container (SPEC 11.2) in our flash header */
        snprintf(path, sizeof path, "%.*s.boot", (int)(plen > 5 ? plen - 5 : plen), argv[2]);
        f = fopen(path, "rb");
        if (!f) { perror(path); return 2; }
        blen = fread(boot, 1, sizeof boot, f);
        fclose(f);
        ota_deadline = 0;                                  /* no session: no deadline */
        {
            static const char *board[] = { "fm1", "smk37" };
            uint32_t v;
            for (v = 0; v < 3u; v++) {
                uint8_t c[0x4000], rec[32];
                memcpy(c, boot, blen);
                if (v == 1u) {                              /* another board's, every CRC right */
                    memset(c + 16 + 0x48, 0, 8);
                    memcpy(c + 16 + 0x48, board[1], strlen(board[1]));
                }
                ota_wr16(c + 12, ota_crc16(c + 16, (uint32_t)blen - 16u, 0));
                ota_wr16(c + 14, ota_crc16(c, 14, 0));
                memcpy(img, tpl, sizeof img);
                memcpy(rec, img + 32, 32);
                ota_jl_enc(rec, 32);
                ota_wr16(rec + 2, ota_crc16(c, (uint32_t)blen, 0));
                ota_wr32(rec + 4, 0xA0u);
                ota_wr32(rec + 8, (uint32_t)blen);
                ota_wr16(rec, ota_crc16(rec + 2, 30, 0));
                ota_jl_enc(rec, 32);
                memcpy(img + 32, rec, 32);
                memset(img + 0xA0, 0xFF, sizeof img - 0xA0);
                memcpy(img + 0xA0, c, blen);
                ota_jl_enc(img + 0xA0, 16);
                ota_jl_enc(img + 0xB0, (uint32_t)blen - 16u);
                if (v == 2u)
                    img[0xA0 + 0x800] ^= 0x01;              /* a damaged payload */
                logical = img;
                logical_len = sizeof img;
                rc = ota_boot_valid(0, sizeof img, b_scratch);
                bad += check(v == 0u ? "m-bootloader boot area for the FM-1: valid" :
                             v == 1u ? "m-bootloader boot area for the SMK-37 -> -5" :
                                       "damaged boot area -> -5", v == 0u ? rc == 0 : rc == -5);
            }
            memcpy(img, tpl, sizeof img);                   /* ours keeps the SDK's file unciphered: */
            logical = img;                                  /* never written (Felucca's writer is exempt) */
            rc = ota_boot_valid(0, sizeof img, b_scratch);
            bad += check("our packages' head: not one to write -> -5", rc == -5);
            logical = NULL;
        }

        /* M-VAVE's packages (optional, never committed): staged over any head, m-bootloader's too */
        for (i = 3; i < (size_t)argc; i++) {
            char what[96];
            if (load(argv[i]))
                return 2;
            reset_dev();
            nor[0x1000] ^= 0x01;                           /* the device's head differs from the package's */
            rc = ota_session();
            snprintf(what, sizeof what, "%s over another head commits", argv[i] + (strrchr(argv[i], '/') ? strrchr(argv[i], '/') - argv[i] + 1 : 0));
            bad += check(what, rc == 0 && committed);
            reset_dev();
            corrupt_at = logical + 0x400 + 0xA0 + 0x800;   /* its boot area's payload */
            rc = ota_session();
            bad += check("  ... with a damaged boot area -> -5", rc == -5 && !committed && area_erased());
        }
    }

    printf("%s\n", bad ? "OTA TEST FAILED" : "ota test passed");
    return bad != 0;
}
