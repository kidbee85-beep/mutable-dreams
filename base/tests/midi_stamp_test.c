/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Henri David */
/* MIDI-in stamps (FELUCCA_MIDI_STAMP: felucca-bsp's usb.c usb_midi_peek, midi_uart.c uart_midi_peek): the production
 * code against a model of EP1 OUT's registers and of the TRS jack's DMA ring, run on main_fm1.c's TIMER5 schedule
 * (10 kHz ticks, each with the peeks at the half way time since the last tick; the USB / TRS poll at ticks 0 and 5 of
 * ten, held while the audio render runs; the app's 1 kHz tick draining the queue). Messages land at known random
 * times; each message's stamp (midi_in_t24) is compared with its landing time, beside the stamps an app got before:
 * the read's time (plat_midi_in_t24 = now) and a stamp in the poll that took it, half a poll back (the poll's own
 * +-0.25 ms, and the time the poll waited for the render). The render model is a block every 5.8 ms (256 samples at
 * 44.1 kHz) taking a given share of it; the render's real length depends on the app. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define FELUCCA_MIDI_STAMP 1
#define RING_PUBLISH() ((void)0)
#include "fm1_time.h"
#include "fm1_usb.h"
#include "fm1_uart.h"

/* ------------------------------------------------ the SIE: EP1 OUT --- */
static uint8_t common[16], regs[8][32], index_reg, last_read;
static uint8_t *ep_radr[5];
static uint32_t frame_no;
static void mock_wr(uint32_t r, uint32_t v)
{
    if (r == 14u)
        index_reg = (uint8_t)(v & 7u);
    else if (r < 16u)
        common[r] = (uint8_t)v;
    else if (index_reg == 1u && r == 20u && (v & 0x10u))
        regs[1][20] &= (uint8_t)~1u;                    /* the packet taken: the endpoint free again */
    else
        regs[index_reg][r] = (uint8_t)v;
}
static void mock_rd(uint32_t r) { last_read = (uint8_t)r; }
static uint32_t mock_dt(void)
{
    uint32_t r = last_read, v;
    if (r == 4u || r == 2u || r == 6u) {                /* INTRRX1, INTRTX1, INTRUSB: clear on read */
        v = common[r];
        common[r] = 0;
        return v;
    }
    if (r == 12u)
        return frame_no++ & 0xFFu;                      /* a host is there: the frame number moves */
    return r < 16u ? common[r] : regs[index_reg][r];
}
static void mock_rxbuf(uint32_t ep, void *p) { ep_radr[ep] = p; }
static void mock_txbuf(uint32_t ep, void *p) { (void)ep; (void)p; }
static void mock_send(uint32_t ep, void *p, uint32_t n) { (void)ep; (void)p; (void)n; }
static void mock_ep0_send(void *p, uint32_t n) { (void)p; (void)n; }
static void mock_buf(void *p) { (void)p; }
static void mock_enable(uint32_t e) { (void)e; }
static void mock_nop(void) {}
static uint32_t mock_one(void) { return 1; }
#define fm1_usb_sie_on mock_one
#define fm1_usb_sie_done mock_one
#define fm1_usb_sie_wr_start mock_wr
#define fm1_usb_sie_rd_start mock_rd
#define fm1_usb_sie_data mock_dt
#define fm1_usb_rx_sync mock_nop
#define fm1_usb_ep_rxbuf mock_rxbuf
#define fm1_usb_ep_txbuf mock_txbuf
#define fm1_usb_ep_send mock_send
#define fm1_usb_ep0_send mock_ep0_send
#define fm1_usb_ep0_buf mock_buf
#define fm1_usb_ep_enable mock_enable
#define fm1_usb_sof_take mock_one
#define fm1_uart1_midi_init(ring, len) ((void)(ring), (void)(len))
#include "usb.c"
#include "midi_uart.c"

/* -------------------------------------------------------- the model --- */
#define US FM1_TICKS_PER_US
#define TICK (100u * US)                                /* TIMER5: 10 kHz */
#define BLOCK (5805u * US)                              /* a render block: 256 samples at 44.1 kHz */
enum { N = 4000, KUSB = 0, KTRS = 1 };

