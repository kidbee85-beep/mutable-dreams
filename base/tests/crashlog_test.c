/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* Host test of device/crashlog.c (the crash history in flash) against a simulated NOR: erase -> 0xFF, a program only
 * clears bits and stays within its page. The ring filled past both sectors (the newest 8 shown, oldest first), a
 * power cut at every program and inside an erase (a record cut short reads as absent, nothing older is lost), the
 * boot's dedup across restarts and power-offs, a save cut short not tried again, the readout's frames and the clear. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/plat.h"

static uint8_t nor[0x100000];
static int prog_left = -1;                     /* power cut: programs still allowed (-1: no limit) */
static int erase_cut;                          /* 1: the next erase stops half way (half the sector erased) */
static uint32_t erases;
static char sent[64][256];                     /* the frames sent, as text (between F0 7D 4C xx and F7) */
static uint8_t sent_h2[64];
static int nsent;

static int cl_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int cl_erase(uint32_t off)
{
    if (off & 0xFFFu) {
        printf("erase off a sector: %#x\n", off);
        exit(1);
    }
    erases++;
    memset(nor + off, 0xFF, erase_cut ? 2048u : 4096u);
    if (erase_cut) {
        erase_cut = 0;
        return -5;
    }
    return 0;
}
static int cl_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    if ((off & 0xFFFu) >= 0xF00u) {
        printf("a program in a sector's last 256 bytes (the loader's record scan): %#x\n", off);
        exit(1);
    }
    if (prog_left == 0)
        return -9;
    if (prog_left > 0)
        prog_left--;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
static int cl_send(const uint8_t *f, uint32_t n)
{
    if (n < 5u || f[0] != 0xF0 || f[1] != 0x7D || f[2] != 0x4C || f[n - 1u] != 0xF7 || nsent == 64 || n - 5u > 255u) {
        printf("a bad frame (%u bytes)\n", n);
        exit(1);
    }
    for (uint32_t i = 4; i + 1u < n; i++)
        if (f[i] & 0x80u) {
            printf("a frame byte over 7 bits\n");
            exit(1);
        }
    sent_h2[nsent] = f[3];
    memcpy(sent[nsent], f + 4, n - 5u);
    sent[nsent][n - 5u] = 0;
    nsent++;
    return 0;
}
#include "../device/crashlog.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static cl_rec_t rec(uint32_t count)            /* a crash as main_fm1.c's crash_keep fills it */
{
    cl_rec_t r;
    uint32_t i;
    memset(&r, 0, sizeof r);
    r.count = count, r.vec = 3, r.pc = 0x02010000u + count * 4u, r.rets = 0x02020000u + count, r.sp = 0x01C7A000u;
    r.uptime_ms = 1000u * count, r.emu = 0x10, r.dbg = 0x20, r.psr = 0x30, r.build = 0xABCD1234u;
    r.flags = CL_CTX;
    for (i = 0; i < 16u; i++)
        r.r[i] = count << 8 | i;
    r.usp = 0x01C79000u, r.ssp = 0x01C78000u;
    return r;
}
static uint32_t listed(uint32_t first_count[1])   /* the history's records; first_count: the oldest's count */
{
    uint32_t at[CL_KEEP], n = cl_list(at);
    cl_rec_t r;
    first_count[0] = 0;
    if (n && cl_slot(at[0] >> 8, at[0] & 0xFFu, &r) == 1)
        first_count[0] = r.count;
    return n;
}
static int ordered(void)                       /* the listed records' counts and seqs go up by one each */
{
    uint32_t at[CL_KEEP], n = cl_list(at), k;
    cl_rec_t a, b;
    for (k = 1; k < n; k++) {
        if (cl_slot(at[k - 1u] >> 8, at[k - 1u] & 0xFFu, &a) != 1 || cl_slot(at[k] >> 8, at[k] & 0xFFu, &b) != 1)
            return 0;
        if (b.seq != a.seq + 1u || b.count != a.count + 1u)
            return 0;
    }
    return 1;
}
static void request(const char *frame_tail)    /* a request from the computer: F0 7D 4C 48 <tail> F7 */
{
    uint8_t p[16] = {0x7D, 0x4C, 0x48};
    uint32_t n = 3;
    while (*frame_tail)
        p[n++] = (uint8_t)*frame_tail++;
    nsent = 0;
    check("... the request is the history's", cl_request(p, n) == 1);
}

