/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The crash history: the last crash records kept in flash, so a power-off loses none (fm1_crash and fm1_crash_ctx are
 * .noinit RAM: a restart keeps them, a power-off does not). The fault handler writes no flash; the next boot appends
 * the crash it finds (cl_boot) to a ring of 128-byte records in two sectors, and the USB-MIDI readouts print it.
 *
 * Where: CL_LO, the update's staging area's 3rd and 4th sectors (0xE2000..0xE3FFF: ota.c stages a loader in
 * 0xE0000..0xE4FFF, its record at 0xE4F00; backup.c's journal takes the 2nd, 0xE1000). The platform's own flash, in
 * every layout: no app window reaches it (plat.h PLAT_DATA_*, PLAT_XDATA*: the store and the data area end at 0xE0000,
 * XDATA1 starts at 0xE5000), and it stays where it is whatever the app's slot (app_end). The cost: an update erases
 * the staging area whole (ota_stage, ota_unstage; the boot after an install, ota_boot_cleanup), so an install starts
 * the history again: the old records' addresses were the old build's anyway. Each sector's last 256 bytes stay erased
 * (the update loader's record scan, ldr_core.c: "AT" at 0xF06 of a 4 KiB boundary - 256): 30 records a sector.
 *
 * A record goes in at the first erased slot after the newest; magic last (the commit): a record cut short, or a sector
 * whose erase was cut short, reads as absent (magic, then a CRC-32 over the rest). When the newest's sector is full,
 * the other is erased and the record is its first: the full one keeps the 29 before it, so a power cut in that erase
 * loses nothing that was read before. The readouts show the newest CL_KEEP, oldest first.
 *
 * Hooks from the platform (device/main_fm1.c, host/plat_host.c, tests/crashlog_test.c):
 *   cl_read(off, dst, n)   cl_erase(off) (4 KiB)   cl_prog(off, src, n) (bits 1 to 0, within a 256-byte page)
 *   cl_send(frame, n)      one F0 .. F7 frame to the computer */
#define CL_MAGIC 0x31484C43u                        /* "CLH1" */
#define CL_LO 0x000E2000u                           /* the ring's first sector (the staging area's 3rd) */
#define CL_SECTORS 2u
#define CL_REC 128u                                 /* bytes a record: two to a 256-byte page, never across one */
#define CL_SLOTS (0xF00u / CL_REC)                  /* 30 a sector: 0xF00..0xFFF erased (the loader's record scan) */
#define CL_KEEP 8u                                  /* the newest shown */
#define CL_CTX 1u                                   /* flags: r[] and usp hold the registers at the crash */
#define CL_STK 2u                                   /* flags: bits 2..31 hold the stacks' room at the crash (CL_STK_*) */
/* each stack's room left at the crash in 10 bits of flags, in 32-byte units (rounded down; CL_STK_NONE: not known):
 * main bits 2..11, interrupts 12..21, the second core's 22..31 (main_fm1.c fm1_fault: stacks.c pst_at_crash) */
#define CL_STK_NONE 1023u
#define CL_STK_AT(k) (2u + 10u * (k))
static uint32_t cl_stk_pack(const int32_t left[3])
{
    uint32_t k, f = CL_STK;
    for (k = 0; k < 3u; k++) {
        uint32_t u = (uint32_t)left[k] / 32u;
        u = left[k] < 0 ? CL_STK_NONE : u < CL_STK_NONE ? u : CL_STK_NONE - 1u;   /* (the most: 32,704 bytes) */
        f |= u << CL_STK_AT(k);
    }
    return f;
}
static int32_t cl_stk_left(uint32_t flags, uint32_t k)   /* bytes (a multiple of 32), -1 not known */
{
    uint32_t u = flags >> CL_STK_AT(k) & CL_STK_NONE;
    return !(flags & CL_STK) || u == CL_STK_NONE ? -1 : (int32_t)(u * 32u);
}
#if !defined PLAT_DATA_HI || !defined PLAT_XDATA1_LO
#error "crashlog.c: plat.h first"
#endif
_Static_assert(CL_LO >= PLAT_DATA_HI && CL_LO + CL_SECTORS * 0x1000u <= PLAT_XDATA1_LO - 0x1000u,
               "the crash history: inside the update's staging (0xE0000..0xE4FFF), off its record's sector 0xE4000");
_Static_assert(CL_LO >= 0xE2000u, "the crash history: off backup.c's journal (0xE1000) and the staged loader's head");

typedef struct {
    uint32_t magic;                                 /* CL_MAGIC, programmed last: the commit */
    uint32_t seq;                                   /* the ring's order: 1 up, the newest the highest */
    uint32_t count, vec, pc, rets, sp, uptime_ms, emu, dbg, psr;   /* fm1_crash's (felucca-bsp chip/fm1_irq.h) */
    uint32_t build;                                 /* FM1B_BUILD of the firmware that saved it (tools/build.py) */
    uint32_t flags;                                 /* CL_CTX, CL_STK and the stacks' room */
    uint32_t r[16], usp, ssp;                       /* fm1_crash_ctx's for this count (flags CL_CTX) */
    uint32_t crc;                                   /* CRC-32 of seq .. ssp */
} cl_rec_t;
_Static_assert(sizeof(cl_rec_t) == CL_REC, "a crash record: 128 bytes");

static int cl_read(uint32_t off, void *dst, uint32_t n);
static int cl_erase(uint32_t off);
static int cl_prog(uint32_t off, const void *src, uint32_t n);
static int cl_send(const uint8_t *f, uint32_t n);

static uint32_t cl_crc(const void *p, uint32_t n)  /* zlib's CRC-32, a bit at a time (no table: 30 words, once a boot) */
{
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--)
        for (c ^= *b++, k = 0; k < 8u; k++)
            c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1u)));
    return ~c;
}
static uint32_t cl_at(uint32_t s, uint32_t i) { return CL_LO + s * 0x1000u + i * CL_REC; }
/* slot i of sector s: 1 a whole record (into *r), 0 erased, -1 neither (cut short, or garbage) */
static int cl_slot(uint32_t s, uint32_t i, cl_rec_t *r)
{
    const uint32_t *w = (const uint32_t *)r;
    uint32_t k, all = 0xFFFFFFFFu;
    if (cl_read(cl_at(s, i), r, CL_REC))
        return -1;
    for (k = 0; k < CL_REC / 4u; k++)
        all &= w[k];
    if (all == 0xFFFFFFFFu)
        return 0;
    return r->magic == CL_MAGIC && r->crc == cl_crc(&r->seq, CL_REC - 8u) ? 1 : -1;
}
/* the ring as it is: the newest's seq, its sector and the next slot there (CL_SLOTS: full); none: seq 0, sector 0 */
typedef struct {
    uint32_t seq, sec, next;
} cl_scan_t;
static void cl_scan(cl_scan_t *t)
{
    cl_rec_t r;
    uint32_t s, i;
    t->seq = 0, t->sec = 0, t->next = 0;
    for (s = 0; s < CL_SECTORS; s++)
        for (i = 0; i < CL_SLOTS; i++)
            if (cl_slot(s, i, &r) == 1 && r.seq > t->seq)
                t->seq = r.seq, t->sec = s, t->next = i + 1u;
    while (t->next < CL_SLOTS && cl_slot(t->sec, t->next, &r) != 0)
        t->next++;                                  /* (a slot cut short after the newest, or nothing yet and sector
                                                     * 0 not erased: past it) */
}
/* one record in (r: count .. usp; seq, magic and crc are set here): 0 = kept */
static int cl_append(cl_rec_t *r)
{
    cl_scan_t t;
    uint32_t at;
    cl_rec_t chk;
    cl_scan(&t);
    if (t.next >= CL_SLOTS) {                       /* full (or never written: garbage): the other sector, from its top */
        t.sec = t.seq ? (t.sec + 1u) % CL_SECTORS : 0u;
        t.next = 0;
        if (cl_erase(CL_LO + t.sec * 0x1000u))
            return -1;
    }
    at = cl_at(t.sec, t.next);
    r->magic = 0xFFFFFFFFu;                         /* (left erased: programmed last) */
    r->seq = t.seq + 1u;
    r->crc = cl_crc(&r->seq, CL_REC - 8u);
    if (cl_prog(at + 4u, &r->seq, CL_REC - 4u))
        return -2;
    r->magic = CL_MAGIC;
    if (cl_prog(at, &r->magic, 4u))
        return -3;
    return cl_slot(t.sec, t.next, &chk) == 1 && !memcmp(&chk, r, CL_REC) ? 0 : -4;   /* (read back) */
}
static int cl_clear(void)                           /* both sectors erased: 0 = done */
{
    uint32_t s;
    for (s = 0; s < CL_SECTORS; s++)
        if (cl_erase(CL_LO + s * 0x1000u))
            return -1;
    return 0;
}
/* the newest CL_KEEP records' places, oldest first: their number (at[k] = sector << 8 | slot) */
static uint32_t cl_list(uint32_t at[CL_KEEP])
{
    uint32_t seq[CL_KEEP], n = 0, s, i, k;
    cl_rec_t r;
    for (s = 0; s < CL_SECTORS; s++)
        for (i = 0; i < CL_SLOTS; i++) {
            if (cl_slot(s, i, &r) != 1)
                continue;
            if (n == CL_KEEP && r.seq <= seq[0])
                continue;                           /* (older than every one kept) */
            if (n == CL_KEEP) {                     /* the oldest kept goes */
                for (k = 1; k < n; k++)
                    seq[k - 1u] = seq[k], at[k - 1u] = at[k];
                n--;
            }
            for (k = n; k && seq[k - 1u] > r.seq; k--)   /* (in order of seq: insertion) */
                seq[k] = seq[k - 1u], at[k] = at[k - 1u];
            seq[k] = r.seq, at[k] = s << 8 | i;
            n++;
        }
    return n;
}

