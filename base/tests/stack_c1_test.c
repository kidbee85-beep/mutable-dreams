/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The second core's stack room (plat_stack_left(PLAT_STK_C1), device/stacks.c over device/cpu2.c's plat_cpu2_stack) on
 * a host model of felucca-bsp's fm1_cpu1.h (tests/stack_c1/fm1_cpu1.h): CPU1's jobs' stack is zeroed .bss until a start
 * marks it. A boot where CPU1 never started (2ND CORE off, the fallback after a failed boot, barred) must read "not
 * known" (-1), not 0 bytes left (a zeroed stack looks used to the bottom). After a start the room is what the jobs
 * left: the whole stack less its guard, less the depth they went.
 *
 *   cc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -O2 -Itests/stack_c1 -o stack_c1_test tests/stack_c1_test.c && ./stack_c1_test */
#include <stdint.h>
#include <stdio.h>
#include "../include/plat.h"

#define FM1B_CPU2 1
#define FM1B_BUILD 0x12345678u
#define PST_NOINIT                        /* (plain .bss here: a Mach-O section needs a segment) */
#include "fm1_cpu1.h"
struct bootguard_s { uint32_t magic, failed, pending; };
static struct bootguard_s bootguard;
char _ustack_lo[16], _ustack_top[16], _sstack_lo[16], _sstack_top[16];   /* (the linker's: CPU0's stacks, unused here) */
#include "../device/cpu2.c"
#include "../device/stacks.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

int main(void)
{
    const int32_t whole = (int32_t)FM1_CPU1_USTACK_BYTES - 256;
    uint32_t i;

    check("CPU1 never started: plat_cpu2_stack says 0", plat_cpu2_stack() == 0u);
    check("... and plat_stack_left(C1) says -1 (not 0 left)", plat_stack_left(PLAT_STK_C1) == -1);

    cpu2_booting = 1;
    fm1_cpu1_barred_now = 1;
    check("a barred start fails", plat_cpu2_set(1) == -1);
    check("... still 0: its stack was not marked", plat_cpu2_stack() == 0u && plat_stack_left(PLAT_STK_C1) == -1);
    check("... barred is what it said", fm1_cpu1_gave_up == FM1_CPU1_BARRED);

    fm1_cpu1_barred_now = 0;
    bootguard.failed = 1;                      /* the fallback after a failed boot: no start */
    check("the fallback after a failed boot: no start", plat_cpu2_set(1) == -1);
    check("... -1", plat_stack_left(PLAT_STK_C1) == -1);
    bootguard.failed = 0;

    check("a start works", plat_cpu2_set(1) == 0 && plat_cpu2_on());
    check("... the stack is marked, nothing used yet: 0 bytes used reads as not known (-1)",
          plat_cpu2_stack() == 0u && plat_stack_left(PLAT_STK_C1) == -1);
    for (i = FM1_CPU1_USTACK_WORDS - 100u; i < FM1_CPU1_USTACK_WORDS; i++)
        fm1_cpu1_ustack[i] = 0xDEADBEEFu;      /* (a job used 400 bytes at the top) */
    check("... the jobs went 400 bytes deep", plat_cpu2_stack() == 400u);
    check("... the room left is the stack less its guard and the 400", plat_stack_left(PLAT_STK_C1) == whole - 400);
    for (i = 0; i < FM1_CPU1_USTACK_WORDS; i++)
        fm1_cpu1_ustack[i] = 0xDEADBEEFu;      /* (the whole stack used) */
    check("... a stack used to the bottom: 0 left", plat_stack_left(PLAT_STK_C1) == 0);
    check("plat_stack_left of a stack that does not exist: -1", plat_stack_left(7) == -1);

    printf(bad ? "FAIL: %d\n" : "all ok\n", bad);
    return bad != 0;
}