static int fails;
static void check(int ok, const char *what)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static uint32_t lcg = 12345u;
static uint32_t rnd(uint32_t n)
{
    lcg = lcg * 1664525u + 1013904223u;
    return (lcg >> 8) % n;
}

struct err { double sum, max; uint32_t n; };
static void err_add(struct err *e, uint32_t stamp, uint32_t at)
{
    double d = (double)(int32_t)(stamp - at) / US;
    d = d < 0 ? -d : d;
    e->sum += d;
    e->n++;
    if (d > e->max)
        e->max = d;
}

/* the jack: the DMA writes each byte into the ring as its stop bit ends; the poll takes the count */
static uint32_t dma_w, dma_cnt;
static void trs_land(uint8_t b)
{
    um_ring[dma_w++ & (UM_RING - 1u)] = b;
    dma_cnt++;
}

/* one run: N messages on one transport, the render taking `load` percent of each block.
 * new / poll / read: the errors of the stamp, of the poll's stamp, of the read's time */
static void run(int kind, uint32_t load, struct err *enew, struct err *epoll, struct err *eread)
{
    static uint32_t landed[N + 64], poll_t[MQ];
    uint32_t t = 1000000u, t_prev = 0, sub = 0, sent = 0, got = 0, next, render = load * (BLOCK / 100u);
    uint32_t byte_k = 0, bytes_of = 0, usb_wait_t = 0, usb_waiting = 0, w_seen;
    uint8_t usb_due_m = 0, msg[3] = {0};
    memset(enew, 0, sizeof *enew);
    memset(epoll, 0, sizeof *epoll);
    memset(eread, 0, sizeof *eread);
    mi_r = mi_w = 0;
    w_seen = 0;
    next = t + 3u * TICK;
    while (got < N) {
        int in_render = (t % BLOCK) < render;
        /* what lands before this tick */
        if (kind == KUSB) {
            while (sent < N && (usb_waiting ? usb_wait_t : next) <= t) {
                uint32_t at = usb_waiting ? usb_wait_t : next;
                if (regs[1][20] & 1u) {                 /* the endpoint still holds one: the host's OUT NAKs */
                    if (!usb_waiting) {
                        usb_waiting = 1;
                        usb_wait_t = next;
                    }
                    break;
                }
                ep_radr[1][0] = 0x09;                   /* a note on, cable 0 */
                ep_radr[1][1] = 0x90;
                ep_radr[1][2] = (uint8_t)(sent & 0x7Fu);
                ep_radr[1][3] = 100;
                regs[1][20] |= 1u;
                regs[1][22] = 4;
                regs[1][23] = 0;
                common[4] |= 0x02u;
                landed[sent++] = at;
                usb_waiting = 0;
                next += 40u * US + rnd(3000u * US);     /* 0.04 .. 3 ms apart */
            }
        } else {
            while (sent < N && next <= t) {             /* a note on (3 bytes, back to back) or a clock (1 byte) */
                if (!bytes_of) {
                    bytes_of = rnd(3u) ? 3u : 1u;
                    msg[0] = bytes_of == 3u ? 0x90 : 0xF8;
                    msg[1] = (uint8_t)(sent & 0x7Fu);
                    msg[2] = 100;
                    byte_k = 0;
                }
                trs_land(msg[byte_k++]);
                if (byte_k == bytes_of) {
                    landed[sent++] = next;              /* a message lands with its last byte */
                    bytes_of = 0;
                    next += 320u * US + rnd(3000u * US);
                } else {
                    next += 320u * US;
                }
            }
        }
        /* the TIMER5 tick (main_fm1.c) */
        {
            uint32_t tm = t_prev ? t - (t - t_prev) / 2u : t;
            usb_midi_peek(tm);
            uart_midi_peek(tm);
        }
        t_prev = t;
        if (sub % 5u == 0u)
            usb_due_m = 1;
        if (usb_due_m && !in_render) {
            usb_due_m = 0;
            usb_poll();
            uart_midi_take(dma_cnt);
            dma_cnt = 0;
            if (!(regs[1][20] & 1u) && usb_waiting && usb_wait_t <= t)
                usb_wait_t = t + 10u * US;              /* a held OUT lands at the host's next try */
            while (w_seen != mi_w)
                poll_t[w_seen++ % MQ] = t - 250u * US;  /* the stamp of the 2 kHz scheme */
        }
        if (sub == 0u) {                                /* the app's tick: plat_midi_in */
            while (mi_r != mi_w) {
                uint32_t at = landed[got], s = mi_r % MQ;
                err_add(enew, midi_in_t24[s], at);
                err_add(epoll, poll_t[s], at);
                err_add(eread, t, at);
                mi_r++;
                got++;
            }
        }
        sub = (sub + 1u) % 10u;
        t += TICK;
    }
}

