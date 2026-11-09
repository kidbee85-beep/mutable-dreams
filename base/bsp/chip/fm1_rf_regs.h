/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* The 2.4 GHz radio's calibrated registers, read for a log (whichever stack started the radio): the TX LO trims
 * (0x1191C [14:8], 0x11920 [6:0]), RX DC (0x11908), the RX filter codes (0x11910 [21:14], 0x1195C, 0x11960) and the
 * VCO bank and frequency code (0x11938 [25:19], 0x1193C [12:5]), and the analog words the trim's modes and attenuators
 * set (0x11900-0x11928); fm1-base docs/bt/RF-CALIBRATION.md §3.6, §4.1. */
#pragma once
#include <stdint.h>

#define FM1_RF_CAL_NREGS 14u
static const uint32_t FM1_RF_CAL_REGS[FM1_RF_CAL_NREGS] = {0x1191Cu, 0x11920u, 0x11908u, 0x11910u, 0x1195Cu, 0x11960u,
                                                          0x11938u, 0x1193Cu, 0x11900u, 0x11904u, 0x1190Cu, 0x11914u,
                                                          0x11924u, 0x11928u};
static void fm1_rf_cal_regs(uint32_t v[FM1_RF_CAL_NREGS])
{
    uint32_t i;
    for (i = 0; i < FM1_RF_CAL_NREGS; i++)
        v[i] = *(volatile uint32_t *)(uintptr_t)FM1_RF_CAL_REGS[i];
}
