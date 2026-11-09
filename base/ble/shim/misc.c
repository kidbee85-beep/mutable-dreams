/* SPDX-License-Identifier: MPL-2.0 AND Apache-2.0
 * Copyright (C) 2026 Charles Vestal (from fm1-ble and fm1-ctl)
 * A few interface details repeat the JieLi AC79 SDK, Copyright (c) JIELI (Zhuhai Jieli Technology Co.,
 * Ltd.), under the Apache License 2.0 (LICENSES/Apache-2.0.txt): the cbuffer_t layout
 * (include_lib/system/generic/circular_buf.h), the syscfg ids (include_lib/utils/syscfg/syscfg_id.h)
 * and the board's radio defaults (apps/demo/demo_ble/board/wl82/board.c). The rest is MPL-2.0 (LICENSE). */
/* The rest of what the BT libraries call: libc and a heap, lbuf / cbuf, CRCs, interrupts, the
 * (never sleeping) power hooks, syscfg (in one flash object), logging, and the calibration getters
 * the SDK's sources would give them (apps/common/config/user_cfg.c, net/wifi_conf.c). */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "shim.h"

/* ---------------------------------------------------------------- libc --- */
size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a < b)
        while (n--)
            *a++ = *b++;
    else
        while (n--)
            a[n] = b[n];
    return d;
}
char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

/* a first-fit heap with a size word before each block and a free list */
#if SHIM_WIFI
/* WiFi: 248 KB (lwIP, wpa, the driver's buffers, the tasks' stacks: os.c), in the pool */
#ifndef SHIM_HEAP_KB
#define SHIM_HEAP_KB 248u
#endif
#define HEAP_BYTES (SHIM_HEAP_KB * 1024u)
static uint32_t heap[HEAP_BYTES / 4u] __attribute__((section(".pool")));
#else
#ifndef SHIM_BLE_HEAP_KB
#define SHIM_BLE_HEAP_KB 24u          /* fm1.toml ble_heap_kb: an app short of RAM may ask for less (radio.c logs its
                                       * most ever, "heap", beside the stacks: one link with MIDI both ways took 7 KB) */
#endif
#define HEAP_BYTES (SHIM_BLE_HEAP_KB * 1024u)
/* in .bss, low in RAM, not the pool: the radio's buffers come from here, and the hardware does not reach the pool's
 * top (a CTL-1 with two links, its heap at 0x01C4F028, had stacks overwritten 0.55 s into every Bluetooth start, in
 * the radio's calibration; at 0x01C121F0 it starts cleanly) */
static uint32_t heap[HEAP_BYTES / 4u];
#endif
#if SHIM_WIFI
/* and a second part in .bss, the RAM the task queues gave up (os.c QLEN): one heap across both */
#ifndef SHIM_HEAP2_KB
#define SHIM_HEAP2_KB 24u
#endif
#define HEAP2_BYTES (SHIM_HEAP2_KB * 1024u)
static uint32_t heap2[HEAP2_BYTES / 4u] __attribute__((aligned(8)));
#else
#define HEAP2_BYTES 0u
#endif
const uint32_t shim_heap_size = HEAP_BYTES + HEAP2_BYTES;
void *shim_heap_base(void)                        /* the heap's lowest address (the tasks' stacks are in it: os.c) */
{
#if SHIM_WIFI
    return (uint8_t *)heap2 < (uint8_t *)heap ? (void *)heap2 : (void *)heap;
#else
    return heap;
#endif
}
typedef struct blk { uint32_t size; struct blk *next; } blk_t;   /* size in bytes, header included */
static blk_t *free_list;
static int heap_ready;
uint32_t shim_heap_used, shim_heap_peak;         /* bytes handed out now, and the most ever (the log, at ready) */

static void heap_init(void)
{
    free_list = (blk_t *)heap;
    free_list->size = HEAP_BYTES;
    free_list->next = 0;
#if SHIM_WIFI
    {   /* the second part, in address order (free merges neighbours by address) */
        blk_t *b = (blk_t *)heap2;
        b->size = HEAP2_BYTES;
        if ((uint8_t *)b < (uint8_t *)free_list) {
            b->next = free_list;
            free_list = b;
        } else {
            b->next = 0;
            free_list->next = b;
        }
    }
#endif
    heap_ready = 1;
}

