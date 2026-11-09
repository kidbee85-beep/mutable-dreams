/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Changed for X0X and fm1-base: the object map is the app's (see below). */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload (from offset 256),
 * then the 32-byte header at offset 0 last. The header is the commit record;
 * on load the valid copy with the highest seq wins, so a torn write leaves
 * the previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 *
 * The pool (an app's APP_STORE_POOL, app.h): objects APP_STORE_POOL_OBJ0 .. + APP_STORE_POOL_OBJN share
 * APP_STORE_POOL_N sectors from APP_STORE_POOL_BASE instead of a pair each. A sector holds one copy of one object, with
 * the same header (its slot field: the sector's index); an object's current copy is its valid copy with the highest
 * seq, wherever it is, its previous copy the next. A save takes a sector no current copy holds: an erased or broken
 * one first, then a copy of the object's own older than its current, then other objects' copies with two newer, then
 * other objects' previous copies (so a stream saved object by object keeps its previous copies while there is room).
 * No room: -8, nothing written. An empty object (a restore's absent one) erases the object's copies. The pool's
 * first APP_STORE_POOL_OLD sectors were the app's A/B pairs before it (the same headers, so they are pool copies as
 * they are): st_pool_boot moves the current copies found past the pool's end into it, erasing what it moved, and
 * until every one has moved a lookup scans them too (st_pool_scan: the pool's own sectors once they are gone). An
 * object whose current copy the app calls empty (APP_STORE_POOL_EMPTY(obj, payload, len), its option: a placeholder
 * the layout before needed and the pool does not) is dropped first instead, wherever its copies are.
 */
#define ST_MAGIC 0x30583058u                   /* "X0X0": never mistaken for a Felucca object */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map: the app's (app.h APP_STORE_MAP, an offset per object, plat.h). Nothing between 0xE0000 and 0xFBFFF:
 * that is the update loader's staging area. The magic is X0X's; an app tells its own objects apart by its own magic. */
typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32(const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

#ifdef APP_STORE_POOL
#define ST_POOL_LAST (APP_STORE_POOL_OBJ0 + APP_STORE_POOL_OBJN)
static int st_pooled(uint32_t obj) { return obj >= APP_STORE_POOL_OBJ0 && obj < ST_POOL_LAST; }
static uint32_t st_pool_scan = APP_STORE_POOL_OLD;    /* sectors a lookup reads: APP_STORE_POOL_N once boot moved all */
#else
static int st_pooled(uint32_t obj) { (void)obj; return 0; }
#endif
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1); a pooled object's: sector */
{
    static const uint32_t map[OBJ_NOBJ] = APP_STORE_MAP;
#ifdef APP_STORE_POOL
    if (st_pooled(obj))
        return APP_STORE_POOL_BASE + copy * ST_SECTOR;
#endif
    return map[obj] + copy * ST_SECTOR;
}

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->len > ST_PAYLOAD_MAX ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h)   /* payload -> st_buf, CRC ok: 0 */
{
    if (st_read(st_sector(obj, copy) + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}

/* the current copy: the valid one with the highest seq (A on a tie), -1 when
 * neither is valid. Headers first, so only the winner's payload is read (it
 * is left in st_buf); *h gets its header. */
#ifdef APP_STORE_POOL
/* a pooled object's valid copy with the highest seq below `below` (0: any) whose payload checks out (left in st_buf):
 * its sector, -1 none */
static int st_pool_find(uint32_t obj, st_hdr_t *h, uint32_t below)
{
    for (;;) {
        st_hdr_t c;
        int best = -1;
        for (uint32_t i = 0; i < st_pool_scan; i++)
            if (st_head(obj, i, &c) == 0 && (!below || c.seq < below) && (best < 0 || c.seq > h->seq)) {
                *h = c;
                best = (int)i;
            }
        if (best < 0)
            return -1;
        if (st_body(obj, (uint32_t)best, h) == 0)
            return best;
        below = h->seq;                               /* (a torn payload: the next older) */
    }
}
#endif
static int st_current(uint32_t obj, st_hdr_t *h)
{
    st_hdr_t a, b;
#ifdef APP_STORE_POOL
    if (st_pooled(obj))
        return st_pool_find(obj, h, 0);
#endif
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || b.seq > a.seq)) {
        if (st_body(obj, 1, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}

/* load object into dst (up to max bytes); returns the length, or -1 (none; an empty object is none too: a restore
 * writes one for an object the backup did not have, backup.c) */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (st_current(obj, &h) < 0 || !h.len)
        return -1;
    if (h.len > max)
        h.len = max;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

/* the object's length, or -1 (none) */
static int st_size(uint32_t obj)
{
    st_hdr_t h;
    if (st_current(obj, &h) < 0 || !h.len)
        return -1;
    return (int)h.len;
}

/* the object's first n bytes (a header, without a buffer for the whole): n, or -1 (none, or shorter than n) */
static int st_load_head(uint32_t obj, void *dst, uint32_t n)
{
    uint32_t i;
    st_hdr_t h;
    if (st_current(obj, &h) < 0 || !h.len || h.len < n)
        return -1;
    for (i = 0; i < n; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)n;
}

#ifdef APP_STORE_POOL
/* the pool's sectors as their headers say: a pooled object's copy (its type, seq), an erased sector, anything else
 * (broken: a torn header, a payload that does not check out above the current copy) */
enum { ST_PS_COPY, ST_PS_ERASED, ST_PS_BROKEN, ST_PS_UNREAD };
typedef struct { uint16_t type; uint8_t st, rank; uint32_t seq; } st_ps_t;
static st_ps_t st_ps[APP_STORE_POOL_OLD > APP_STORE_POOL_N ? APP_STORE_POOL_OLD : APP_STORE_POOL_N];
static void st_pool_read(void)
{
    for (uint32_t i = 0; i < st_pool_scan; i++) {
        st_hdr_t h;
        uint32_t k;
        st_ps_t *t = &st_ps[i];
        t->type = 0xFFFFu, t->seq = 0, t->rank = 0xFFu;
        if (st_read(APP_STORE_POOL_BASE + i * ST_SECTOR, &h, sizeof h)) {
            t->st = ST_PS_UNREAD;
            continue;
        }
        for (k = 0; k < sizeof h && ((const uint8_t *)&h)[k] == 0xFFu; k++)
            ;
        if (k == sizeof h)
            t->st = ST_PS_ERASED;
        else if (h.magic == ST_MAGIC && st_pooled(h.type) && h.len <= ST_PAYLOAD_MAX && h.hcrc == st_crc32(&h, sizeof h - 4u))
            t->st = ST_PS_COPY, t->type = h.type, t->seq = h.seq;
        else
            t->st = ST_PS_BROKEN;
    }
    for (uint32_t o = APP_STORE_POOL_OBJ0; o < ST_POOL_LAST; o++) {   /* a copy above its object's current one (torn):
                                                                      * broken */
        st_hdr_t h;
        uint32_t i, any = 0;
        for (i = 0; i < st_pool_scan && !any; i++)
            any = st_ps[i].st == ST_PS_COPY && st_ps[i].type == o;
        if (!any)
            continue;
        if (st_pool_find(o, &h, 0) < 0)
            h.seq = 0;                                /* (none checks out: every copy of it is broken) */
        for (i = 0; i < st_pool_scan; i++)
            if (st_ps[i].st == ST_PS_COPY && st_ps[i].type == o && (st_ps[i].seq > h.seq || !h.seq))
                st_ps[i].st = ST_PS_BROKEN;
    }
}
/* how good a sector is to write object obj's next copy into (st_pool_read first): 0 erased or broken, 1 an older
 * copy of obj's own, 2 another object's with two newer, 3 another's previous copy; 0xFF none (a current copy, unread,
 * or past the pool's end) */
static uint32_t st_pool_rank(uint32_t obj, uint32_t i)
{
    const st_ps_t *t = &st_ps[i];
    uint32_t newer = 0;
    if (i >= APP_STORE_POOL_N || t->st == ST_PS_UNREAD)
        return 0xFFu;
    if (t->st != ST_PS_COPY)
        return 0;
    for (uint32_t j = 0; j < st_pool_scan; j++)
        newer += st_ps[j].st == ST_PS_COPY && st_ps[j].type == t->type && st_ps[j].seq > t->seq;
    if (!newer)
        return 0xFFu;
    return t->type == obj ? 1u : newer >= 2u ? 2u : 3u;
}
static int st_pool_pick(uint32_t obj)               /* the best sector for obj's next copy, -1 none */
{
    int best = -1;
    uint32_t br = 0xFFu;
    st_pool_read();
    for (uint32_t i = 0; i < APP_STORE_POOL_N; i++) {
        uint32_t r = st_pool_rank(obj, i);
        if (r < br)
            br = r, best = (int)i;
    }
    return best;
}
/* sectors a save could write without overwriting any current copy (the app's check before a save that writes n
 * objects: n of them keep every previous copy); a pooled save needs one */
static int st_pool_room(void)
{
    int n = 0;
    st_pool_read();
    for (uint32_t i = 0; i < APP_STORE_POOL_N; i++)
        n += st_pool_rank(0xFFFFu, i) != 0xFFu;
    return n;
}
/* an empty object: its copies erased (a restore's absent object; a power cut half way leaves some, which load) */
static int st_pool_drop(uint32_t obj)
{
    st_hdr_t h;
    for (uint32_t i = 0; i < st_pool_scan; i++)
        if (st_head(obj, i, &h) == 0 && st_erase(APP_STORE_POOL_BASE + i * ST_SECTOR))
            return -3;
    return 0;
}
static uint32_t st_pool_maxseq(uint32_t obj)        /* the highest seq of any of obj's copies (a torn one too) */
{
    st_hdr_t h;
    uint32_t m = 0;
    for (uint32_t i = 0; i < st_pool_scan; i++)
        if (st_head(obj, i, &h) == 0 && h.seq > m)
            m = h.seq;
    return m;
}
#endif

static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t seq, base, off;
    int cur, rc;
    st_hdr_t h;
    if (len > ST_PAYLOAD_MAX)
        return -1;
#ifdef APP_STORE_POOL
    if (st_pooled(obj)) {
        if (!len)
            return st_pool_drop(obj);
        seq = st_pool_maxseq(obj);
        cur = st_pool_pick(obj);
        if (cur < 0)
            return -8;                                /* the pool is full: nothing written */
        cur = -(cur + 2);                             /* (sector k as -(k + 2): the write below takes it) */
    } else
#endif
    {
        cur = st_current(obj, &h);
        seq = cur < 0 ? 0u : h.seq;
    }
    base = st_sector(obj, cur <= -2 ? (uint32_t)(-cur - 2) : cur == 0 ? 1u : 0u);   /* write the other copy */
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)(cur <= -2 ? -cur - 2 : cur == 0 ? 1 : 0);
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        uint32_t c = h.slot;
        if (st_head(obj, c, &chk) || chk.seq != h.seq || st_body(obj, c, &chk))
            return -7;
    }
    return 0;
}

/* the copy before the current one (A/B: the other; the pool: the next older valid one): a stream torn across objects
 * falls back to the save before (LOOM-8's project.c). Its length, -1 none */
static int st_load_prev(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    int cur = st_current(obj, &h);
    if (cur < 0)
        return -1;
#ifdef APP_STORE_POOL
    if (st_pooled(obj)) {
        if (st_pool_find(obj, &h, h.seq) < 0)
            return -1;
    } else
#endif
    if (st_head(obj, (uint32_t)!cur, &h) || st_body(obj, (uint32_t)!cur, &h))
        return -1;
    if (h.len > max)
        h.len = max;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

#ifdef APP_STORE_POOL
/* boot: the current copies past the pool's end (the A/B pairs the app had before it) into the pool, each copied,
 * read back, then its old sector erased; older copies there erased. A power cut leaves two copies with one seq (the
 * same bytes) or one; no room: the rest stay where they are and lookups keep reading them. 0 = all in the pool */
static int st_pool_boot(void)
{
    int left = 0;
#ifdef APP_STORE_POOL_EMPTY
    for (uint32_t o = APP_STORE_POOL_OBJ0; o < ST_POOL_LAST; o++) {   /* the empty ones dropped, the current copy last */
        st_hdr_t h;                                   /* (a power cut between: the next boot drops it again, never an */
        int c = st_pool_find(o, &h, 0);               /* older copy left current) */
        if (c < 0 || st_body(o, (uint32_t)c, &h) || !APP_STORE_POOL_EMPTY(o, st_buf, h.len))
            continue;
        for (uint32_t i = 0; i < st_pool_scan; i++)
            if (i != (uint32_t)c && st_head(o, i, &h) == 0 && st_erase(APP_STORE_POOL_BASE + i * ST_SECTOR))
                left++;
        if (st_erase(APP_STORE_POOL_BASE + (uint32_t)c * ST_SECTOR))
            left++;
    }
#endif
    for (uint32_t i = APP_STORE_POOL_N; i < APP_STORE_POOL_OLD; i++) {
        st_hdr_t h, c;
        uint32_t at = APP_STORE_POOL_BASE + i * ST_SECTOR, to, off;
        int k;
        if (st_read(at, &h, sizeof h) || h.magic != ST_MAGIC || !st_pooled(h.type) || h.len > ST_PAYLOAD_MAX ||
            h.hcrc != st_crc32(&h, sizeof h - 4u))
            continue;                                 /* (not a copy: an erased sector, or the app's other data) */
        if (st_pool_find(h.type, &c, 0) != (int)i) {   /* an older copy, or a torn one */
            if (st_erase(at))
                left++;
            continue;
        }
        k = st_pool_pick(h.type);
        if (k < 0 || st_body(h.type, i, &c)) {        /* (st_buf: the copy's payload again; the pick used it) */
            left++;
            continue;
        }
        to = APP_STORE_POOL_BASE + (uint32_t)k * ST_SECTOR;
        if (st_erase(to)) {
            left++;
            continue;
        }
        for (off = 0; off < c.len; off += 256u) {
            uint32_t n = c.len - off > 256u ? 256u : c.len - off;
            if (st_prog(to + ST_PAYLOAD_OFF + off, st_buf + off, n))
                break;
        }
        c.slot = (uint16_t)k;
        c.hcrc = st_crc32(&c, sizeof c - 4u);
        if (off < c.len || st_prog(to, &c, sizeof c) || st_head(h.type, (uint32_t)k, &c) || st_body(h.type, (uint32_t)k, &c) ||
            st_erase(at))
            left++;
    }
    if (!left)
        st_pool_scan = APP_STORE_POOL_N;
    return left;
}
#endif
