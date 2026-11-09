/* SPDX-License-Identifier: MPL-2.0 */
/* The resource region (fm1.toml resources = FILE, FM1B_RES): read-only data an app does not need to start (LOOM-8's
 * 909 samples), kept out of the image in sectors of the data area of the app's choosing (app.h APP_RES_BASE,
 * APP_RES_SECTORS) and read in place (plat_res). After Melodee's resource region (firmware/src/res.c, by Kerem Kilic,
 * Ellic Studio; used under MPL-2.0 with permission): the build knows its payload's length and CRC-32 (build.py writes
 * fm1_res.h from the file), so a firmware finds whether the region holds its own; the host writes it after an update
 * (tools/fm1_resources.py, fm1_install.py, the installer page), and an update that keeps the payload keeps the region.
 * Missing, torn or another build's: plat_res 0, and the app does without (its own words for it).
 *
 * The transfer is the restore's (backup.c): the host's RES_REQ (length, CRC-32), then the device pulls the payload
 * with read requests (ota_read; addresses are the payload's offsets) and reports by reading RS_OK_ADDR or
 * RS_FAIL_ADDR | code. The device checks the request against its build first, so nothing is erased for another
 * build's file; one that holds it already answers at once.
 *   APP_RES_BASE        header: magic, length, CRC-32 of the payload, header CRC (written last: the commit record)
 *   APP_RES_BASE + 32   the payload, read in place through the unencrypted window (plat_data)
 * A pooled store whose move from the A/B pairs has not finished (storage.c st_pool_boot) may still have projects in
 * the region's sectors: no write until it has. */
#define RES_REQ 0x44u                               /* the host's request (ota.c framing; backup.c's are 0x41..0x43) */
#define RES_HEAD 32u
#define RES_MAGIC 0x31534552u                       /* "RES1" */
#define RES_E_BUILD 20                              /* another build's resources */
#define RES_E_STORE 21                              /* the store's move into its pool not done (the next boot: again) */
#define RES_E_CHECK 22                              /* the payload as written does not check */
typedef struct { uint32_t magic, len, crc, rsv[4], hcrc; } res_hdr_t;
_Static_assert(sizeof(res_hdr_t) == RES_HEAD, "the resource commit record");
_Static_assert(FM1B_RES_LEN && RES_HEAD + FM1B_RES_LEN <= APP_RES_SECTORS * ST_SECTOR, "the resources fit the region");
_Static_assert(APP_RES_BASE >= PLAT_DATA_LO && APP_RES_BASE + APP_RES_SECTORS * ST_SECTOR <= PLAT_DATA_HI &&
               !(APP_RES_BASE & 0xFFFu), "the region: whole sectors of the data area");
#ifdef APP_STORE_POOL
_Static_assert(APP_RES_BASE >= APP_STORE_POOL_BASE + APP_STORE_POOL_N * ST_SECTOR ||
               APP_RES_BASE + APP_RES_SECTORS * ST_SECTOR <= APP_STORE_POOL_BASE, "the region is not the store's pool");
#endif

static uint8_t res_ok;                              /* the region holds this build's payload (res_check) */

static int res_valid(void)
{
    res_hdr_t h;
    const uint8_t *p = plat_data(APP_RES_BASE + RES_HEAD);
    if (!p || st_read(APP_RES_BASE, &h, sizeof h) || h.magic != RES_MAGIC || h.hcrc != st_crc32(&h, sizeof h - 4u) ||
        h.len != FM1B_RES_LEN || h.crc != FM1B_RES_CRC)
        return 0;
    return st_crc32(p, FM1B_RES_LEN) == FM1B_RES_CRC; /* the payload as the app reads it */
}
static void res_check(void) { res_ok = (uint8_t)res_valid(); }

const uint8_t *plat_res(uint32_t *len)
{
    if (len)
        *len = res_ok ? FM1B_RES_LEN : 0u;
    return res_ok ? plat_data(APP_RES_BASE + RES_HEAD) : 0;
}

/* the payload's bytes at off (RAM), split at the NOR's 256-byte pages */
static int res_prog(uint32_t off, const uint8_t *src, uint32_t n)
{
    uint32_t at = APP_RES_BASE + RES_HEAD + off;
    while (n) {
        uint32_t k = 256u - (at & 255u);
        if (k > n)
            k = n;
        if (st_prog(at, src, k))
            return -1;
        at += k, src += k, n -= k;
    }
    return 0;
}

/* the host's RES_REQ: its payload of len bytes, CRC-32 crc. 0 (written, or there already) or an RS_E_* / RES_E_*
 * code; the host is told either way (rs_note). *begun: the screen was taken */
static int res_rx(uint32_t len, uint32_t crc, int *begun)
{
    uint32_t off, s, nsec = (RES_HEAD + FM1B_RES_LEN + ST_SECTOR - 1u) / ST_SECTOR;
    res_hdr_t h;
    int rc = 0;
    *begun = 0;
    if (len != FM1B_RES_LEN || crc != FM1B_RES_CRC)
        rc = RES_E_BUILD;
    else if (res_ok)
        rc = 0;                                     /* (there already: nothing to do) */
    else if (rs_playing())
        rc = RS_E_PLAYING;
#ifdef APP_STORE_POOL
    else if (st_pool_scan != APP_STORE_POOL_N)
        rc = RES_E_STORE;
#endif
    else {
        *begun = 1;
        rs_begin();                                 /* (the app's sounds stopped: none reads the region now) */
        rs_screen("RESOURCES", "0%");
        res_ok = 0;
        for (s = 0; s < nsec && !rc; s++)
            if (st_erase(APP_RES_BASE + s * ST_SECTOR))
                rc = RS_E_ERASE;
        for (off = 0; off < len && !rc; off += 512u) {
            uint32_t n = len - off > 512u ? 512u : len - off;
            if (rs_fetch(off, bu_buf, n))
                rc = RS_E_FETCH;
            else if (res_prog(off, bu_buf, n))
                rc = RS_E_PROG;
            else if (!(off & 0x1FFFu))
                rs_show(off * 100u / len);
        }
        if (!rc) {
            const uint8_t *p = plat_data(APP_RES_BASE + RES_HEAD);
            if (!p || st_crc32(p, len) != crc)
                rc = RES_E_CHECK;
        }
        if (!rc) {
            memset(&h, 0, sizeof h);
            h.magic = RES_MAGIC, h.len = len, h.crc = crc;
            h.hcrc = st_crc32(&h, sizeof h - 4u);
            if (st_prog(APP_RES_BASE, &h, sizeof h))
                rc = RS_E_PROG;
        }
        res_check();
        if (!rc && !res_ok)
            rc = RES_E_CHECK;
        if (!rc)
            rs_show(100);
    }
    rs_note(rc ? RS_FAIL_ADDR | (uint32_t)rc : RS_OK_ADDR);
    return rc;
}
