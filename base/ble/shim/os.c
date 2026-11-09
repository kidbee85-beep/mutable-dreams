/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-ble, fm1-ctl and fm1-radio) */
/* JieLi's OS API (include_lib/system/os/os_api.h), the part the BT and WiFi libraries call, on a small
 * cooperative scheduler. Task 0 is the firmware's main loop; task_create() adds the library's tasks
 * (BT: btctrler, btstack, btencry; WiFi: tasklet, mlme, cmdq, rx irq, tcpip, connect, wpa), each with its
 * own stack and message queue. A task runs until it
 * waits (os_taskq_pend, os_sem_pend, os_mutex_pend, os_time_dly); the main loop calls
 * shim_run() to let every task that can go do so. Posts may come from interrupts (the radio's):
 * the queues and counts are changed with interrupts off.
 *
 * Messages are int arrays: msg[0] the type (Q_MSG / Q_EVENT / Q_CALLBACK / Q_USER | n), then the
 * arguments. os_taskq_pend returns OS_TASKQ (13) with one copied out, as the libraries expect. */
#include <stdint.h>
#include <string.h>
#include "shim.h"
#include <stddef.h>
void *malloc(size_t n);

/* SHIM_WIFI (fm1.toml wifi = true): JieLi's OS as the WiFi library needs it (fm1-radio's): ten more tasks, each with
 * its own interrupt nesting (unless SHIM_GLOBAL_IRQ), timers run by the task that added them, os_time_dly(0) a
 * yield, a pend of -1 ticks no wait, a wait nested in a callback given back its state. Without it (Bluetooth only)
 * the shim behaves as it always has: one interrupt nesting, timers from the main loop. */
#if !SHIM_WIFI && !defined SHIM_GLOBAL_IRQ
#define SHIM_GLOBAL_IRQ
#endif
#if SHIM_WIFI
#define NTASK 14                      /* main + BT (3) + WiFi (7 seen: tasklet, mlme, cmdq, rx irq, tcpip, connect, wpa) */
#else
#define NTASK 4                       /* main + btctrler, btstack, btencry */
#endif
#if SHIM_WIFI
#define QLEN 64                       /* ints per task queue: WiFi's tasks wait on semaphores, only sys_timer posts
                                       * (4 deep at most, measured on fm1-radio): the room goes to the heap (misc.c) */
#else
#define QLEN 512                      /* ints per task queue (BT posts bursts) */
#endif
#ifndef SHIM_STACK_WORDS
#define SHIM_STACK_WORDS 2048         /* fm1.toml ble_stack_kb: an app short of pool may ask for less (radio.c logs
                                       * each task's deepest, "stacks", 10 s after Bluetooth is ready) */
#endif
#define STACK_WORDS SHIM_STACK_WORDS  /* 8 KiB per task, in the pool: an interrupt (the audio render) nests on top */

enum { W_NONE, W_TASKQ, W_SEM, W_MUTEX, W_TIME };
typedef struct {
    char name[24];                    /* a copy: the WiFi library names tasks from a reused buffer */
    void (*fn)(void *);
    void *arg;
    uint32_t sp;
    uint8_t used, wait;
    void *wobj;                       /* the semaphore / mutex waited for */
    uint32_t deadline;                /* ms, 0 = forever */
    uint32_t irq_depth;               /* its critical-section nesting while switched out */
    int *pid;                         /* thread_fork's pid: zeroed when the task ends (JieLi thread_fork_kill) */
    uint8_t kill_req;                 /* thread_kill asked it to stop (KILL_WAIT / KILL_REQ) */
    int q[QLEN];                      /* ring of messages: [len, type, args...] */
    uint32_t qr, qw;
} task_t;

static task_t tasks[NTASK];
#if SHIM_WIFI
/* WiFi: a slot's stack from the heap when its first task starts, kept with the slot (a killed task's stack may still
 * be pointed into): slots the library never uses cost nothing, and their room is the heap's (fm1-radio's HTTPS
 * stream ran it out with 13 stacks fixed in the pool) */
