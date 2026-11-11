/* SPDX-License-Identifier: MIT */
#pragma once
#include "plat.h"
#define APP_NAME "MUTABLE DREAMS"
#define BLE_DEVICE_NAME "Mutable Dreams"
#define APP_SPLASH_RGB RGB(216,160,163)
/* Private 2.0 capture build: keep the audio reserves intact. Whole-row hashes
 * cost 960 B of DSP pool; mirror scratch lives in ordinary SRAM. This is the
 * existing fm1-base protocol, not a second framebuffer or an audio-IRQ sender. */
#define MR_SEGW 240u
#define MRQ 2048u
#define MR_SCRATCH_STORAGE
#define APP_REDRAW
#define APP_DIM_LEVELS
void app_redraw(void);
#define APP_BLOCK 256u /* 5.80 ms audio half; DSP tiles remain 64 frames. */
/* Tile the original LCD at full resolution, without borrowing audio memory.
 * System text also tiles tall glyphs; no shortened recovery/crash headings. */
#if defined(FM1B_UAC_CH) && FM1B_UAC_CH == 8
/* Private multichannel profile. Keep the regular audio and mirror buffers. */
#define DREAM_TILE_ROWS 20u
#define FM1B_UAC_RING_POOL 1
#define FELUCCA_UAC_RING_FRAMES 480u
#else
#define DREAM_TILE_ROWS 30u
#endif
#define APP_CANVAS_PX (240u*DREAM_TILE_ROWS)
#define APP_HOST_EXPECT
#define APP_HOST_CMD
int app_host_cmd(const char *,const char *,const char *,const char *,const char *);
#define APP_WEB
#define APP_BTDEV
#define APP_BLE_AT_BOOT md_ble_at_boot()
int md_ble_at_boot(void);
#define APP_SPLASH
void app_splash(void);
#define APP_OVERLOAD
#define APP_LOCAL_MONITOR
int app_local_monitor_muted(void);
#define APP_SYSEX
int app_sysex(const uint8_t *,uint32_t);
#define APP_BACKUP_OBJS {0,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22}
#define APP_BACKUP_MAGIC 0x4244444du /* "MDDB", Mutable Dreams device banks */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "project, prefs + user banks"
#define APP_BACKUP_VALIDATE
int app_backup_validate(unsigned,const void *,unsigned);
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int);
void app_backup_restored(void);
enum { OBJ_PATCH,OBJ_PANEL,OBJ_PREFS,OBJ_USER=3,OBJ_TONES=19,OBJ_BLE=23,OBJ_NOBJ=24 };
#include "target_storage.h"
typedef char md_store_count_matches_app[(OBJ_NOBJ==MD_TARGET_STORE_OBJECTS)?1:-1];
#define APP_STORE_MAP MD_TARGET_STORE_MAP
void app_init(void);
void app_render(int32_t *,uint32_t,uint32_t);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *,const char *);
void app_overload(void);
int app_expect(const char *,int *);