/* the boot: r is the crash fm1_crash holds (count .. usp filled), kept the count already saved and busy a save under
 * way (both .noinit, zero after a power-on: fm1b_dbg). A save that did not finish (a crash or the watchdog inside it)
 * is not tried again: busy still set says so, and the crash it caused is let go too, so a fault in this path cannot
 * loop. 1 = appended, 0 = nothing new, < 0 = not kept */
static int cl_boot(cl_rec_t *r, uint32_t *kept, uint32_t *busy)
{
    int rc;
    if (*busy) {
        *busy = 0;
        *kept = r->count;
        return -9;
    }
    if (*kept == r->count)
        return 0;
    *kept = r->count;
    *busy = 1;
    rc = cl_append(r);
    *busy = 0;
    return rc ? rc : 1;
}

/* ---------------------------------------------------------- readout --- */
static char *cl_hex(char *q, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        *q++ = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    return q;
}
static char *cl_dec(char *q, uint32_t v)
{
    char t[10];
    int n = 0;
    do
        t[n++] = (char)('0' + v % 10u);
    while (v /= 10u);
    while (n)
        *q++ = t[--n];
    return q;
}
static char *cl_str(char *q, const char *s)
{
    while (*s)
        *q++ = *s++;
    return q;
}
static void cl_frame(uint8_t h2, uint8_t *f, char *end)   /* f: F0 7D 4C h2 then the text up to end; sent */
{
    f[0] = 0xF0, f[1] = 0x7D, f[2] = 0x4C, f[3] = h2;
    *end = (char)0xF7;
    cl_send(f, (uint32_t)((uint8_t *)end - f) + 1u);
}
/* the records in a fixed form, oldest first, after head (h2: the frame's 4th byte), each one frame:
 *   crash I/N n COUNT vec HEX pc HEX rets HEX sp HEX ms DEC emu HEX dbg HEX psr HEX build HEX seq DEC
 * and, when it has them, a second:
 *   regs I/N r0 HEX .. r15 HEX usp HEX ssp HEX
 * and, when it has them, a third:
 *   stk I/N main DEC irq DEC c1 DEC guard 0|1
 * (each stack's bytes left at the crash, -1 not known; guard 1: a stack ran into its guard, EMU_MSG bit 3 (sp out of
 * the stacks' window) or DBG_MSG bit 13 (a write into a guard band; the same bit as the top window's, 0x01C7FD50..))
 * then "end N" (N records sent: a reader tells none from an answer lost). Hex: 8 digits, upper case */