static uint32_t *stk[NTASK - 1];
static uint16_t stk_words[NTASK - 1];             /* each slot's stack, in words */
static void *stk_mem[NTASK - 1];                  /* each one as malloc gave it (before the alignment) */
void free(void *p);
#define STACK(i) stk[(i) - 1]
#define STACK_N(i) stk_words[(i) - 1]
extern void *shim_heap_base(void);
uint32_t shim_stacks_lo(void) { return (uint32_t)(uintptr_t)shim_heap_base(); }
/* a task's stack by its name: the WiFi library's measured deepest (fm1-radio, with the audio render nesting on
 * them): tasklet and RtmpMlmeTask 3.3 KB, tcpip_thread 1.3 KB, RtmpCmdQTask and wl_rx_irq_thread hardly any;
 * the rest (the app's, TLS's 6 KB handshake among them) 8 KB */
static uint32_t stack_words_for(const char *name)
{
    static const struct { const char *name; uint16_t words; } S[] = {
        {"tasklet", 1536}, {"RtmpMlmeTask", 1536}, {"tcpip_thread", 1024}, {"RtmpCmdQTask", 1024},
        {"wl_rx_irq_thread", 1024}, {"sys_timer", 1024}};
    unsigned k;
    for (k = 0; name && k < sizeof S / sizeof S[0]; k++)
        if (!strcmp(name, S[k].name))
            return S[k].words;
    return STACK_WORDS;
}
static int stack_get(int i, const char *name)
{
    uint32_t words = stack_words_for(name);
    if (!STACK(i) || STACK_N(i) < words) {
        uint8_t *p = malloc(words * 4u + 8u);
        if (!p) {
            shim_log("task: no memory for a stack");
            return -1;
        }
        free(stk_mem[i - 1]);                     /* a smaller one goes back: dropped, every WiFi restart lost ~15 KB
                                                   * (fm1-radio: then AAC's 88 KB decoder no longer fit). No riskier
                                                   * than reusing it, which writes over it all the same */
        stk_mem[i - 1] = p;
        STACK(i) = (uint32_t *)(((uintptr_t)p + 7u) & ~(uintptr_t)7u);   /* 8-aligned */
        STACK_N(i) = (uint16_t)words;
    }
    return 0;
}
#else
static uint32_t stacks[NTASK - 1][STACK_WORDS] __attribute__((aligned(8), section(".pool")));
#define STACK(i) stacks[(i) - 1]
#define STACK_N(i) STACK_WORDS
uint32_t shim_stacks_lo(void) { return (uint32_t)(uintptr_t)&stacks[0][0]; }
static int stack_get(int i, const char *name) { (void)i, (void)name; return 0; }
#endif
task_t *shim_cur = &tasks[0];
uint32_t shim_task_now __attribute__((section(".noinit")));   /* the task running (a watchdog reset's breadcrumb) */
static uint32_t ticks_ms(void) { return shim_ms(); }

void shim_os_init(void)
{
    memset(tasks, 0, sizeof tasks);
    strcpy(tasks[0].name, "app_core");
    tasks[0].used = 1;
    shim_cur = &tasks[0];
}

static task_t *find(const char *name)
{
    int i;
    for (i = 0; i < NTASK; i++)
        if (tasks[i].used && name && !strcmp(tasks[i].name, name))
            return &tasks[i];
    return 0;
}

/* ---- the queue: one ring of ints per task, each message stored as its length then its words */
static uint32_t q_used(const task_t *t) { return t->qw - t->qr; }

static int q_put(task_t *t, int type, int argc, const int *argv)
{
    uint32_t f = shim_irq_save(), i;
    if (QLEN - q_used(t) < (uint32_t)argc + 2u) {
        shim_irq_restore(f);
        return OS_Q_FULL;
    }
    t->q[t->qw++ % QLEN] = argc + 1;
    t->q[t->qw++ % QLEN] = type;
    for (i = 0; i < (uint32_t)argc; i++)
        t->q[t->qw++ % QLEN] = argv[i];
    shim_irq_restore(f);
    return OS_NO_ERR;
}

