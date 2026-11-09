/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* A host model of the parts of felucca-bsp's chip/fm1_cpu1.h that device/cpu2.c and device/stacks.c use, for
 * tests/stack_c1_test.c: the jobs' stack is a zeroed array (.bss) until fm1_cpu1_start marks it with FM1_CPU1_MARK (not
 * when the start is barred: it returns before), fm1_cpu1_stack_used counts the words from the mark's end, as the bsp
 * does. The registers and the second core itself are not modelled. */
#pragma once
#include <stdint.h>

enum { FM1_CPU1_MISREAD = 1, FM1_CPU1_LATE = 2, FM1_CPU1_BARRED = 3, FM1_CPU1_FAULT = 4, FM1_CPU1_SILENT = 5 };
#define FM1_CPU1_MARK 0x43314D4Bu
#ifndef FM1_CPU1_USTACK_BYTES
#define FM1_CPU1_USTACK_BYTES 2816
#endif
#define FM1_CPU1_USTACK_WORDS (FM1_CPU1_USTACK_BYTES / 4u)
#define FM1_CPU1_FALLBACK 1

static uint32_t fm1_cpu1_ustack[FM1_CPU1_USTACK_WORDS];   /* (.bss: zero at power-on) */
static uint8_t fm1_cpu1_on, fm1_cpu1_gave_up;
static struct { uint32_t rejected; } fm1_cpu1_mb;
static int fm1_cpu1_barred_now;                           /* the test's switch: 1 = barred */
static int fm1_cpu1_barred(void) { return fm1_cpu1_barred_now; }
static void fm1_cpu1_bar_build(uint32_t build) { (void)build; }
static void fm1_cpu1_stop(void) {}
static int fm1_cpu1_start(void)
{
    uint32_t i;
    if (fm1_cpu1_on)
        return 0;
    if (fm1_cpu1_barred()) {
        fm1_cpu1_gave_up = FM1_CPU1_BARRED;
        return -1;
    }
    for (i = 0; i < FM1_CPU1_USTACK_WORDS; i++)
        fm1_cpu1_ustack[i] = FM1_CPU1_MARK;
    fm1_cpu1_on = 1;
    return 0;
}
static int fm1_cpu1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn, (void)arg; return 0; }
static int fm1_cpu1_wait(void) { return 0; }
static void fm1_cpu1_sync(void) {}
static int fm1_cpu1_busy(void) { return 0; }
static uint32_t fm1_cpu1_stack_used(void)
{
    uint32_t i = 0;
    while (i < FM1_CPU1_USTACK_WORDS && fm1_cpu1_ustack[i] == FM1_CPU1_MARK)
        i++;
    return (FM1_CPU1_USTACK_WORDS - i) * 4u;
}