int main(void)
{
    uint32_t c, n, first, kept = 0, busy = 0, i;
    cl_rec_t r;
    memset(nor, 0xFF, sizeof nor);

    request("");
    check("empty flash: no records, \"end 0\"", nsent == 1 && !strcmp(sent[0], "end 0") && sent_h2[0] == 0x48);
    {
        const uint8_t other[] = {0x7D, 0x4C, 0x47};
        check("the log's own request (LG) is not the history's", cl_request(other, 3) == 0);
    }

    r = rec(1);
    check("a crash appended", cl_append(&r) == 0);
    n = listed(&first);
    check("... listed: 1, count 1", n == 1 && first == 1);
    request("");
    check("... the readout: crash, regs, end 1", nsent == 3 && !strcmp(sent[2], "end 1"));
    check("... the crash frame's fixed form",
          !strcmp(sent[0], "crash 1/1 n 1 vec 00000003 pc 02010004 rets 02020001 sp 01C7A000 ms 1000 emu 00000010 "
                           "dbg 00000020 psr 00000030 build ABCD1234 seq 1"));
    check("... the regs frame's fixed form",
          !strncmp(sent[1], "regs 1/1 r0 00000100 r1 00000101 ", 33) && strstr(sent[1], " r15 0000010F usp 01C79000 ssp 01C78000") &&
          strlen(sent[1]) == 232u);

    for (c = 2; c <= 75; c++) {                /* past both sectors (60 slots) twice over */
        r = rec(c);
        if (cl_append(&r)) {
            check("append 2..75", 0);
            break;
        }
    }
    n = listed(&first);
    check("75 appended: the newest 8 listed (68..75), oldest first", n == 8 && first == 68 && ordered());
    check("... nothing in a sector's last 256 bytes", nor[CL_LO + 0xF00u] == 0xFF && nor[CL_LO + 0x1FFFu] == 0xFF);
    request("");
    check("... the readout: 8 records x 2 frames, then \"end 8\"", nsent == 17 && !strcmp(sent[16], "end 8") &&
          !strncmp(sent[0], "crash 1/8 n 68 ", 15) && !strncmp(sent[14], "crash 8/8 n 75 ", 15));
    r = rec(76);
    r.flags = 0;                               /* (no registers: the ring no longer held them) */
    cl_append(&r);
    request("");
    check("a record without registers: no regs frame", nsent == 16 && !strncmp(sent[14], "crash 8/8 n 76 ", 15) &&
          !strcmp(sent[15], "end 8"));

    /* a power cut at every program of an append: the record absent or whole, the 8 before it intact */
    for (i = 0; i < 2u; i++) {
        uint32_t seq_before;
        cl_scan_t t;
        cl_scan(&t);
        seq_before = t.seq;
        prog_left = (int)i;
        r = rec(77);
        cl_append(&r);
        prog_left = -1;
        n = listed(&first);
        check(i ? "power cut before the magic: absent, the rest kept" : "power cut before the body: absent, the rest kept",
              n == 8 && first == 69 && ordered());
        cl_scan(&t);
        check("... the newest still the one before", t.seq == seq_before);
    }
    r = rec(77);
    check("the next append after the cut ones: kept", cl_append(&r) == 0);
    n = listed(&first);
    check("... listed, after the slots cut short", n == 8 && first == 70 && ordered());

    /* fill the newest's sector, then cut the erase of the other one: nothing older lost */
    for (c = 77;;) {
        cl_scan_t t;
        cl_scan(&t);
        if (t.next >= CL_SLOTS)
            break;
        r = rec(++c);
        cl_append(&r);
    }
    n = listed(&first);
    erase_cut = 1;
    r = rec(c + 1u);
    check("the switch's erase cut short: the append fails", cl_append(&r) != 0);
    {
        uint32_t f2;
        check("... the 8 newest still there", listed(&f2) == 8 && f2 == first && ordered());
    }
    check("the next append: the other sector erased again, kept", cl_append(&r) == 0);
    n = listed(&first);
    check("... listed", n == 8 && first == c + 1u - 7u && ordered());

    /* the boot: a restart keeps .noinit (kept), a power-off zeroes it */
    cl_clear();
    request("CLR");
    check("the clear answers \"cleared\"", nsent == 1 && !strcmp(sent[0], "cleared"));
    request("");
    check("... then the history is empty", nsent == 1 && !strcmp(sent[0], "end 0"));
    r = rec(1);
    check("boot after crash 1: kept", cl_boot(&r, &kept, &busy) == 1 && kept == 1);
    r = rec(1);
    check("a restart, the same crash: not again", cl_boot(&r, &kept, &busy) == 0);
    r = rec(2);
    check("crash 2: kept", cl_boot(&r, &kept, &busy) == 1);
    kept = busy = 0;                           /* a power-off: .noinit zero; the next crash is count 1 again */
    r = rec(1);
    r.uptime_ms = 5;
    check("a power-off, then crash 1 again: kept (not taken for the old 1)", cl_boot(&r, &kept, &busy) == 1);
    n = listed(&first);
    check("... three records", n == 3 && first == 1);
    busy = 1;                                  /* the save of crash 2 struck by a crash (count 2) */
    r = rec(2);
    check("a save cut short: the next boot keeps nothing", cl_boot(&r, &kept, &busy) < 0 && !busy && kept == 2);
    r = rec(2);
    check("... nor the one after", cl_boot(&r, &kept, &busy) == 0);
    check("... still three", listed(&first) == 3);

    /* a frame of another form under LH: taken (not the app's), nothing done */
    request("X");
    check("LH with other bytes: nothing sent", nsent == 0);
    check("... nothing erased", listed(&first) == 3);

    /* the stacks' room at the crash (main_fm1.c fm1_fault: stacks.c pst_at_crash), 32-byte units in flags; guard from
     * EMU_MSG bit 3 or DBG_MSG bit 13 */
    {
        const int32_t left[3] = {1500, 6000, -1}, big[3] = {40000, 0, 31};
        cl_clear();
        r = rec(1);
        r.flags = CL_CTX | cl_stk_pack(left);
        r.emu = 1u << 3;
        cl_append(&r);
        request("");
        check("a crash with its stacks' room: crash, regs, stk, end", nsent == 4 && !strcmp(sent[3], "end 1"));
        check("... the stk line (bytes, rounded down to 32; -1 not known; guard: EMU_MSG bit 3)",
              !strcmp(sent[2], "stk 1/1 main 1472 irq 5984 c1 -1 guard 1"));
        r = rec(2);
        r.flags = CL_CTX | cl_stk_pack(big);
        r.dbg = 1u << 13;
        cl_append(&r);
        request("");
        check("... the most kept (32,704), 0 and under 32 as 0; guard: DBG_MSG bit 13",
              nsent == 7 && !strcmp(sent[5], "stk 2/2 main 32704 irq 0 c1 0 guard 1"));
        check("... a record without them (flags CL_CTX only): no stk line", cl_stk_left(CL_CTX, 0) == -1);
    }

    printf(bad ? "FAIL: %d\n" : "all ok\n", bad);
    return bad != 0;
}