static int q_get(task_t *t, int *argv, int argc)
{
    uint32_t f = shim_irq_save();
    int n, i;
    if (!q_used(t)) {
        shim_irq_restore(f);
        return 0;
    }
    n = t->q[t->qr++ % QLEN];
    for (i = 0; i < n; i++) {
        int v = t->q[t->qr++ % QLEN];
        if (i < argc)
            argv[i] = v;
    }
    shim_irq_restore(f);
    return n;                                     /* words: the type and its arguments */
}

/* ---- scheduling */
static int can_run(const task_t *t)
{
    if (!t->used)
        return 0;
    if (t->deadline && (int32_t)(ticks_ms() - t->deadline) >= 0)
        return 1;
    switch (t->wait) {
    case W_NONE: return 1;
    case W_TASKQ: return q_used(t) != 0;
    case W_SEM: return ((shim_sem_t *)t->wobj)->count > 0;
    case W_MUTEX: return ((shim_sem_t *)t->wobj)->owner == 0 || ((shim_sem_t *)t->wobj)->owner == t;
    case W_TIME: return 0;            /* only the deadline */
    default: return 1;
    }
}

extern void shim_ctx_switch(uint32_t *save_sp, uint32_t new_sp);

static void switch_to(task_t *n)
{
    task_t *o = shim_cur;
    if (n == o)
        return;
#ifndef SHIM_GLOBAL_IRQ
    o->irq_depth = shim_irq_hold();
#endif
    shim_cur = n;
    shim_task_now = (uint32_t)(n - tasks);
    shim_ctx_switch(&o->sp, n->sp);
#ifndef SHIM_GLOBAL_IRQ
    shim_irq_resume(shim_cur->irq_depth);         /* back in o: its own interrupt state */
#endif
}

/* the current task waits: run the others (round robin) until it can go on */
static void block(void)
{
    for (;;) {
        int k, i = (int)(shim_cur - tasks);
        for (k = 1; k <= NTASK; k++) {
            task_t *t = &tasks[(i + k) % NTASK];
            if (t == shim_cur) {
                if (can_run(t))
                    return;
            } else if (can_run(t)) {
                switch_to(t);
                break;
            }
        }
        if (k > NTASK) {             /* nobody can run: idle, with the interrupts on whatever this task holds
                                      * (FreeRTOS's idle task has its own state; what wakes us comes from them) */
#ifndef SHIM_GLOBAL_IRQ
            uint32_t d = shim_irq_hold();
            shim_irq_resume(0);
            shim_idle();
            (void)shim_irq_hold();
            shim_irq_resume(d);
#else
            shim_idle();
#endif
        }
        if (can_run(shim_cur))
            return;
    }
}

/* the main loop's slice: every BT task that can go runs until it waits again */
void shim_run(void)
{
    int pass, i;
    for (pass = 0; pass < 8; pass++) {
        int ran = 0;
        for (i = 1; i < NTASK; i++)
            if (can_run(&tasks[i])) {
                switch_to(&tasks[i]);
                ran = 1;
            }
        if (!ran)
            break;
    }
}

extern volatile uint32_t shim_isr_blocks;
void shim_timers_drop_owner(int owner);

/* a wait asked for in an interrupt would switch tasks on the interrupt's stack: refused (counted) */
static void wait_on(int what, void *obj, int timeout_ticks)
{
    if (shim_in_irq()) {
        shim_isr_blocks++;
        return;
    }
#if SHIM_WIFI
    {   /* a wait may nest (a callback run while waiting): the outer one's state comes back after */
        uint8_t ow = shim_cur->wait;
        void *oo = shim_cur->wobj;
        uint32_t od = shim_cur->deadline;
        shim_cur->wait = (uint8_t)what;
        shim_cur->wobj = obj;
        shim_cur->deadline = timeout_ticks > 0 ? ticks_ms() + (uint32_t)timeout_ticks * 10u : 0;
        if (shim_cur->deadline == 0 && timeout_ticks > 0)
            shim_cur->deadline = 1;
        if (timeout_ticks < 0)                    /* a yield: due now, the others go first */
            shim_cur->deadline = ticks_ms() ? ticks_ms() : 1u;
        block();
        shim_cur->wait = ow;
        shim_cur->wobj = oo;
        shim_cur->deadline = od;
    }
#else
    shim_cur->wait = (uint8_t)what;
    shim_cur->wobj = obj;
    shim_cur->deadline = timeout_ticks > 0 ? ticks_ms() + (uint32_t)timeout_ticks * 10u : 0;
    if (shim_cur->deadline == 0 && timeout_ticks > 0)
        shim_cur->deadline = 1;
    block();
    shim_cur->wait = W_NONE;
    shim_cur->deadline = 0;
#endif
}

