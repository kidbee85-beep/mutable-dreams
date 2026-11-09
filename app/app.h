/* SPDX-License-Identifier: MIT */
#pragma once
#include "plat.h"
#define APP_NAME "MUTABLE DREAMS"
#define BLE_DEVICE_NAME "Mutable Dreams"
#define APP_SPLASH_RGB RGB(216,160,163)
#define APP_BLOCK 256u /* 5.80 ms audio half; DSP tiles remain 64 frames. */
#define APP_CANVAS_PX (240u*40u)
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
#define APP_STORE_MAP {0xFC000u,0xDC000u,0xDA000u,0x98000u,0x9a000u,0x9c000u,0x9e000u,0xa0000u,0xa2000u,0xa4000u,0xa6000u,0xa8000u,0xaa000u,0xac000u,0xae000u,0xb0000u,0xb2000u,0xb4000u,0xb6000u,0xb8000u,0xba000u,0xbc000u,0xbe000u,0xc0000u}
void app_init(void);
void app_render(int32_t *,uint32_t,uint32_t);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *,const char *);
void app_overload(void);
int app_expect(const char *,int *);
