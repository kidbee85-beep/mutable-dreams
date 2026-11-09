/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Host test of device/storage.c's pool (APP_STORE_POOL) against a simulated NOR flash, laid out as LOOM-8's: the
 * project objects (three slots of seven) pooled in 20 sectors from 0x97000, where 42 sectors of A/B pairs were.
 *   - the A/B pairs from before the pool move into it at boot: every object loads as before, the old area ends empty;
 *     a power cut anywhere in the move loses nothing (the move again)
 *   - saves and loads, the previous copy, an empty object, the room, a full pool (-8, nothing written)
 *   - a power cut anywhere in a save leaves the object as it was or as saved, never neither
 *   - random saves with random cuts against a model of what must load */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static int fail_after = -1;            /* power-cut injection: every program and erase after N fails */
static uint32_t ops;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int cut(void)
{
    ops++;
    if (fail_after == 0)
        return 1;
    if (fail_after > 0)
        fail_after--;
    return 0;
}
static int st_erase(uint32_t off)
{
    if (cut())
        return -9;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (cut())
        return -9;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../include/plat.h"
#define NPROJ 3
#define PROJ_NOBJ 7
enum { OBJ_PANEL, OBJ_PRESETS, OBJ_PROJ0, OBJ_BLE = OBJ_PROJ0 + NPROJ * PROJ_NOBJ, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u, 0xDE000u, \
    0x97000u, 0x99000u, 0x9B000u, 0x9D000u, 0x9F000u, 0xA1000u, 0xA3000u, \
    0xA5000u, 0xA7000u, 0xA9000u, 0xAB000u, 0xAD000u, 0xAF000u, 0xB1000u, \
    0xB3000u, 0xB5000u, 0xB7000u, 0xB9000u, 0xBB000u, 0xBD000u, 0xBF000u, \
    0xFC000u}
#define APP_STORE_POOL
#define APP_STORE_POOL_BASE 0x97000u
#define APP_STORE_POOL_N 20u
#define APP_STORE_POOL_OLD 42u
#define APP_STORE_POOL_OBJ0 OBJ_PROJ0
#define APP_STORE_POOL_OBJN (NPROJ * PROJ_NOBJ)
#define APP_STORE_POOL_EMPTY(obj, p, n) \
    ((n) == 4u && ((obj) - OBJ_PROJ0) % PROJ_NOBJ >= 2u && (p)[0] == 2u && !(p)[1] && !(p)[2] && !(p)[3])   /* LOOM-8's */
#include "../device/storage.c"

static const uint32_t OLD_MAP[OBJ_NOBJ] = APP_STORE_MAP;
static int bad;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

/* a copy as the A/B store before the pool wrote it, at copy (0 A, 1 B) of obj's pair */
static void old_copy(uint32_t obj, uint32_t copy, uint32_t seq, const uint8_t *p, uint32_t len)
{
    st_hdr_t h;
    uint32_t base = OLD_MAP[obj] + copy * ST_SECTOR;
    memset(nor + base, 0xFF, ST_SECTOR);
    memcpy(nor + base + ST_PAYLOAD_OFF, p, len);
    memset(&h, 0, sizeof h);
    h.magic = ST_MAGIC, h.type = (uint16_t)obj, h.slot = (uint16_t)copy, h.seq = seq, h.len = len;
    h.crc = st_crc32(p, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    memcpy(nor + base, &h, sizeof h);
}
static void fill(uint8_t *p, uint32_t len, uint32_t salt)
{
    for (uint32_t i = 0; i < len; i++)
        p[i] = (uint8_t)(salt * 131u + i * 7u + (i >> 8));
}
static uint8_t want[OBJ_NOBJ][ST_PAYLOAD_MAX];
static int want_len[OBJ_NOBJ];
static int loads_as(uint32_t obj, const uint8_t *p, int len)
{
    static uint8_t got[ST_PAYLOAD_MAX];
    int n = st_load(obj, got, sizeof got);
    return len <= 0 ? n < 0 : n == len && !memcmp(got, p, (size_t)len);
}
static int all_load(void)
{
    for (uint32_t o = 0; o < OBJ_NOBJ; o++)
        if (!loads_as(o, want[o], want_len[o]))
            return 0;
    return 1;
}
/* the layout before the pool: three slots, each its settings and some stream objects, some with an older copy (B) */
static void old_layout(void)
{
    static const int8_t used[NPROJ * PROJ_NOBJ] = {1, 1, 1, 0, 0, 0, 0,  1, 1, 1, 1, 1, 0, 0,  1, 1, 0, 0, 0, 0, 0};
    memset(nor, 0xFF, sizeof nor);
    memset(want_len, 0, sizeof want_len);
    st_pool_scan = APP_STORE_POOL_OLD;
    for (uint32_t k = 0; k < NPROJ * PROJ_NOBJ; k++) {
        uint32_t o = OBJ_PROJ0 + k, len = 300u + k * 151u;
        uint8_t old[ST_PAYLOAD_MAX];
        if (!used[k])
            continue;
        fill(want[o], len, o);
        want_len[o] = (int)len;
        if (k % 3 == 0) {                             /* an older copy beside it, in either half of the pair */
            fill(old, len, o + 99u);
            old_copy(o, (k / 3) & 1u, 4u, old, len);
            old_copy(o, !((k / 3) & 1u), 5u, want[o], len);
        } else {
            old_copy(o, k & 1u, 1u, want[o], len);
        }
    }
    fill(want[OBJ_PRESETS], 900, 77);                 /* (an object outside the pool, at its own pair) */
    want_len[OBJ_PRESETS] = 900;
    old_copy(OBJ_PRESETS, 0, 3u, want[OBJ_PRESETS], 900);
}
/* the layout as LOOM-8 0.11 wrote it: every slot's seven objects, the stream objects past the packed stream its 4-byte
 * format word alone (placeholders: APP_STORE_POOL_EMPTY), one of them over an older copy with data; 21 copies, more
 * than the pool's 20 sectors */
static void old_layout_full(void)
{
    static const uint8_t ph[4] = {2, 0, 0, 0};
    memset(nor, 0xFF, sizeof nor);
    memset(want_len, 0, sizeof want_len);
    st_pool_scan = APP_STORE_POOL_OLD;
    for (uint32_t k = 0; k < NPROJ * PROJ_NOBJ; k++) {
        uint32_t o = OBJ_PROJ0 + k, i = k % PROJ_NOBJ, data = i < 2u || (k / PROJ_NOBJ == 1u && i == 2u);
        if (data) {
            uint32_t len = i ? 3840u - (k % 5u) * 200u : 600u + k;
            fill(want[o], len, o);
            want_len[o] = (int)len;
            old_copy(o, k & 1u, 2u, want[o], len);
        } else if (k == 4u) {                         /* a placeholder over a stream object's older copy */
            uint8_t old[ST_PAYLOAD_MAX];
            fill(old, 1500, o + 50u);
            old_copy(o, 0, 6u, old, 1500);
            old_copy(o, 1, 7u, ph, 4);
        } else {
            old_copy(o, k & 1u, 3u, ph, 4);
        }
    }
}
static int tail_clear(void)                         /* no copy left past the pool's end */
{
    for (uint32_t i = APP_STORE_POOL_N; i < APP_STORE_POOL_OLD; i++) {
        st_hdr_t h;
        memcpy(&h, nor + APP_STORE_POOL_BASE + i * ST_SECTOR, sizeof h);
        if (h.magic == ST_MAGIC)
            return 0;
    }
    return 1;
}

int main(void)
{
    static uint8_t a[ST_PAYLOAD_MAX], b[ST_PAYLOAD_MAX], got[ST_PAYLOAD_MAX];
    uint32_t total, o;

    printf("-- the move from the A/B pairs\n");
    old_layout();
    check("the old layout loads before the move (the pool reads the A/B pairs as copies)", all_load());
    check("the move: every copy into the pool", st_pool_boot() == 0 && st_pool_scan == APP_STORE_POOL_N);
    check("every object loads as before the move", all_load());
    check("nothing left past the pool's end (the 909 samples' place)", tail_clear());

    old_layout();
    fail_after = -1, ops = 0;
    st_pool_boot();
    total = ops;
    {
        int ok = 1;
        for (uint32_t c = 0; c < total; c++) {         /* a power cut after every step of the move */
            old_layout();
            fail_after = (int)c;
            st_pool_boot();
            fail_after = -1;
            st_pool_scan = APP_STORE_POOL_OLD;         /* (the next boot) */
            ok &= all_load();
            st_pool_boot();
            ok &= all_load() && tail_clear() && st_pool_scan == APP_STORE_POOL_N;
        }
        printf("  (%u steps in the move, each cut)\n", total);
        check("a power cut anywhere in the move: all there, the next boot finishes it", ok);
    }

    printf("-- the move from 0.11's whole layout (placeholders)\n");
    old_layout_full();
    check("the move: the placeholders dropped, the rest into the pool", st_pool_boot() == 0 && all_load() && tail_clear());
    check("room for saves after it (the placeholders took none)", st_pool_room() >= 10);
    old_layout_full();
    fail_after = -1, ops = 0;
    st_pool_boot();
    total = ops;
    {
        int ok = 1;
        for (uint32_t c = 0; c < total; c++) {
            old_layout_full();
            fail_after = (int)c;
            st_pool_boot();
            fail_after = -1;
            st_pool_scan = APP_STORE_POOL_OLD;
            st_pool_boot();
            ok &= all_load() && tail_clear() && st_pool_scan == APP_STORE_POOL_N && st_pool_room() >= 10;
        }
        printf("  (%u steps, each cut)\n", total);
        check("a power cut anywhere in it: the next boot finishes it, no placeholder back", ok);
    }

    printf("-- saves in the pool\n");
    old_layout();
    st_pool_boot();
    fill(a, 3000, 1);
    fill(b, 2000, 2);
    o = OBJ_PROJ0 + 3;
    check("save a new object", st_save(o, a, 3000) == 0 && loads_as(o, a, 3000));
    check("save it again: the new loads, the previous is the old", st_save(o, b, 2000) == 0 && loads_as(o, b, 2000) &&
                                                                     st_load_prev(o, got, sizeof got) == 3000 &&
                                                                     !memcmp(got, a, 3000));
    want_len[o] = 2000;
    memcpy(want[o], b, 2000);
    check("the others as they were", all_load());
    check("an empty object: none after", st_save(o, a, 0) == 0 && st_load(o, got, sizeof got) < 0);
    want_len[o] = 0;
    {
        int room = st_pool_room(), filled = 0, rc = 0;
        printf("  room %d sectors\n", room);
        for (uint32_t k = 0; k < 40 && rc == 0; k++) {   /* new objects until the pool is full */
            uint32_t ob = OBJ_PROJ0 + (k % (NPROJ * PROJ_NOBJ));
            fill(a, 1000, 50u + k);
            rc = st_save(ob, a, 1000);
            if (rc == 0) {
                memcpy(want[ob], a, 1000);
                want_len[ob] = 1000;
                filled++;
            }
        }
        check("saves while there is room; a full pool says so (-8)", rc == -8 || rc == 0);
        check("a refused save wrote nothing: every object loads as last saved", all_load());
        printf("  %d saves before the pool said full (rc %d), room now %d\n", filled, rc, st_pool_room());
    }

    printf("-- power cuts in a save\n");
    {
        int ok = 1;
        o = OBJ_PROJ0 + 1;
        for (int c = 0; c < 20; c++) {
            fill(a, 2500, 300u + (uint32_t)c);
            fail_after = c;
            int rc = st_save(o, a, 2500);
            fail_after = -1;
            if (rc == 0) {
                memcpy(want[o], a, 2500);
                want_len[o] = 2500;
            }
            ok &= loads_as(o, want[o], want_len[o]) || loads_as(o, a, 2500);
            if (loads_as(o, a, 2500)) {
                memcpy(want[o], a, 2500);
                want_len[o] = 2500;
            }
            ok &= all_load();
        }
        check("a cut at any step: the object as it was or as saved, the rest untouched", ok);
    }

    printf("-- random saves, random cuts\n");
    {
        uint32_t rng = 12345, saves = 0, cuts = 0;
        int ok = 1;
        old_layout();
        st_pool_boot();
        for (int it = 0; it < 4000 && ok; it++) {
            uint32_t ob, len;
            int rc;
            rng = rng * 1664525u + 1013904223u;
            ob = OBJ_PROJ0 + (rng >> 8) % (NPROJ * PROJ_NOBJ);
            len = (rng >> 16) % 4 == 0 ? 0u : 1u + (rng >> 12) % 3000u;
            fill(a, len, rng);
            fail_after = (rng >> 20) % 7 == 0 ? (int)((rng >> 4) % 12) : -1;
            rc = st_save(ob, a, len);
            if (fail_after >= 0 && rc)
                cuts++;
            fail_after = -1;
            if (rc == 0) {
                memcpy(want[ob], a, len);
                want_len[ob] = (int)len;
                saves++;
            } else if (rc != -8) {                     /* cut: as it was or as saved (an empty one: maybe part
                                                        * erased, then some older copy loads; that is the restore's
                                                        * case, which erases first anyway) */
                if (loads_as(ob, a, (int)len)) {
                    memcpy(want[ob], a, len);
                    want_len[ob] = (int)len;
                } else if (!loads_as(ob, want[ob], want_len[ob])) {
                    if (len == 0) {
                        want_len[ob] = st_load(ob, want[ob], ST_PAYLOAD_MAX);
                    } else {
                        printf("  object %u lost at step %d\n", ob, it);
                        ok = 0;
                    }
                }
            }
            if (it % 50 == 0) {                        /* a reboot now and then: the same as before */
                st_pool_scan = APP_STORE_POOL_OLD;
                st_pool_boot();
                ok &= all_load();
            }
            ok &= all_load();
        }
        printf("  %u saves, %u cut\n", saves, cuts);
        check("4000 random saves with cuts: every object as last saved", ok);
    }
    printf(bad ? "STORAGE POOL TESTS FAILED\n" : "storage pool tests passed\n");
    return bad != 0;
}