void *malloc(size_t n)
{
    uint32_t f = shim_irq_save(), need = ((uint32_t)n + 4u + 7u) & ~7u;
    blk_t **pp, *b;
    if (!heap_ready)
        heap_init();
    if (need < sizeof(blk_t))
        need = sizeof(blk_t);
    for (pp = &free_list; (b = *pp); pp = &b->next)
        if (b->size >= need) {
            if (b->size - need >= 16u) {           /* split */
                blk_t *r = (blk_t *)((uint8_t *)b + need);
                r->size = b->size - need;
                r->next = b->next;
                *pp = r;
                b->size = need;
            } else {
                *pp = b->next;
            }
            shim_heap_used += b->size;
            if (shim_heap_used > shim_heap_peak)
                shim_heap_peak = shim_heap_used;
            shim_irq_restore(f);
            return (uint8_t *)b + 4;
        }
    shim_irq_restore(f);
    shim_log("heap: out of memory");
    return 0;
}

void free(void *p)
{
    uint32_t f;
    blk_t *b, **pp;
    if (!p)
        return;
    f = shim_irq_save();
    b = (blk_t *)((uint8_t *)p - 4);
    shim_heap_used -= b->size;
    for (pp = &free_list; *pp && *pp < b; pp = &(*pp)->next)    /* address order: merge neighbours */
        ;
    b->next = *pp;
    *pp = b;
    if (b->next && (uint8_t *)b + b->size == (uint8_t *)b->next) {
        b->size += b->next->size;
        b->next = b->next->next;
    }
    if (pp != &free_list) {
        blk_t *prev = (blk_t *)((uint8_t *)pp - offsetof(blk_t, next));
        if ((uint8_t *)prev + prev->size == (uint8_t *)b) {
            prev->size += b->size;
            prev->next = b->next;
        }
    }
    shim_irq_restore(f);
}

void *zalloc(size_t n)
{
    void *p = malloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}
void *ram_malloc(size_t n) { return malloc(n); }
void ram_free(void *p) { free(p); }
void *calloc(size_t a, size_t b) { return zalloc(a * b); }

/* ---------------------------------------------------------------- log --- */
static void fmt(char *o, uint32_t max, const char *f, va_list ap)
{
    uint32_t n = 0;
    while (*f && n + 1 < max) {
        if (*f != '%') {
            if (*f != '\r' && *f != '\n')
                o[n++] = *f;
            f++;
            continue;
        }
        f++;
        while (*f == '-' || *f == '0' || (*f >= '1' && *f <= '9') || *f == 'l' || *f == '.')
            f++;
        if (*f == 's') {
            const char *s = va_arg(ap, const char *);
            while (s && *s && n + 1 < max)
                o[n++] = *s++;
        } else if (*f == 'd' || *f == 'i' || *f == 'u' || *f == 'x' || *f == 'X' || *f == 'c' || *f == 'p') {
            uint32_t v = va_arg(ap, uint32_t), base = (*f == 'x' || *f == 'X' || *f == 'p') ? 16u : 10u;
            char t[12];
            int k = 0;
            if (*f == 'c') {
                o[n++] = (char)v;
            } else {
                if (*f == 'd' && (int32_t)v < 0) {
                    o[n++] = '-';
                    v = (uint32_t)-(int32_t)v;
                }
                do {
                    t[k++] = "0123456789abcdef"[v % base];
                    v /= base;
                } while (v && k < 11);
                while (k && n + 1 < max)
                    o[n++] = t[--k];
            }
        } else if (*f == 'f') {
            (void)va_arg(ap, double);
            o[n++] = '?';
        }
        if (*f)
            f++;
    }
    o[n] = 0;
}

int printf(const char *f, ...)
{
    char b[96];
    va_list ap;
    va_start(ap, f);
    fmt(b, sizeof b, f, ap);
    va_end(ap);
    shim_wdt();
    if (b[0])
        shim_log(b);
    return 0;
}
int puts(const char *s) { shim_log(s); return 0; }
int putchar(int c) { (void)c; return c; }
void put_buf(const u8 *buf, int len) { (void)buf; (void)len; }
void printf_buf(const u8 *buf, int len) { (void)buf; (void)len; }
void log_print(int level, const char *tag, const char *f, ...)
{
    char b[96];
    va_list ap;
    (void)tag;
    if (level > 2)
        return;
    va_start(ap, f);
    fmt(b, sizeof b, f, ap);
    va_end(ap);
    if (b[0])
        shim_log(b);
}
const int config_asser = 1;                       /* the SDK's lib_system_config.c: assertions report */
void cpu_assert_debug(void) { shim_log("BT assert"); }