static void cl_stk_line(uint8_t h2, uint32_t k, uint32_t n, const cl_rec_t *r)
{
    static const char *const L[3] = {" main ", " irq ", " c1 "};
    uint8_t f[4u + 64u];
    char *q = cl_str((char *)f + 4, "stk ");
    uint32_t j;
    q = cl_dec(q, k + 1u);
    *q++ = '/';
    q = cl_dec(q, n);
    for (j = 0; j < 3u; j++) {
        int32_t v = cl_stk_left(r->flags, j);
        q = cl_str(q, L[j]);
        q = v < 0 ? cl_str(q, "-1") : cl_dec(q, (uint32_t)v);
    }
    q = cl_dec(cl_str(q, " guard "), (r->emu >> 3 & 1u) | (r->dbg >> 13 & 1u));
    cl_frame(h2, f, q);
}
/* a line's fields from the record's words: label i at word w[i] (dec: decimal, else hex), after "HEAD I/N" */
static __attribute__((noinline)) void cl_line(uint8_t h2, const char *head, uint32_t k, uint32_t n,
                                              const uint32_t *rw, const char *const *lab, const uint8_t *w,
                                              uint32_t nf, uint32_t dec)
{
    uint8_t f[4u + 240u];                           /* (a regs line: 232 characters at most, then F7) */
    char *q = cl_str((char *)f + 4, head);
    uint32_t j;
    q = cl_dec(q, k + 1u);
    *q++ = '/';
    q = cl_dec(q, n);
    for (j = 0; j < nf; j++) {
        q = cl_str(q, lab[j]);
        q = dec >> j & 1u ? cl_dec(q, rw[w[j]]) : cl_hex(q, rw[w[j]]);
    }
    cl_frame(h2, f, q);
}
static uint32_t cl_print(uint8_t h2)
{
    static const char *const LC[11] = {" n ", " vec ", " pc ", " rets ", " sp ", " ms ", " emu ", " dbg ", " psr ",
                                       " build ", " seq "};
    static const uint8_t WC[11] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 1};   /* (cl_rec_t's words: count .. build, seq) */
    static const char *const LR[18] = {" r0 ", " r1 ", " r2 ", " r3 ", " r4 ", " r5 ", " r6 ", " r7 ", " r8 ", " r9 ",
                                       " r10 ", " r11 ", " r12 ", " r13 ", " r14 ", " r15 ", " usp ", " ssp "};
    static const uint8_t WR[18] = {13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30};
    uint32_t at[CL_KEEP], n = cl_list(at), k;
    cl_rec_t r;
    _Static_assert(__builtin_offsetof(cl_rec_t, build) == 11u * 4u && __builtin_offsetof(cl_rec_t, r) == 13u * 4u &&
                   __builtin_offsetof(cl_rec_t, ssp) == 30u * 4u, "cl_print's word numbers: cl_rec_t's");
    for (k = 0; k < n; k++) {
        if (cl_slot(at[k] >> 8, at[k] & 0xFFu, &r) != 1)
            continue;
        cl_line(h2, "crash ", k, n, (const uint32_t *)&r, LC, WC, 11u, 1u << 0 | 1u << 5 | 1u << 10);
        if (r.flags & CL_CTX)
            cl_line(h2, "regs ", k, n, (const uint32_t *)&r, LR, WR, 18u, 0u);
        if (r.flags & CL_STK)
            cl_stk_line(h2, k, n, &r);
    }
    return n;
}
/* the history's own request (F0 7D 4C 48 F7, "LH"): the records, then "end N". F0 7D 4C 48 43 4C 52 F7 ("LHCLR"):
 * the history erased, then "cleared" (or "clear failed"). 1 = the frame was this */
static int cl_request(const uint8_t *p, uint32_t n)
{
    uint8_t f[4u + 24u];
    char *q;
    if (n < 3u || p[0] != 0x7Du || p[1] != 0x4Cu || p[2] != 0x48u)
        return 0;
    if (n == 6u && p[3] == 'C' && p[4] == 'L' && p[5] == 'R') {
        q = cl_str((char *)f + 4, cl_clear() ? "clear failed" : "cleared");
        cl_frame(0x48, f, q);
        return 1;
    }
    if (n != 3u)
        return 1;                                   /* (another of its form: not ours to guess at) */
    q = cl_dec(cl_str((char *)f + 4, "end "), cl_print(0x48));
    cl_frame(0x48, f, q);
    return 1;
}