void shim_task_entry(void)
{
    task_t *t = shim_cur;
#ifndef SHIM_GLOBAL_IRQ
    shim_irq_resume(0);                           /* a new task: no critical section of its own */
#endif
    t->fn(t->arg);
    if (t->pid)
        *t->pid = 0;                  /* JieLi's thread_fork_kill: a KILL_WAIT waits for this */
    shim_timers_drop_owner((int)(t - tasks));
    t->used = 0;                      /* a task that returns ends */
    block();
}

static int last_created;
int shim_task_last(void) { return last_created; }

/* slots of killed tasks, by when (0 = clean): something of the library may still point into a killed
 * task's stack (its locals), so such a slot is the last one handed out, the oldest kill first */
static uint32_t killed_at[NTASK], kill_seq;
void shim_timers_drop_owner(int owner);

#if SHIM_WIFI
static uint32_t stack_words_for(const char *name);
/* WiFi starting again: the stacks of the tasks that ended (the last session's, the library's and the app's) back to
 * the heap, their slots clean. Kept, each restart's tasks took clean slots and new stacks while the old ones stayed
 * with their killed slots: ~20 KB a restart until every slot had one (fm1-radio: then AAC's 88 KB decoder did not
 * fit). The library starts from nothing after it, so nothing of it is left pointing into them. */
void shim_stacks_release(void)
{
    int i;
    for (i = 1; i < NTASK; i++)
        if (!tasks[i].used && STACK(i)) {
            free(stk_mem[i - 1]);
            stk_mem[i - 1] = 0;
            STACK(i) = 0;
            STACK_N(i) = 0;
            killed_at[i] = 0;
        }
}
#endif
static int free_slot(const char *name)
{
    int i, best = 0, fit = 0;
    (void)name;
    for (i = 1; i < NTASK; i++)
        if (!tasks[i].used) {
            if (!killed_at[i])
                return i;
            if (!best || killed_at[i] < killed_at[best])
                best = i;
#if SHIM_WIFI                                     /* among the killed: the oldest whose stack is big enough already */
            if (STACK(i) && STACK_N(i) >= stack_words_for(name) && (!fit || killed_at[i] < killed_at[fit]))
                fit = i;
#endif
        }
    return fit ? fit : best;
}

int task_create(void (*fn)(void *), void *p, const char *name)
{
    int i = free_slot(name), k;
    if (i && stack_get(i, name) == 0) {
            task_t *t = &tasks[i];
            uint32_t *top = &STACK(i)[STACK_N(i)];
            killed_at[i] = 0;
            memset(t, 0, sizeof *t);
            for (k = 0; name && name[k] && k < (int)sizeof t->name - 1; k++)
                t->name[k] = name[k];
            t->name[k] = 0;
            t->fn = fn;
            t->arg = p;
            for (k = 0; k < (int)STACK_N(i) - 13; k++)
                STACK(i)[k] = 0x5354414Bu;               /* "STAK": how deep it went (shim_os_dump) */
            for (k = 1; k <= 13; k++)
                top[-k] = (uint32_t)(uintptr_t)shim_task_entry;
            t->sp = (uint32_t)(uintptr_t)(top - 13);
            t->used = 1;
            last_created = i;
            return 0;
    }
    return OS_NO_MORE_TCB;
}

int task_kill(const char *name)
{
    task_t *t = find(name);
    if (t && t != &tasks[0])
        shim_task_end((int)(t - tasks));
    return 0;
}