static void report(const char *name, uint32_t load, const struct err *a, const struct err *b, const struct err *c)
{
    printf("  %s, render %2u%%: stamp error (us) mean %3.0f worst %3.0f | poll's stamp mean %4.0f worst %5.0f | "
           "read time mean %4.0f worst %5.0f\n", name, load, a->sum / a->n, a->max, b->sum / b->n, b->max,
           c->sum / c->n, c->max);
}

int main(void)
{
    static const uint32_t loads[] = {0, 50, 90};
    uint32_t i;
    usb.up = 1;
    usb.config = 1;
    ep1_config();
    uart_midi_init();
    for (i = 0; i < UM_RING; i++)
        um_ring[i] = UM_MARK;                           /* (what the init leaves on the FM-1) */
    for (i = 0; i < 3u; i++) {
        struct err a, b, c;
        run(KUSB, loads[i], &a, &b, &c);
        report("USB", loads[i], &a, &b, &c);
        check(a.n == N && a.max <= 50.5, "USB: every stamp within +-0.05 ms of its packet's landing");
        if (!loads[i])
            check(b.max > 200.0 && b.max <= 250.5, "(the poll's stamp is within +-0.25 ms without a render)");
        else
            check(b.max > 1000.0, "(the poll's stamp is late by the render's hold)");
        run(KTRS, loads[i], &a, &b, &c);
        report("TRS", loads[i], &a, &b, &c);
        check(a.n == N && a.max <= 50.5, "TRS: every stamp within +-0.05 ms of its last byte's landing");
    }
    {   /* a packet held for want of room keeps the time it landed */
        uint32_t k;
        mi_r = mi_w = 0;
        mi_w = MQ - 8u;                                 /* the ring nearly full: ep1_take refuses */
        ep_radr[1][0] = 0x09; ep_radr[1][1] = 0x90; ep_radr[1][2] = 60; ep_radr[1][3] = 100;
        regs[1][20] |= 1u;
        regs[1][22] = 4;
        common[4] |= 0x02u;
        usb_midi_peek(5000u);
        usb_poll();
        check(mi_w == MQ - 8u && (regs[1][20] & 1u), "a packet without room is left in the endpoint");
        for (k = 0; k < 4u; k++)
            usb_midi_peek(6000u + k);
        mi_r = mi_w;
        usb_poll();
        check(mi_w == mi_r + 1u && midi_in_t24[mi_r % MQ] == 5000u, "taken later, it keeps the time it landed");
        mi_r = mi_w;
    }
    {   /* bytes the DMA had written before a peek saw them: the take gives them the last peek's time */
        uint32_t w0 = mi_w;
        uart_midi_peek(7000u);
        trs_land(0xF8);
        uart_midi_take(dma_cnt);
        dma_cnt = 0;
        check(mi_w == w0 + 1u && midi_in_t24[w0 % MQ] == 7000u, "a byte no peek saw takes the last peek's time");
        /* two bytes found by one peek: the first a byte time back */
        trs_land(0x90);
        trs_land(0x40);
        trs_land(0x64);
        uart_midi_peek(20000u);
        uart_midi_take(dma_cnt);
        dma_cnt = 0;
        check(mi_w == w0 + 2u && midi_in_t24[(w0 + 1u) % MQ] == 20000u, "a message found whole takes the peek's time");
        mi_r = mi_w;
    }
    printf(fails ? "midi_stamp_test: %d FAILED\n" : "midi_stamp_test: all passed\n", fails);
    return fails != 0;
}
