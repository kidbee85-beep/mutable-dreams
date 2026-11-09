/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Backup and restore over USB (fm1.toml backup = true: FM1B_BACKUP; tools/fm1_backup.py): the app's saved objects
 * (app.h APP_BACKUP_OBJS, in order; never the panel's calibration, which belongs to the unit), over the update's
 * SysEx framing (felucca-bsp ota.c: F0 pack7(00 59 cmd len24 body ~sum(body)) F7). (From Polly's.)
 *
 * The image (the backup file, little-endian):
 *   head  the app's magic (APP_BACKUP_MAGIC), u16 its version (APP_BACKUP_VERSION), u16 the count of objects,
 *         u32 flags (bit 0: taken while playing, changes not yet saved left out), u32 CRC-32 of the 12 bytes before
 *   the records, in APP_BACKUP_OBJS's order: u16 object id, u16 length, u32 CRC-32 of the payload, the payload
 *         (length 0: the object absent; a restore makes it absent)
 *   tail  u32 CRC-32 of everything before
 * (CRC-32: zlib's, as storage.c's st_crc32)
 *
 * Backup (cmd 0x42 from the host, body the app's magic): the device streams the image from its main loop, one frame
 * at a time (cmd 0x42, body: u32 offset + up to BU_CHUNK bytes; 161 wire bytes, 54 packets: it fits the SysEx TX
 * ring whole, so a frame never waits and the notes between frames go out as ever), then cmd 0x43 (u32 length, u32
 * CRC-32 of the whole image, u8 status, u8 flags). It plays on meanwhile. Stopped, what is not saved yet is saved
 * first (app_backup_flush); the app does not save until the stream ends (app_backup_hold). Each object is read from
 * the flash at its turn (one in RAM at a time).
 *
 * Restore (cmd 0x41, body: u32 the image's length, u32 its CRC-32): refused while playing (app_backup_playing: STOP
 * FIRST). Else, as an update does, the device pulls the image with read requests (ota_read; addresses are the image's
 * offsets) and
 *   1. checks the head, then each record: its id, its length, its payload's CRC-32, the image's CRC-32 (the tail and
 *      the host's), the length;
 *   2. stages each object as it comes into the copy storage.c would save it to next (the other of its A/B pair:
 *      erased, the payload programmed, read back and checked) WITHOUT its header: storage.c's header is the commit
 *      record of a copy, so a copy without one does not exist for st_load: the current set stays in charge;
 *   3. everything received and verified (a cut cable, a changed byte, a refused write: stops here, nothing changed):
 *      asks on the screen, RESTORE FROM USB? SEL yes / HOME no (60 s: no), stopped only;
 *   4. yes: commits. A journal record (RS_REC, in the update's staging area, unused but by an update) lists the
 *      headers to write; written and read back first, then the headers, then the record erased. A power loss after
 *      the record is valid is finished at the next boot (rs_boot: the headers written again, idempotent on NOR);
 *      before it, nothing happened. So the set changes whole or not at all.
 *   5. everything reloaded as at power-on (app_backup_restored); the result to the host: a read of RS_OK_ADDR, or
 *      RS_FAIL_ADDR | code. RS_ASK_ADDR is read just before the question (the host says so).
 * RAM: one object's buffer for a restore (bu_buf, 3840 B, the pool; or the app's, APP_BACKUP_BUF), the staging list;
 * the flash does the rest. A backup sends each object from its current copy on the flash, a chunk a frame (saving is
 * held meanwhile; a copy written over anyway fails its record's CRC on a restore, which then changes nothing).
 * An app with a pooled store (APP_STORE_POOL, storage.c) has no second copy of everything to stage into: 2. stages
 * nothing (every record still received and checked, so a wrong or cut backup changes nothing), and 4. erases the
 * pooled objects the backup holds (one it does not, OBJ_BLE's keys say, is kept), then fetches the image again and
 * saves each object (rs_rewrite; the backup is the user's own: a
 * cable pulled in between leaves a partly restored set, and restoring again finishes it). An older backup's
 * placeholders (APP_STORE_POOL_EMPTY) are not written.
 *
 * Hooks (the platform's, unity.c; the app's are app_backup_*, plat.h):
 *   rs_fetch(addr, p, n)  the image's bytes from the host (ota_read)                 0 = ok
 *   rs_note(addr)         a read of a reply address (ota_read of 8 bytes)           0 = the host answered
 *   rs_playing()          the app plays (app_backup_playing)
 *   rs_begin()            a backup to restore: the screen is ours (then rs_show(pct))
 *   rs_show(pct)          progress 0..100
 *   rs_ask()              the question on the screen: 1 yes, 0 no (HOME, 60 s, playing)
 *   rs_reload()           everything from the flash again (app_backup_restored)
 *   rs_rec_erase() rs_rec_prog(off, p, n) rs_rec_read(off, p, n)   the journal record's sector (RS_REC)
 *   bu_room()             a frame fits the TX ring now (it is empty): 1; not yet: 0; USB gone (unplugged,
 *                         suspended, not configured): -1, the backup given up
 *   bu_send(p, n)         one F0..F7 message to the host
 *   bu_flush()            stopped: what is unsaved saved (app_backup_flush)
 *   bu_hold(on)           the app's saving held (1) / free again (0) (app_backup_hold)
 * and storage.c's st_read / st_erase / st_prog, st_current, st_sector; ota.c's ota_now_ms for the stall limit
 * (BU_STALL_MS without a frame going out: the backup given up, the app's saving free again). */
#define RS_REQ 0x41u                                  /* the host's requests (ota.c framing) */
#define BU_REQ 0x42u
#define BU_END 0x43u
#define RS_OK_ADDR 0xC0000000u
#define RS_ASK_ADDR 0xC1000000u
#define RS_FAIL_ADDR 0xCF000000u
#define BU_MAGIC APP_BACKUP_MAGIC
#define BU_VERSION APP_BACKUP_VERSION
#ifndef APP_BACKUP_WHAT
#define APP_BACKUP_WHAT "replaces what is saved"
#endif
static const uint16_t bu_objs[] = APP_BACKUP_OBJS;
#define BU_NOBJ ((uint32_t)(sizeof bu_objs / sizeof bu_objs[0]))
#define BU_HEAD 16u
#define BU_REC 8u
#define BU_CHUNK 128u                                 /* a backup frame's data */
#define BU_STALL_MS 2000u                             /* no frame out for this long: the backup given up */
#define BU_MAXIMG (BU_HEAD + BU_NOBJ * (BU_REC + ST_PAYLOAD_MAX) + 4u)
#define RS_REC 0x000E1000u                            /* the journal record: the update's staging area's 2nd sector */
#define RS_REC_MAGIC 0x31534C50u                      /* "PLS1" */
enum { RS_E_SIZE = 1, RS_E_FETCH, RS_E_HEAD, RS_E_OBJ, RS_E_CRC, RS_E_TOTAL, RS_E_ERASE, RS_E_PROG, RS_E_VERIFY,
       RS_E_PLAYING, RS_E_NO, RS_E_COMMIT };

static int rs_fetch(uint32_t addr, uint8_t *dst, uint32_t n);
static int rs_note(uint32_t addr);
static int rs_playing(void);
static void rs_begin(void);
static void rs_show(uint32_t pct);
static int rs_ask(void);
static void rs_reload(void);
static int rs_rec_erase(void);
static int rs_rec_prog(uint32_t off, const void *src, uint32_t n);
static int rs_rec_read(uint32_t off, void *dst, uint32_t n);
static int bu_room(void);
static int bu_send(const uint8_t *p, uint32_t n);
static void bu_flush(void);
static void bu_hold(int on);

#ifdef APP_BACKUP_BUF                                /* the app's scratch buffer, lent to a restore (plat.h) */
extern uint8_t APP_BACKUP_BUF[];
#define bu_buf APP_BACKUP_BUF
#else
static uint8_t bu_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4))) PLAT_POOL;   /* an object being restored */
#endif

static uint32_t bu_rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t bu_rd32(const uint8_t *p) { return bu_rd16(p) | bu_rd16(p + 2) << 16; }
static void bu_wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void bu_wr32(uint8_t *p, uint32_t v) { bu_wr16(p, v); bu_wr16(p + 2, v >> 16); }
static uint32_t bu_obj(uint32_t i) { return bu_objs[i]; }

static uint32_t bu_crc(uint32_t c, const uint8_t *p, uint32_t n)   /* zlib's crc32(c, p, n): chains */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    c = ~c;
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

/* ------------------------------------------------------------- backup --- */
enum { BU_S_HEAD, BU_S_REC, BU_S_BODY, BU_S_TAIL, BU_S_END };
static struct {
    uint8_t on, state, i, flags;
    uint32_t off, crc;                                /* bytes sent, their CRC-32 */
    uint32_t last;                                    /* ota_now_ms() of the start / the last frame out */
    const uint8_t *p;                                 /* the segment being sent; 0: an object's body, from the flash */
    uint32_t n, len, at;                              /* (at: the body's next byte on the flash) */
    uint8_t seg[BU_HEAD];
} bu;

static uint8_t bu_msg[6 + 4 + BU_CHUNK + 1], bu_wire[2 + ((6 + 4 + BU_CHUNK + 1) * 8 + 6) / 7];

static int bu_frame(uint32_t cmd, const uint8_t *body, uint32_t n)   /* ota.c's framing, any body to BU_CHUNK + 4 */
{
    uint32_t i, s = 0, w;
    bu_msg[0] = 0;
    bu_msg[1] = 0x59;
    bu_msg[2] = (uint8_t)cmd;
    bu_msg[3] = (uint8_t)n;
    bu_msg[4] = (uint8_t)(n >> 8);
    bu_msg[5] = 0;
    for (i = 0; i < n; i++) {
        bu_msg[6 + i] = body[i];
        s += body[i];
    }
    bu_msg[6 + n] = (uint8_t)~s;
    bu_wire[0] = 0xF0;
    w = 1u + ota_pack7(bu_msg, 7u + n, bu_wire + 1);
    bu_wire[w++] = 0xF7;
    return bu_send(bu_wire, w);
}

/* the next segment of the image into bu.p / bu.n; 0 at the end */
static int bu_next(void)
{
    st_hdr_t h;
    switch (bu.state) {
    case BU_S_HEAD:
        bu_wr32(bu.seg, BU_MAGIC);
        bu_wr16(bu.seg + 4, BU_VERSION);
        bu_wr16(bu.seg + 6, BU_NOBJ);
        bu_wr32(bu.seg + 8, bu.flags);
        bu_wr32(bu.seg + 12, bu_crc(0, bu.seg, 12));
        bu.p = bu.seg;
        bu.n = BU_HEAD;
        bu.state = BU_S_REC;
        bu.i = 0;
        return 1;
    case BU_S_REC:
        if (bu.i == BU_NOBJ) {                        /* the tail: the CRC of everything before (all sent) */
            bu_wr32(bu.seg, bu.crc);
            bu.p = bu.seg;
            bu.n = 4;
            bu.state = BU_S_TAIL;
            return 1;
        }
        bu.len = 0;                                   /* the object as st_load sees it (none, or damaged: absent) */
        {
            int c = st_current(bu_obj(bu.i), &h);     /* (its payload checked against h.crc) */
            if (c >= 0 && h.len <= ST_PAYLOAD_MAX) {
                bu.len = h.len;
                bu.at = st_sector(bu_obj(bu.i), (uint32_t)c) + ST_PAYLOAD_OFF;
            } else
                h.crc = 0;
        }
        bu_wr16(bu.seg, bu_obj(bu.i));
        bu_wr16(bu.seg + 2, bu.len);
        bu_wr32(bu.seg + 4, h.crc);
        bu.p = bu.seg;
        bu.n = BU_REC;
        bu.state = BU_S_BODY;
        return 1;
    case BU_S_BODY:
        bu.p = 0;
        bu.n = bu.len;
        bu.state = BU_S_REC;
        bu.i++;
        return 1;
    default:
        bu.state = BU_S_END;
        return 0;
    }
}

/* the host asked for a backup (again: from the start) */
static void bu_start(int playing)
{
    if (!playing)
        bu_flush();
    bu.on = 1;
    bu.state = BU_S_HEAD;
    bu.flags = (uint8_t)(playing != 0);
    bu.off = bu.crc = 0;
    bu.n = 0;
    bu.last = ota_now_ms();
    bu_hold(1);
}

static void bu_stop(void)
{
    bu.on = 0;
    bu_hold(0);
}

/* the main loop: the next frame when the ring has room for it (one frame a pass) */
static void bu_step(void)
{
    uint8_t b[4 + BU_CHUNK];
    uint32_t n = 0;
    int r;
    if (!bu.on)
        return;
    r = bu_room();
    if (r <= 0) {                                     /* a cut cable: the ring never drains; USB gone: at once */
        if (r < 0 || ota_now_ms() - bu.last > BU_STALL_MS)
            bu_stop();
        return;
    }
    while (n < BU_CHUNK) {
        uint32_t k;
        if (!bu.n) {
            if (!bu_next())
                break;
            continue;
        }
        k = bu.n < BU_CHUNK - n ? bu.n : BU_CHUNK - n;
        if (!bu.p) {                                  /* an object's body, from its copy */
            if (st_read(bu.at, b + 4 + n, k)) {
                bu_stop();                            /* (the host sees no end: the backup failed) */
                return;
            }
            bu.at += k;
        } else {
            for (uint32_t j = 0; j < k; j++)
                b[4 + n + j] = bu.p[j];
            bu.p += k;
        }
        bu.crc = bu_crc(bu.crc, b + 4 + n, k);
        bu.n -= k;
        n += k;
    }
    if (n) {
        bu_wr32(b, bu.off);
        bu_frame(BU_REQ, b, 4u + n);                  /* (a frame lost on a cable cut: the host sees the gap) */
        bu.off += n;
        bu.last = ota_now_ms();
        return;
    }
    bu_wr32(b, bu.off);                               /* the end: the length and the whole image's CRC-32 */
    bu_wr32(b + 4, bu.crc);
    b[8] = 0;
    b[9] = bu.flags;
    bu_frame(BU_END, b, 10);
    bu_stop();
}

/* ------------------------------------------------------------ restore --- */
typedef struct {
    uint16_t obj, copy;
    uint32_t seq, len, crc;
} rs_ent_t;
static struct {
    uint32_t magic, n;
    rs_ent_t e[BU_NOBJ];
    uint32_t crc;
} rs_rec __attribute__((aligned(4)));
_Static_assert(sizeof rs_rec <= 4096u, "the journal record fits its sector: fewer objects (APP_BACKUP_OBJS)");

static void rs_head(const rs_ent_t *e, st_hdr_t *h)   /* the header storage.c would write: the copy's commit */
{
    h->magic = ST_MAGIC;
    h->type = e->obj;
    h->slot = e->copy;
    h->seq = e->seq;
    h->len = e->len;
    h->crc = e->crc;
    h->rsv[0] = h->rsv[1] = 0xFFFFFFFFu;
    h->hcrc = st_crc32(h, sizeof *h - 4u);
}

/* object e->obj's payload (bu_buf, e->len bytes) into the copy its next save would take, without its header (so
 * not there yet for st_load); the seq that will make it current */
static int rs_stage(rs_ent_t *e)
{
    st_hdr_t h;
    int cur = st_current(e->obj, &h);
    uint32_t base, off;
    e->copy = (uint16_t)(cur == 0 ? 1u : 0u);
    e->seq = cur < 0 ? 1u : h.seq + 1u;
    base = st_sector(e->obj, e->copy);
    if (st_erase(base))
        return RS_E_ERASE;
    for (off = 0; off < e->len; off += 256u) {
        uint32_t n = e->len - off > 256u ? 256u : e->len - off;
        if (st_prog(base + ST_PAYLOAD_OFF + off, bu_buf + off, n))
            return RS_E_PROG;
    }
    if (st_read(base, &h, sizeof h) || st_read(base + ST_PAYLOAD_OFF, st_buf, e->len) ||
        bu_crc(0, st_buf, e->len) != e->crc)
        return RS_E_VERIFY;
    for (off = 0; off < sizeof h; off++)              /* its header still erased: the copy not there */
        if (((const uint8_t *)&h)[off] != 0xFFu)
            return RS_E_VERIFY;
    return 0;
}

static int rs_rec_valid(void)
{
    return rs_rec.magic == RS_REC_MAGIC && rs_rec.n == BU_NOBJ &&
           rs_rec.crc == bu_crc(0, (const uint8_t *)&rs_rec, sizeof rs_rec - 4u);
}

/* the staged copies' headers (the record's), each written unless already there; 0 = all there */
static int rs_heads(void)
{
    int rc = 0;
    for (uint32_t i = 0; i < BU_NOBJ; i++) {
        st_hdr_t h, f;
        const rs_ent_t *e = &rs_rec.e[i];
        uint32_t base = st_sector(e->obj, e->copy);
        rs_head(e, &h);
        if (st_read(base, &f, sizeof f))
            rc = -1;
        else if (memcmp(&f, &h, sizeof h) && st_prog(base, &h, sizeof h))
            rc = -1;
    }
    for (uint32_t i = 0; i < BU_NOBJ; i++) {          /* each the current copy now */
        st_hdr_t h;
        const rs_ent_t *e = &rs_rec.e[i];
        if (st_current(e->obj, &h) != (int)e->copy || h.seq != e->seq || h.crc != e->crc || h.len != e->len)
            rc = -1;
    }
    return rc;
}

/* boot: a commit cut short (a power loss after its record was written) finished: 1; 0 none; -1 failed */
static int rs_boot(void)
{
    uint8_t b[4];
    uint32_t i;
    int rc;
    if (rs_rec_read(0, &rs_rec, sizeof rs_rec))
        return -1;
    if (!rs_rec_valid()) {
        for (i = 0; i < 4u; i++)                     /* a record half written or half erased: nothing to do; gone */
            b[i] = ((const uint8_t *)&rs_rec)[i];
        if (bu_rd32(b) != 0xFFFFFFFFu)
            rs_rec_erase();
        return 0;
    }
    for (i = 0; i < BU_NOBJ; i++)                     /* (a record of objects storage.c does not have: ignored) */
        if (rs_rec.e[i].obj != bu_obj(i) || rs_rec.e[i].copy > 1u || rs_rec.e[i].len > ST_PAYLOAD_MAX) {
            rs_rec_erase();
            return 0;
        }
    rc = rs_heads();
    if (rc == 0)
        rs_rec_erase();
    return rc ? -1 : 1;
}

/* every object staged: the record, the headers, the record gone. 0 = the new set is current */
static int rs_commit(void)
{
    uint32_t off;
    rs_rec.magic = RS_REC_MAGIC;
    rs_rec.n = BU_NOBJ;
    rs_rec.crc = bu_crc(0, (const uint8_t *)&rs_rec, sizeof rs_rec - 4u);
    if (rs_rec_erase())
        return RS_E_ERASE;
    for (off = 0; off < sizeof rs_rec; off += 256u) {
        uint32_t n = sizeof rs_rec - off > 256u ? 256u : sizeof rs_rec - off;
        if (rs_rec_prog(off, (const uint8_t *)&rs_rec + off, n))
            return RS_E_PROG;
    }
    {   /* read back: a record that is not whole commits nothing (the old set stays) */
        uint32_t want = rs_rec.crc;
        if (rs_rec_read(0, &rs_rec, sizeof rs_rec) || !rs_rec_valid() || rs_rec.crc != want) {
            rs_rec_erase();
            return RS_E_VERIFY;
        }
    }
    if (rs_heads())                                   /* (the record stays: the next boot tries again) */
        return RS_E_COMMIT;
    rs_rec_erase();
    return 0;
}

#ifdef APP_STORE_POOL
/* the pool's restore, after the question: the copies of every pooled object the backup holds erased (a pooled object
 * it does not, the radio's keys say, kept), then each object of the image (fetched again, each record's CRC checked
 * again) saved; 0 = done */
static int rs_rewrite(void)
{
    uint32_t pos = BU_HEAD, i, k;
    (void)rs_stage;                                   /* (the A/B pairs' staging and commit: not a pool's) */
    (void)rs_commit;
    for (i = 0; i < BU_NOBJ; i++)
        if (st_pooled(bu_obj(i)) && st_pool_drop(bu_obj(i)))
            return RS_E_ERASE;
    for (i = 0; i < BU_NOBJ; i++) {
        uint8_t r[BU_REC];
        uint32_t len, crc;
        if (rs_fetch(pos, r, BU_REC))
            return RS_E_FETCH;
        pos += BU_REC;
        len = bu_rd16(r + 2);
        crc = bu_rd32(r + 4);
        if (bu_rd16(r) != bu_obj(i) || len > ST_PAYLOAD_MAX)
            return RS_E_OBJ;
        for (k = 0; k < len; k += 512u) {
            uint32_t n = len - k > 512u ? 512u : len - k;
            if (rs_fetch(pos + k, bu_buf + k, n))
                return RS_E_FETCH;
        }
        pos += len;
        if (bu_crc(0, bu_buf, len) != crc)
            return RS_E_CRC;
#ifdef APP_STORE_POOL_EMPTY
        if (st_pooled(bu_obj(i)) && APP_STORE_POOL_EMPTY(bu_obj(i), bu_buf, len))
            len = 0;                                  /* (an older backup's placeholder: no sector, storage.c) */
#endif
        if (st_save(bu_obj(i), bu_buf, len))
            return RS_E_PROG;
        rs_show(95u + (i + 1u) * 5u / BU_NOBJ);
    }
    return 0;
}
#endif

/* the host's restore request: the image of len bytes, CRC-32 crc. Returns 0 (restored) or RS_E_*; the host is told
 * either way (rs_note); *begun: the screen was taken (a backup was coming) */
static int restore_rx(uint32_t len, uint32_t crc, int *begun_out)
{
    uint8_t hd[BU_HEAD];
    uint32_t c, pos, i, k;
    int rc = 0, begun = 0;
    if (rs_playing()) {
        rc = RS_E_PLAYING;
        goto out;
    }
    if (len < BU_HEAD + BU_NOBJ * BU_REC + 4u || len > BU_MAXIMG) {
        rc = RS_E_SIZE;
        goto out;
    }
    if (rs_fetch(0, hd, BU_HEAD)) {
        rc = RS_E_FETCH;
        goto out;
    }
    if (bu_rd32(hd) != BU_MAGIC || bu_rd16(hd + 4) != BU_VERSION || bu_rd16(hd + 6) != BU_NOBJ ||
        bu_rd32(hd + 12) != bu_crc(0, hd, 12)) {
        rc = RS_E_HEAD;
        goto out;
    }
    rs_begin();
    begun = 1;
    rs_show(0);
    c = bu_crc(0, hd, BU_HEAD);
    pos = BU_HEAD;
    for (i = 0; i < BU_NOBJ; i++) {
        rs_ent_t *e = &rs_rec.e[i];
        uint8_t r[BU_REC];
        if (rs_fetch(pos, r, BU_REC)) {
            rc = RS_E_FETCH;
            goto out;
        }
        c = bu_crc(c, r, BU_REC);
        pos += BU_REC;
        e->obj = (uint16_t)bu_rd16(r);
        e->len = bu_rd16(r + 2);
        e->crc = bu_rd32(r + 4);
        if (e->obj != bu_obj(i) || e->len > ST_PAYLOAD_MAX || pos + e->len + 4u > len) {
            rc = RS_E_OBJ;
            goto out;
        }
        for (k = 0; k < e->len; k += 512u) {
            uint32_t n = e->len - k > 512u ? 512u : e->len - k;
            if (rs_fetch(pos + k, bu_buf + k, n)) {
                rc = RS_E_FETCH;
                goto out;
            }
        }
        c = bu_crc(c, bu_buf, e->len);
        pos += e->len;
        if (bu_crc(0, bu_buf, e->len) != e->crc) {
            rc = RS_E_CRC;
            goto out;
        }
#ifndef APP_STORE_POOL
        if ((rc = rs_stage(e)) != 0)
            goto out;
#endif
        rs_show((i + 1u) * 90u / BU_NOBJ);
    }
    {
        uint8_t t[4];
        if (rs_fetch(pos, t, 4)) {
            rc = RS_E_FETCH;
            goto out;
        }
        if (bu_rd32(t) != c || pos + 4u != len || bu_crc(c, t, 4) != crc) {
            rc = RS_E_TOTAL;
            goto out;
        }
    }
    if (rs_playing()) {                               /* (started meanwhile: MIDI start) */
        rc = RS_E_PLAYING;
        goto out;
    }
    rs_note(RS_ASK_ADDR);                             /* the host: "confirm on the FM-1" */
    if (!rs_ask()) {
        rc = RS_E_NO;
        goto out;
    }
    if (rs_playing()) {
        rc = RS_E_PLAYING;
        goto out;
    }
    rs_show(95);
#ifdef APP_STORE_POOL
    if ((rc = rs_rewrite()) != 0) {
        rs_reload();                                  /* (part written: what the flash holds now) */
        goto out;
    }
    if (0) {
#else
    if ((rc = rs_commit()) != 0) {
#endif
        if (rc == RS_E_COMMIT)
            rs_reload();                              /* (headers part written: what the flash holds now) */
        goto out;
    }
    rs_reload();
    rs_show(100);
out:
    rs_note(rc ? RS_FAIL_ADDR | (uint32_t)rc : RS_OK_ADDR);
    if (begun_out)
        *begun_out = begun;
    return rc;
}
