/* SPDX-License-Identifier: MPL-2.0 AND Apache-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-ble and fm1-ctl)
 * The OS_* error codes and the Q_* message types repeat the JieLi AC79 SDK's include_lib/system/os/
 * os_error.h and os_api.h, Copyright (c) JIELI (Zhuhai Jieli Technology Co., Ltd.), under the Apache
 * License 2.0 (LICENSES/Apache-2.0.txt), so the SDK's binary libraries link against this shim. The rest
 * is MPL-2.0 (LICENSE). */
/* The BT shim: what JieLi's BT libraries expect of their system, on a Felucca-style bare-metal
 * firmware. The firmware provides the shim_* platform hooks below. */
#pragma once
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* include_lib/system/os/os_error.h */
enum { OS_NO_ERR = 0, OS_TRUE, OS_ERR_EVENT_TYPE, OS_ERR_PEND_ISR, OS_ERR_POST_NULL_PTR, OS_ERR_PEVENT_NULL,
       OS_ERR_POST_ISR, OS_ERR_QUERY_ISR, OS_ERR_INVALID_OPT, OS_ERR_TASK_WAITING, OS_ERR_PDATA_NULL, OS_TIMEOUT,
       OS_TIMER, OS_TASKQ, OS_TASK_NOT_EXIST, OS_ERR_EVENT_NAME_TOO_LONG, OS_ERR_FLAG_NAME_TOO_LONG,
       OS_ERR_TASK_NAME_TOO_LONG, OS_ERR_PNAME_NULL, OS_ERR_TASK_CREATE_ISR, OS_MBOX_FULL, OS_Q_FULL, OS_Q_EMPTY,
       OS_Q_ERR, OS_ERR_NO_QBUF, OS_PRIO_EXIST, OS_PRIO_ERR, OS_PRIO_INVALID, OS_SEM_OVF, OS_TASK_DEL_ERR,
       OS_TASK_DEL_IDLE, OS_TASK_DEL_ISR, OS_NO_MORE_TCB };
#define Q_MSG 0x100000
#define Q_EVENT 0x200000
#define Q_CALLBACK 0x300000
#define Q_USER 0x400000

/* OS_SEM / OS_MUTEX are FreeRTOS StaticSemaphore_t (~70 bytes) in the libraries' storage; ours
 * uses the first 16 bytes of it */
typedef struct {
    uint32_t magic;
    volatile int count;
    void *owner;
    uint32_t rsv;
} shim_sem_t;
#define SHIM_SEM_MAGIC 0x53454D31u
typedef struct { uint32_t opaque[20]; } OS_SEM;
typedef OS_SEM OS_MUTEX;

/* SHIM_WIFI (fm1.toml wifi = true, tools/build.py): JieLi's OS as the WiFi library needs it (os.c) */
#ifndef SHIM_WIFI
#define SHIM_WIFI 0
#endif

/* the platform (the firmware) */
uint32_t shim_ms(void);
uint32_t shim_irq_save(void);         /* interrupts off; nests. Returns whether they were on ... */
void shim_irq_restore(uint32_t f);    /* ... and the outermost restore, given that, switches them on only if they
                                       * were: safe inside an interrupt (the radio's take it), they stay off */
int shim_in_irq(void);
void shim_idle(void);                 /* the main loop has nothing to run: return soon */
void shim_request_irq(int index, int prio, void (*handler)(void));
void shim_reboot(void);
void shim_wdt(void);                  /* feed the watchdog: long hardware loops (the radio's calibration) */
int shim_task_pid(const char *name);  /* thread_fork's pid: the task's slot, 0 = none */
int shim_task_last(void);             /* the slot task_create filled last */
void shim_task_end(int pid);
uint32_t shim_irq_hold(void);         /* SHIM_WIFI: interrupts off; the current nesting (a task switch) */
void shim_irq_resume(uint32_t depth); /* SHIM_WIFI: a task resumes: its nesting, interrupts on at 0 */
void shim_timers_post(void);          /* due timers posted to their owners (a task's idle) */
void shim_delay_us(uint32_t us);      /* a busy wait in RAM (.ram_text), the watchdog fed: the radio trim's delays */
void shim_log(const char *s);         /* a line for the screen's log */
int shim_store_load(void *dst, uint32_t max);   /* syscfg's flash object */
int shim_store_save(const void *src, uint32_t len);
void shim_bt_event(int from, const void *event, int len);

/* the shim, to the firmware */
void shim_os_init(void);
uint32_t shim_stacks_lo(void);        /* the lowest address a task's sp may reach */
void shim_run(void);                  /* the main loop's slice: run every BT task that can go */
void shim_timers(void);               /* due sys_timers, from the main loop */
int task_create(void (*fn)(void *), void *p, const char *name);
