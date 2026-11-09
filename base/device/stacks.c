/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* How deep CPU0's two stacks have gone (plat.h plat_stack_left): each painted with PST_MARK at the start, below what is
 * live, then read from the bottom up to the first word written. The linker scripts (app.ld, app_wifi.ld) put them
 * under the reset-kept record, each over a 256-byte guard band (felucca-bsp chip/fm1_guard.h's write limits):
 *   main        _ustack_lo .. _ustack_top (the main loop's: crt0's sp)
 *   interrupts  _sstack_lo .. _sstack_top (ssp: the interrupts', and the fault handler's)
 * and the second core's jobs' stack through felucca-bsp's own marks (fm1_cpu1_stack_used). PST_MARK is the word
 * Optimist's system/stackmark.h paints with, so an app that paints its own the same way changes nothing here.
 *
 * At a crash (main_fm1.c fm1_fault, first thing: before the app's hook and the crash screen draw on a stack) each
 * stack's room left is read into pst_crash (.noinit), and the boot after keeps it in the crash record (crashlog.c
 * CL_STK). There and not at the boot: the crash screen's own calls and whatever the reset path runs (the boot ROM,
 * crt0) write into the stacks after the fault; the fault handler's frames before it are the only ones counted (the
 * stub's frame and fm1_fault_c's, on the stack the exception runs on: a few hundred bytes at most, unmeasured). The
 * read is RAM only, a word at a time, the interrupts already off: no flash, no lock, no call out. */
#define PST_MARK 0x4B545350u                        /* "PSTK" (Optimist's STK_MARK) */
#define PST_SLACK 64u                               /* words left unpainted under sp (and at the interrupts' top) */
extern char _ustack_top[];                          /* (the others: felucca-bsp chip/fm1_guard.h) */
#define PST_ULO ((uint32_t *)(uintptr_t)_ustack_lo)
#define PST_UTOP ((uint32_t *)(uintptr_t)_ustack_top)
#define PST_SLO ((uint32_t *)(uintptr_t)_sstack_lo)
#define PST_STOP ((uint32_t *)(uintptr_t)_sstack_top)
static uint8_t pst_painted;                         /* 1: the marks are this start's */
typedef struct {
    uint32_t count;                                 /* the crash (fm1_crash.count) these are of */
    int32_t left[3];                                /* bytes left at it, main / interrupts / second core; -1 unknown */
} pst_crash_t;
#ifndef PST_NOINIT
#define PST_NOINIT __attribute__((section(".noinit")))   /* (a host test: its own, as dbgstats.c's DS_NOINIT) */
#endif
static pst_crash_t pst_crash PST_NOINIT;

static void pst_fill(uint32_t *lo, uint32_t *hi)
{
    volatile uint32_t *p = lo;                      /* (a plain loop: no library call under sp) */
    while (p < hi)
        *p++ = PST_MARK;
}
/* the interrupts off, at fm1_main's start: sp is about the address of a local of the caller's (no asm: the build's
 * check), so PST_SLACK words under it stay as they are */
static void pst_boot(const void *sp)
{
    uint32_t *p = (uint32_t *)((uintptr_t)sp & ~(uintptr_t)3u);
    if (p < PST_ULO + PST_SLACK || p > PST_UTOP)
        return;                                     /* (sp elsewhere: nothing painted, nothing told) */
    pst_fill(PST_ULO, p - PST_SLACK);
    pst_fill(PST_SLO, PST_STOP - PST_SLACK);
    pst_painted = 1;
}
static int32_t pst_unused(const uint32_t *lo, const uint32_t *top)   /* bytes of the mark from lo up */
{
    const volatile uint32_t *p = lo;
    while (p < top && *p == PST_MARK)
        p++;
    return (int32_t)((p - lo) * 4);
}
int32_t plat_stack_left(int k)
{
    if (k == PLAT_STK_C1) {
#if FM1B_CPU2
        uint32_t used = plat_cpu2_stack(), n = FM1_CPU1_USTACK_BYTES - 256u;
        return used ? (int32_t)(n > used ? n - used : 0u) : -1;
#else
        return -1;
#endif
    }
    if (!pst_painted || (k != PLAT_STK_MAIN && k != PLAT_STK_IRQ))
        return -1;
    return k == PLAT_STK_MAIN ? pst_unused(PST_ULO, PST_UTOP) : pst_unused(PST_SLO, PST_STOP);
}
static void pst_at_crash(uint32_t count)            /* fm1_fault: each stack's room now, for the boot's record */
{
    int k;
    pst_crash.count = count;
    for (k = 0; k < 3; k++)
        pst_crash.left[k] = plat_stack_left(k);
}