/* ---------------------------------------------------------------- interrupts --- */
static uint32_t irq_saved[8];
static int irq_depth;
void local_irq_disable(void)
{
    uint32_t f = shim_irq_save();
    if (irq_depth < 8)
        irq_saved[irq_depth] = f;
    irq_depth++;
}
void local_irq_enable(void)
{
    if (irq_depth > 0 && --irq_depth < 8)
        shim_irq_restore(irq_saved[irq_depth]);
}
void __local_irq_disable(void) { local_irq_disable(); }
void __local_irq_enable(void) { local_irq_enable(); }
void sys_local_irq_disable(void) { local_irq_disable(); }
void sys_local_irq_enable(void) { local_irq_enable(); }
int cpu_in_irq(void) { return shim_in_irq(); }
#if SHIM_WIFI
uint32_t shim_irq_depth(void);
int cpu_irq_disabled(void) { return shim_irq_depth() > 0; }   /* the current task's own nesting */
#else
int cpu_irq_disabled(void) { return irq_depth > 0; }
#endif
void request_irq(u8 index, u8 priority, void (*handler)(void), u8 cpu_id)
{
    (void)cpu_id;
    shim_request_irq(index, priority, handler);
}
void unrequest_irq(u8 index) { (void)index; }
void bit_clr_ie(u8 index) { (void)index; }
void bit_set_ie(u8 index) { (void)index; }

/* ---------------------------------------------------------------- lbuf / cbuf --- */
/* lbuf: the libraries only allocate, shrink and free; the pool head is never looked inside */
void *lbuf_alloc(void *head, u32 len)
{
    uint32_t *p;
    (void)head;
    p = malloc(len + 4u);
    if (!p)
        return 0;
    p[0] = len;
    return p + 1;
}
int lbuf_free(void *lbuf)
{
    if (lbuf)
        free((uint32_t *)lbuf - 1);
    return 0;
}
void *lbuf_realloc(void *lbuf, int size)
{
    if (!lbuf || (uint32_t)size > ((uint32_t *)lbuf)[-1])
        return 0;
    ((uint32_t *)lbuf)[-1] = (uint32_t)size;
    return lbuf;
}
int lbuf_real_size(void *lbuf) { return lbuf ? (int)((uint32_t *)lbuf)[-1] : 0; }
void lbuf_free_check(void *lbuf, u32 rets) { (void)lbuf; (void)rets; }

typedef struct {                      /* include_lib/system/generic/circular_buf.h */
    u8 *begin, *end, *read_ptr, *write_ptr, *tmp_ptr;
    u32 tmp_len, data_len, total_len;
    u32 lock;
} cbuffer_t;
void cbuf_init(cbuffer_t *c, void *buf, u32 size)
{
    c->begin = c->read_ptr = c->write_ptr = c->tmp_ptr = buf;
    c->end = (u8 *)buf + size;
    c->tmp_len = c->data_len = 0;
    c->total_len = size;
}
void cbuf_clear(cbuffer_t *c)
{
    uint32_t f = shim_irq_save();
    c->read_ptr = c->write_ptr = c->tmp_ptr = c->begin;
    c->tmp_len = c->data_len = 0;
    shim_irq_restore(f);
}
u32 cbuf_write(cbuffer_t *c, void *buf, u32 len)
{
    uint32_t f = shim_irq_save(), i;
    if (c->total_len - c->data_len < len) {
        shim_irq_restore(f);
        return 0;
    }
    for (i = 0; i < len; i++) {
        *c->write_ptr++ = ((u8 *)buf)[i];
        if (c->write_ptr >= c->end)
            c->write_ptr = c->begin;
    }
    c->data_len += len;
    shim_irq_restore(f);
    return len;
}
u32 cbuf_read(cbuffer_t *c, void *buf, u32 len)
{
    uint32_t f = shim_irq_save(), i;
    if (c->data_len < len) {
        shim_irq_restore(f);
        return 0;
    }
    for (i = 0; i < len; i++) {
        ((u8 *)buf)[i] = *c->read_ptr++;
        if (c->read_ptr >= c->end)
            c->read_ptr = c->begin;
    }
    c->data_len -= len;
    shim_irq_restore(f);
    return len;
}
u32 cbuf_get_data_size(cbuffer_t *c) { return c->data_len; }

