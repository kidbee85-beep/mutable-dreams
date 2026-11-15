/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Store writes run from the main loop; XIP is unavailable during the flash operation.
 * Keep the outer interrupt lock until its old DMA notification has been acknowledged.
 * Otherwise the nested flash wrapper re-enables interrupts and renders an arbitrarily
 * old free-half notification: even an idle 1.6 ms render can then cross the next edge.
 * The following real DMA edge gets the full audio budget. No XRun counter is reset or
 * masked, and the rendering path, its quality and the DSP reserve are unchanged.
 * Included by unity.c; exercised over a DMA/interrupt model in storage_flash_test.c. */
static void st_audio_resume(void)                 /* XIP restored, global interrupts still off */
{
    if (fm1_audio_running() && (fm1_audio_pending() & FM1_AUDIO_HALF))
        fm1_audio_ack_half();
}

static int st_erase(uint32_t off)
{
    uint32_t took, f;
    int rc;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    f = irq_save();
    audio_silence();                              /* atomic with the flash lock, no partial stale buffer */
    rc = fl_erase4k(off, &took);
    st_audio_resume();
    irq_restore(f);
    return rc;
}

static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t f;
    int rc;
    if (!FL_STORE_OK(off, n))
        return -8;
    if (!n)
        return 0;
    f = irq_save();
    rc = fl_write(off, src, n);
    st_audio_resume();
    irq_restore(f);
    return rc;
}