/* thread_fork's pid: the task's slot (1..NTASK-1); 0 = none */
int shim_task_pid(const char *name)
{
    task_t *t = find(name);
    int i;
    for (i = NTASK - 1; i > 0; i--)               /* the newest of that name: task_create fills from 1 up */
        if (tasks[i].used && name && !strcmp(tasks[i].name, name))
            return i;
    return t ? (int)(t - tasks) : 0;
}
/* end a task outright (the JieLi OS deletes it; the WiFi library's threads never poll a kill request) */
void shim_task_end(int i)
{
    if (i <= 0 || i >= NTASK || !tasks[i].used)
        return;
    if (tasks[i].pid)
        *tasks[i].pid = 0;
    shim_timers_drop_owner(i);
    tasks[i].used = 0;
    tasks[i].name[0] = 0;
    killed_at[i] = ++kill_seq;
    if (&tasks[i] == shim_cur)                    /* itself: never comes back */
        block();
}

/* task's deepest stack so far, in 256-byte units (the "STAK" fill left untouched) */
int shim_stack_depth(int i)
{
    int u = 0;
    if (i < 1 || i >= NTASK || !tasks[i].used || !STACK(i))
        return 0;
    while (u < (int)STACK_N(i) && STACK(i)[u] == 0x5354414Bu)
        u++;
    return ((int)STACK_N(i) - u) * 4 / 256;
}

/* for the screen: each task and what it waits for */
void shim_os_dump(void)
{
    static const char *const W[] = {"run", "taskq", "sem", "mutex", "time"};
    int i;
    for (i = 0; i < NTASK; i++)
        if (tasks[i].used) {
            char b[40];
            int n = 0;
            const char *s = tasks[i].name[0] ? tasks[i].name : "?";
            while (*s && n < 14)
                b[n++] = *s++;
            b[n++] = ' ';
            s = W[tasks[i].wait < 5 ? tasks[i].wait : 0];
            while (*s && n < 22)
                b[n++] = *s++;
            b[n++] = ' ';
            b[n++] = 'q';
            b[n++] = (char)('0' + (q_used(&tasks[i]) > 9 ? 9 : q_used(&tasks[i])));
            if (i) {                                    /* stack used, of 8 KiB, in 256-byte steps */
                int u = 0;
                while (STACK(i) && u < (int)STACK_N(i) && STACK(i)[u] == 0x5354414Bu)
                    u++;
                u = ((int)STACK_N(i) - u) * 4 / 256;
                b[n++] = ' ';
                b[n++] = 's';
                b[n++] = (char)('0' + u / 10 % 10);
                b[n++] = (char)('0' + u % 10);
            }
            if (&tasks[i] == shim_cur)
                b[n++] = '*';
            b[n] = 0;
            shim_log(b);
        }
}

const char *os_current_task(void) { return shim_cur->name; }
const char *os_current_task_rom(void) { return shim_cur->name; }

void os_time_dly(int ticks)
{
    wait_on(W_TIME, 0, ticks > 0 ? ticks : SHIM_WIFI ? -1 : 1);   /* 0: a yield (FreeRTOS vTaskDelay(0)) */
}
void os_task_yield(void) { os_time_dly(0); }

/* the pieces thread_fork / thread_kill need (wifi_shim.c) */
int shim_cur_index(void) { return (int)(shim_cur - tasks); }
void shim_task_set_pid(int i, int *pid) { if (i > 0 && i < NTASK) tasks[i].pid = pid; }
void shim_task_kill_req(int i) { if (i > 0 && i < NTASK && tasks[i].used) tasks[i].kill_req = 1; }
int thread_kill_req(void) { return shim_cur->kill_req; }
int shim_task_alive(int i) { return i > 0 && i < NTASK && tasks[i].used; }
/* post to a task by its slot (a timer's owner) */
int shim_task_post_idx(int i, int type, int argc, int *argv)
{
    if (i < 0 || i >= NTASK || !tasks[i].used)
        return OS_TASK_NOT_EXIST;
    return q_put(&tasks[i], type, argc, argv);
}

int os_taskq_post_type(const char *name, int type, int argc, int *argv)
{
    task_t *t = find(name);
    if (!t) {
        char b[40] = "post: no task ";
        int k = 14;
        while (name && *name && k < 39)
            b[k++] = *name++;
        b[k] = 0;
        shim_log(b);
        return OS_TASK_NOT_EXIST;
    }
    return q_put(t, type, argc, argv);
}