/* ---------------------------------------------------------------- CRC --- */
u16 CRC16(const void *ptr, u32 len)                /* CRC-16/CCITT-FALSE: the libraries only compare */
{
    const u8 *p = ptr;
    u16 c = 0xFFFF;
    while (len--) {
        int k;
        c ^= (u16)(*p++ << 8);
        for (k = 0; k < 8; k++)
            c = (u16)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
}
u16 crc_get_16bit(const void *src, u32 len) { return CRC16(src, len); }
u16 chip_crc16(const void *src, u32 len) { return CRC16(src, len); }
u32 crc_get_32bit(const void *src, u32 len)
{
    const u8 *p = src;
    u32 c = 0xFFFFFFFFu;
    while (len--) {
        int k;
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}

/* ---------------------------------------------------------------- power, clocks --- */
/* the BLE controller keeps this handle (hw->power_ctrl) and asserts it is not null; it is only ever
 * handed back to the low_power_* calls below */
static uint32_t low_power_handle[16];
void *low_power_get(void *priv, const void *ops) { (void)priv; (void)ops; return low_power_handle; }
void low_power_put(void *priv) { (void)priv; }
void low_power_request(char *name) { (void)name; }
void low_power_exit_request(void) {}
void low_power_on(void) {}
void low_power_trace_drift(int us) { (void)us; }
int low_power_get_osc_type(void) { return 0; }
int low_power_get_default_osc_type(void) { return 0; }
void low_power_reset_osc_type(void) {}
void low_power_hw_unsleep_lock(void) {}
void low_power_hw_unsleep_unlock(void) {}
int power_is_poweroff_post(void) { return 0; }
void P33_SYSTEM_RESET(void) { shim_reboot(); }
u32 clk_get(const char *name) { (void)name; return 24000000u; }   /* the radio asks for "osc" */

/* in RAM, as the radio trim that calls them (app.ld): no flash access while it draws its current */
#define SHIM_RAMFN __attribute__((section(".ram_text"), noinline, used))
SHIM_RAMFN void delay(u32 n) { shim_delay_us(n); }
SHIM_RAMFN void delay_us(u32 us) { shim_delay_us(us); }
SHIM_RAMFN void hw_udelay(u32 us) { shim_delay_us(us); }
void adc_add_sample_ch(u32 ch) { (void)ch; }
u32 adc_get_value(u32 ch) { (void)ch; return 0; }
void aes_hw_lock(void) {}
void aes_hw_unlock(void) {}

/* section bounds the libraries walk; nothing registered */
const int a2dp_sink_media_codec_begin[0], a2dp_sink_media_codec_end[0];

/* ---------------------------------------------------------------- syscfg --- */
/* item id -> bytes, all in one flash object: the BT address, the radio's trim, bonding keys */
#define CFG_MAX 1536u
static u8 cfg[CFG_MAX];
static u32 cfg_len = 0xFFFFFFFFu;
static void cfg_load(void)
{
    int n;
    if (cfg_len != 0xFFFFFFFFu)
        return;
    n = shim_store_load(cfg, CFG_MAX);
    cfg_len = n > 0 ? (u32)n : 0u;
}
static int cfg_find(u16 id, u32 *at, u32 *len)      /* records: id u16, len u16, bytes */
{
    u32 p = 0;
    while (p + 4u <= cfg_len) {
        u16 i = (u16)(cfg[p] | cfg[p + 1] << 8), l = (u16)(cfg[p + 2] | cfg[p + 3] << 8);
        if (i == id) {
            *at = p;
            *len = l;
            return 1;
        }
        p += 4u + l;
    }
    return 0;
}
int syscfg_read(u16 id, void *buf, u16 len)
{
    u32 at, l;
    cfg_load();
    if (!cfg_find(id, &at, &l))
        return -1;
    if (l > len)
        l = len;
    memcpy(buf, &cfg[at + 4u], l);
    return (int)l;
}
int syscfg_write(u16 id, void *buf, u16 len)
{
    u32 at, l;
    cfg_load();
    if (cfg_find(id, &at, &l)) {                   /* drop the old record */
        memmove(&cfg[at], &cfg[at + 4u + l], cfg_len - at - 4u - l);
        cfg_len -= 4u + l;
    }
    if (cfg_len + 4u + len > CFG_MAX)
        return -1;
    cfg[cfg_len] = (u8)id;
    cfg[cfg_len + 1] = (u8)(id >> 8);
    cfg[cfg_len + 2] = (u8)len;
    cfg[cfg_len + 3] = (u8)(len >> 8);
    memcpy(&cfg[cfg_len + 4u], buf, len);
    cfg_len += 4u + len;
    shim_store_save(cfg, cfg_len);
    return len;
}

/* ---------------------------------------------------------------- calibration --- */
/* the SDK board's defaults (apps/demo/demo_ble/board/wl82/board.c); the radio's own trim is
 * measured at the first start and kept by syscfg (VM_WIFI_RF_INIT_INFO) */
#define VM_XOSC_INDEX 106               /* include_lib/utils/syscfg/syscfg_id.h */
#define VM_WIFI_PA_DATA 107
#define VM_WIFI_PA_MCS_DGAIN 108
#define VM_WIFI_RF_INIT_INFO 187
static const u8 XOSC[2] = {0xb, 0xb}, PA[7] = {1, 7, 4, 7, 11, 1, 7};
u8 shim_xosc[2];                        /* a test override of the crystal's load caps (0 = none) */
void wifi_get_xosc(u8 *x)
{
    if (shim_xosc[0] || shim_xosc[1])
        memcpy(x, shim_xosc, 2);
    else if (syscfg_read(VM_XOSC_INDEX, x, 2) != 2)
        memcpy(x, XOSC, 2);
}
void wifi_get_mcs_dgain(u8 *d)
{
#if SHIM_WIFI
    static const u8 DGAIN[20] = {0x32, 0x32, 0x32, 0x32, 0x48, 0x48, 0x55, 0x50, 0x40, 0x40,   /* SDK cfg_tool.bin */
                                 0x3e, 0x34, 0x48, 0x5a, 0x50, 0x40, 0x40, 0x40, 0x32, 0x2b};
    if (syscfg_read(VM_WIFI_PA_MCS_DGAIN, d, 20) != 20)
        memcpy(d, DGAIN, 20);
#else
    if (syscfg_read(VM_WIFI_PA_MCS_DGAIN, d, 20) != 20)
        memset(d, 0, 20);
#endif
}
int wifi_get_pa_trim_data(u8 *pa)
{
    if (syscfg_read(VM_WIFI_PA_DATA, pa, 7) != 7)
        memcpy(pa, PA, 7);
    return 1;
}
#if SHIM_WIFI && !defined SHIM_WIFI_KEEP_TRIM
/* WiFi: a fresh radio trim at every start, kept nowhere. A stored one is shared with Bluetooth's (the same record, in
 * the same store object across firmwares): WiFi on a trim a Bluetooth start had kept could not be heard by the
 * access point (no join even to a hotspot beside it), and joined at once on a fresh one (2026-10-16) */
int wifi_get_rf_trim_data(void *info, int size) { (void)info; (void)size; return -1; }
int wifi_set_rf_trim_data(void *info, int size) { (void)info; (void)size; return 0; }
#else
int wifi_get_rf_trim_data(void *info, int size) { return syscfg_read(VM_WIFI_RF_INIT_INFO, info, (u16)size) == size ? 0 : -1; }
int wifi_set_rf_trim_data(void *info, int size) { return syscfg_write(VM_WIFI_RF_INIT_INFO, info, (u16)size) == size ? 0 : -1; }
#endif

int bt_event_notify(int from, void *event)
{
    shim_bt_event(from, event, 0);
    return 0;
}

/* the Wi-Fi coexistence task: only the USB-to-SDIO Wi-Fi path calls it (never, here) */
void wlc_main(void) {}
