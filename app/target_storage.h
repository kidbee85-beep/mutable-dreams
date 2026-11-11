/* SPDX-License-Identifier: MIT */
#pragma once

/* Storage selection is separate from the musical engine. The only active
 * map is the unchanged, published FM-1 layout. The proposed SMK map in docs
 * is intentionally not selectable by a device build. */
#if (defined(MD_TARGET_FM1) + defined(MD_TARGET_SMK37_ELITE) + defined(MD_TARGET_SMK37_PRO)) > 1
#error "Select exactly one Mutable Dreams hardware target."
#endif

#if defined(MD_TARGET_SMK37_ELITE) || defined(MD_TARGET_SMK37_PRO)
#error "SMK-37 storage is not qualified. Complete the model-specific board, backup and recovery tests first."
#endif

#ifndef MD_TARGET_FM1
#define MD_TARGET_FM1 1
#endif

#define MD_TARGET_NAME "M-VAVE FM-1"
#define MD_TARGET_STORE_OBJECTS 24u
#define MD_TARGET_STORE_PAIR_BYTES 8192u
#define MD_TARGET_FLASH_BYTES 1048576u
#define MD_TARGET_STORE_MAP {0xFC000u,0xDC000u,0xDA000u,0x98000u,0x9a000u,0x9c000u,0x9e000u,0xa0000u,0xa2000u,0xa4000u,0xa6000u,0xa8000u,0xaa000u,0xac000u,0xae000u,0xb0000u,0xb2000u,0xb4000u,0xb6000u,0xb8000u,0xba000u,0xbc000u,0xbe000u,0xc0000u}