int os_taskq_pend(const char *fmt, int *argv, int argc)
{
    (void)fmt;
    for (;;) {
        int n = q_get(shim_cur, argv, argc);
        if (!n) {
            if (shim_in_irq()) {
                shim_isr_blocks++;
                return OS_ERR_PEND_ISR;
            }
            wait_on(W_TASKQ, 0, 0);
        } else if (argv[0] == Q_CALLBACK && argc >= 2) {   /* [Q_CALLBACK, fn, args...]: runs here, by count */
            int na = n - 2;
            uintptr_t fn = (uintptr_t)argv[1];
            int a = argc > 2 ? argv[2] : 0, b = argc > 3 ? argv[3] : 0, c = argc > 4 ? argv[4] : 0,
                d = argc > 5 ? argv[5] : 0;
            if (na <= 0)
                ((void (*)(void))fn)();
            else if (na == 1)
                ((void (*)(int))fn)(a);
            else if (na == 2)
                ((void (*)(int, int))fn)(a, b);
            else if (na == 3)
                ((void (*)(int, int, int))fn)(a, b, c);
            else
                ((void (*)(int, int, int, int))fn)(a, b, c, d);
        } else {
            return OS_TASKQ;
        }
    }
}

int os_sem_create(OS_SEM *s, int cnt)
{
    shim_sem_t *m = (shim_sem_t *)s;
    m->magic = SHIM_SEM_MAGIC;
    m->count = cnt;
    m->owner = 0;
    return OS_NO_ERR;
}
int os_sem_set(OS_SEM *s, u16 cnt) { ((shim_sem_t *)s)->count = cnt; return OS_NO_ERR; }
int os_sem_post(OS_SEM *s)
{
    uint32_t f = shim_irq_save();
    ((shim_sem_t *)s)->count++;
    shim_irq_restore(f);
    return OS_NO_ERR;
}
int os_sem_pend(OS_SEM *s, int timeout)
{
    shim_sem_t *m = (shim_sem_t *)s;
    for (;;) {
        uint32_t f = shim_irq_save();
        if (m->count > 0) {
            m->count--;
            shim_irq_restore(f);
            return OS_NO_ERR;
        }
        shim_irq_restore(f);
        if (SHIM_WIFI && timeout < 0)
            return OS_TIMEOUT;                    /* -1: don't wait (JieLi: 0 ticks) */
        if (shim_in_irq())
            return OS_ERR_PEND_ISR;
        wait_on(W_SEM, m, timeout);
        if (timeout > 0 && m->count <= 0)
            return OS_TIMEOUT;
    }
}

int os_mutex_create(OS_MUTEX *x)
{
    shim_sem_t *m = (shim_sem_t *)x;
    m->magic = SHIM_SEM_MAGIC;
    m->count = 0;
    m->owner = 0;
    return OS_NO_ERR;
}
int os_mutex_valid(OS_MUTEX *x) { return ((shim_sem_t *)x)->magic == SHIM_SEM_MAGIC; }
int os_mutex_del(OS_MUTEX *x, int force) { (void)force; ((shim_sem_t *)x)->magic = 0; return OS_NO_ERR; }
int os_mutex_pend(OS_MUTEX *x, int timeout)
{
    shim_sem_t *m = (shim_sem_t *)x;
    for (;;) {
        if (m->owner == 0 || m->owner == shim_cur) {
            m->owner = shim_cur;
            m->count++;                            /* recursive */
            return OS_NO_ERR;
        }
        if (SHIM_WIFI && timeout < 0)
            return OS_TIMEOUT;
        if (shim_in_irq()) {
            shim_isr_blocks++;
            return OS_ERR_PEND_ISR;
        }
        wait_on(W_MUTEX, m, timeout);
        if (timeout > 0 && m->owner && m->owner != shim_cur)
            return OS_TIMEOUT;
    }
}
int os_mutex_post(OS_MUTEX *x)
{
    shim_sem_t *m = (shim_sem_t *)x;
    if (m->owner == shim_cur && --m->count <= 0) {
        m->count = 0;
        m->owner = 0;
    }
    return OS_NO_ERR;
}
