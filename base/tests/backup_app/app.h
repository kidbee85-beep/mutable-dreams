/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* The backup test app (tests/emu_backup_test.mjs, tests/backup_app/tests/backup.txt): SEL counts a saved value up and
 * saves it, PLAY toggles "playing" (a backup then leaves out what is not saved; a restore is refused). The lit buttons
 * show the saved value (bits 0..13). No sound, a black screen. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "BKTEST"
#define APP_SPLASH_RGB RGB(200, 160, 250)
#define APP_BLOCK 256u

enum { OBJ_STATE, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

#define APP_BACKUP_OBJS {OBJ_STATE}
#define APP_BACKUP_MAGIC 0x42544252u            /* "RBTB" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces the count"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
