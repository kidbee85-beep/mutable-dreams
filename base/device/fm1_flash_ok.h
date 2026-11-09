/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Laurens (@profikid) */
/* Which flash parts the FM-1's storage may use (main_fm1.c's boot, plat_fm1.c's data area).
 *
 * The stock FM-1 carries a Puya P25Q80 (JEDEC 85 60 14, 1 MiB). Units sold since autumn 2026 (mostly
 * AliExpress) carry a UC25HQ80 (JEDEC B3 60 14, 1 MiB, the same plain SPI NOR commands) instead.
 * A firmware that only knows the Puya takes those units for unknown: its saves stay in RAM and its
 * update loader refuses to install.
 *
 * The list lives here, in fm1-base, and not in felucca-bsp's fm1_flash.h: the chip layer is header-only
 * and shared with every other firmware on it, while this list is a policy of this platform's storage.
 * An app that needs a different one (a part it has checked for itself) defines FM1B_FLASH_JEDEC_OK
 * before including this header.
 *
 * Any other answer: RAM only, nothing written. */
#ifndef FM1B_FLASH_OK_H
#define FM1B_FLASH_OK_H

/* the Puya P25Q80 (the stock part) and the UC25HQ80 (the newer units' part): both 1 MiB */
#ifndef FM1B_FLASH_JEDEC_OK
#define FM1B_FLASH_JEDEC_OK(id) ((id) == 0x856014u || (id) == 0xB36014u)
#endif

#endif
