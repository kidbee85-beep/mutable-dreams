/* SPDX-License-Identifier: MPL-2.0 */
/* Actual storage_flash.c over a model of nested IRQ locks and a DMA which continues
 * through a flash wait. A pending half at an arbitrary old phase must not render on
 * unlock; a fresh edge and genuine rendering overruns must still be serviced/reported. */
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

static uint32_t enabled, pending, phase, late, renders, acks, silenced, flash_calls, ack_while_enabled;
static uint16_t alnk;
static int flash_result;
#define FM1_AUDIO_HALF 0x80u
#define FL_STORE_OK(off,n) ((off)>=0x98000u && (off)<0xc0000u && (n)<=0xc0000u-(off))
static void dispatch(void)
{
    if (enabled && (alnk & 0x800u) && pending) {
        renders++;
        if (phase) late++;
        pending = 0;
    }
}
static uint32_t irq_save(void) { uint32_t f=enabled; enabled=0; return f; }
static void irq_restore(uint32_t f) { enabled=f; dispatch(); }
static uint8_t fm1_audio_pending(void) { return (uint8_t)pending; }
static int fm1_audio_running(void) { return (alnk & 0x800u) != 0; }
static void fm1_audio_ack_half(void) { acks++; ack_while_enabled+=enabled!=0; pending=0; }
static void audio_silence(void) { assert(!enabled); silenced++; }
static int flash_wait(void)
{
    uint32_t f=irq_save(); assert(!f); flash_calls++;
    pending=FM1_AUDIO_HALF; phase=5700;            /* DMA kept moving while the flash had XIP */
    irq_restore(f);                              /* inner wrapper must not release the outer lock */
    return flash_result;
}
static int fl_erase4k(uint32_t off,uint32_t *took) { (void)off;*took=27000;return flash_wait(); }
static int fl_write(uint32_t off,const uint8_t *src,uint32_t n) { (void)off;(void)src;(void)n;return flash_wait(); }
#include "../device/storage_flash.c"

static void setup(uint32_t on,int running,int rc)
{
    enabled=on;alnk=running?0x800u:0;pending=phase=acks=silenced=flash_calls=ack_while_enabled=renders=0;
    late=17;flash_result=rc;
}
int main(void)
{
    uint8_t payload[256]={0};
    setup(0x200u,1,0);
    assert(st_erase(0x9a000u)==0 && silenced==1 && flash_calls==1);
    assert(enabled==0x200u && acks==1 && !pending && !ack_while_enabled && late==17 && !renders);
    phase=0;pending=FM1_AUDIO_HALF;dispatch();
    assert(renders==1 && late==17);              /* new boundary still renders normally */
    phase=5700;pending=FM1_AUDIO_HALF;dispatch();
    assert(renders==2 && late==18);              /* a genuine late render is not hidden */
    puts("PASS erase: atomic silence, old notification drained before IRQ restore, fresh/genuine late edges retained");
    setup(0x200u,1,0);
    assert(st_prog(0x9a100u,payload,sizeof payload)==0 && acks==1 && !pending && !silenced);
    assert(enabled==0x200u && late==17 && !renders && !ack_while_enabled);
    puts("PASS program: nested flash restore cannot prematurely run an old audio half");
    setup(0,1,0);
    assert(st_prog(0x9a100u,payload,sizeof payload)==0 && enabled==0 && acks==1 && late==17);
    setup(0x200u,0,0);
    assert(st_erase(0x9a000u)==0 && acks==0 && enabled==0x200u);
    puts("PASS preserves an initially disabled IRQ state and does not acknowledge audio before it is running");
    for (int rc=-1;rc>=-4;rc--) {
        setup(0x200u,1,rc);
        assert(st_prog(0x9a100u,payload,sizeof payload)==rc && !pending && enabled==0x200u && late==17);
        setup(0x200u,1,rc);
        assert(st_erase(0x9a000u)==rc && !pending && enabled==0x200u && late==17);
    }
    puts("PASS preserves flash errors and resumes safely on their cleanup paths");
    setup(0x200u,1,0);pending=FM1_AUDIO_HALF;
    assert(st_erase(0x1000u)==-8 && st_prog(0xbffffu,payload,256)==-8);
    assert(st_prog(0x9a100u,payload,0)==0);
    assert(!flash_calls && !acks && !silenced && pending==FM1_AUDIO_HALF && enabled==0x200u);
    puts("PASS range refusal and no-op do not touch flash or audio state");
    return 0;
}
