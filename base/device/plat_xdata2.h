/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* PLAT_XDATA2 (plat.h, FM1B_XDATA2): the store's own sectors there are not the app's to erase or write. Included after
 * app.h (its APP_STORE_MAP) by device/plat_fm1.c and host/plat_host.c, only when the window is on. */
#pragma once
#include <stdint.h>
#include "plat.h"

/* 1: [off, off + n) is in PLAT_XDATA2 and touches a store object's A/B pair (two 4 KiB sectors from its offset) */
static int plat_xdata2_store_hit(uint32_t off, uint32_t n)
{
    static const uint32_t map[OBJ_NOBJ] = APP_STORE_MAP;
    uint32_t i;
    if (!plat_in(off, n, PLAT_XDATA2_LO, PLAT_XDATA2_HI))
        return 0;
    for (i = 0; i < OBJ_NOBJ; i++)
        if (off < map[i] + 0x2000u && map[i] < off + (n ? n : 1u))
            return 1;
    return 0;
}
