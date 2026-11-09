/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal */
/* Disk mode (fm1.toml msc = true: FM1B_MSC; plat.h plat_disk_*): the FM-1 as a USB drive holding the app's disk. The
 * USB side is felucca-bsp usb.c's (FELUCCA_MSC: the descriptors, EP3's packets); here, in the main loop, the bulk-only
 * transport (a command block, its data, a status) and the SCSI commands a computer sends a small removable drive.
 * The blocks themselves are the app's (plat_disk_t: read, write, flush), so a write may take the time a flash erase
 * takes. Part of the unity build (unity.c), after usb.c. */

enum { MS_CBW, MS_IN, MS_OUT, MS_CSW };
static struct {
    const plat_disk_t *d;
    uint8_t state, status, ejected, waiting_csw;
    uint8_t cbw[31];
    uint32_t cbw_n;                              /* bytes of the command block so far */
    uint32_t tag, dlen, done;                    /* the command's tag, its data length, the bytes moved */
    uint32_t lba, left;                          /* READ / WRITE: the next block, the blocks to go */
    uint8_t sense_key, asc;                      /* REQUEST SENSE's answer */
    uint8_t buf[512];                            /* a block being written; small answers */
    uint32_t buf_n;
    uint8_t csw[13];
    uint32_t last_ms, resets;
} ms;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void ms_sense(uint8_t key, uint8_t asc)
{
    ms.sense_key = key;
    ms.asc = asc;
}

/* the status: what was moved against what was asked */
static void ms_finish(uint8_t status)
{
    ms.status = status;
    ms.state = MS_CSW;
    ms.waiting_csw = 0;
}

/* a short answer from ms.buf (n bytes, at most what the host asked for) */
static void ms_answer(uint32_t n)
{
    if (n > ms.dlen)
        n = ms.dlen;
    ms.buf_n = n;
    ms.done = 0;
    ms.left = 0;
    ms.state = n ? MS_IN : MS_CSW;
    ms.status = 0;
    if (n)
        usb_msc_send(ms.buf, n);
}

static void ms_command(void)
{
    const uint8_t *cb = ms.cbw + 15;
    uint32_t i;
    ms.tag = (uint32_t)ms.cbw[4] | (uint32_t)ms.cbw[5] << 8 | (uint32_t)ms.cbw[6] << 16 | (uint32_t)ms.cbw[7] << 24;
    ms.dlen = (uint32_t)ms.cbw[8] | (uint32_t)ms.cbw[9] << 8 | (uint32_t)ms.cbw[10] << 16 | (uint32_t)ms.cbw[11] << 24;
    ms.done = 0;
    ms.buf_n = 0;
    memset(ms.buf, 0, 36);
    switch (cb[0]) {
    case 0x00:                                   /* TEST UNIT READY */
        if (ms.ejected) {
            ms_sense(0x02, 0x3A);                /* not ready: no medium */
            ms_finish(1);
        } else
            ms_finish(0);
        return;
    case 0x03:                                   /* REQUEST SENSE */
        memset(ms.buf, 0, 18);
        ms.buf[0] = 0x70;
        ms.buf[2] = ms.sense_key;
        ms.buf[7] = 10;
        ms.buf[12] = ms.asc;
        ms_sense(0, 0);
        ms_answer(18);
        return;
    case 0x12: {                                 /* INQUIRY */
        const char *v = ms.d->vendor ? ms.d->vendor : "FM-1", *p = ms.d->product ? ms.d->product : "Disk";
        ms.buf[0] = 0x00;                        /* a direct-access block device */
        ms.buf[1] = 0x80;                        /* removable */
        ms.buf[2] = 0x04;
        ms.buf[3] = 0x02;
        ms.buf[4] = 31;
        for (i = 0; i < 8u; i++)
            ms.buf[8 + i] = (uint8_t)(*v ? *v++ : ' ');
        for (i = 0; i < 16u; i++)
            ms.buf[16 + i] = (uint8_t)(*p ? *p++ : ' ');
        ms.buf[32] = '1';
        ms.buf[33] = '.';
        ms.buf[34] = '0';
        ms.buf[35] = ' ';
        ms_answer(36);
        return;
    }
    case 0x1A:                                   /* MODE SENSE (6): no pages, not write-protected */
        memset(ms.buf, 0, 4);
        ms.buf[0] = 3;
        ms_answer(4);
        return;
    case 0x5A:                                   /* MODE SENSE (10) */
        memset(ms.buf, 0, 8);
        ms.buf[1] = 6;
        ms_answer(8);
        return;
    case 0x1E:                                   /* PREVENT / ALLOW MEDIUM REMOVAL */
    case 0x2F:                                   /* VERIFY (10) */
        ms_finish(0);
        return;
    case 0x35:                                   /* SYNCHRONIZE CACHE (10) */
        if (ms.d->flush)
            ms.d->flush();
        ms_finish(0);
        return;
    case 0x1B:                                   /* START STOP UNIT: eject (LoEj, not Start) */
        if ((cb[4] & 3u) == 2u) {
            if (ms.d->flush)
                ms.d->flush();
            ms.ejected = 1;
        }
        ms_finish(0);
        return;
    case 0x23:                                   /* READ FORMAT CAPACITIES */
        memset(ms.buf, 0, 12);
        ms.buf[3] = 8;
        put_be32(ms.buf + 4, ms.d->blocks);
        ms.buf[8] = 0x02;                        /* formatted */
        ms.buf[10] = 0x02;                       /* 512-byte blocks */
        ms_answer(12);
        return;
    case 0x25:                                   /* READ CAPACITY (10) */
        put_be32(ms.buf, ms.d->blocks - 1u);
        put_be32(ms.buf + 4, 512u);
        ms_answer(8);
        return;
    case 0x28:                                   /* READ (10) */
    case 0x2A:                                   /* WRITE (10) */
        ms.lba = be32(cb + 2);
        ms.left = (uint32_t)cb[7] << 8 | cb[8];
        if (ms.ejected || ms.lba + ms.left > ms.d->blocks || ms.left * 512u > ms.dlen) {
            ms_sense(ms.ejected ? 0x02 : 0x05, ms.ejected ? 0x3A : 0x21);
            ms.left = 0;
            ms.state = (ms.cbw[12] & 0x80u) ? MS_IN : MS_OUT;   /* the data stage still runs: padding */
            if (ms.state == MS_IN && ms.dlen) {
                memset(ms.buf, 0, 64);
                ms.buf_n = 0;
            }
            ms.status = 1;
            return;
        }
        ms.state = cb[0] == 0x28 ? MS_IN : MS_OUT;
        ms.status = 0;
        ms.buf_n = 0;
        if (!ms.left)
            ms_finish(0);
        return;
    default:                                     /* not ours: an error, and the data stage padded through */
        ms_sense(0x05, 0x20);                    /* illegal request: invalid command */
        ms.left = 0;
        ms.status = 1;
        if (!ms.dlen)
            ms_finish(1);
        else {
            ms.state = (ms.cbw[12] & 0x80u) ? MS_IN : MS_OUT;
            memset(ms.buf, 0, 64);
        }
        return;
    }
}

/* data to the host: the blocks being read, a short answer, or padding for a refused command */
static void ms_data_in(void)
{
    if (usb_msc_busy())
        return;
    if (ms.left) {                               /* READ: the next block (the last one is sent now) */
        const uint8_t *b = ms.d->read(ms.lba);
        if (!b) {
            memset(ms.buf, 0, 512);
            b = ms.buf;
        }
        usb_msc_send(b, 512u);
        ms.lba++;
        ms.left--;
        ms.done += 512u;
        return;
    }
    if (ms.buf_n) {                              /* a short answer, sent: counted */
        ms.done += ms.buf_n;
        ms.buf_n = 0;
        ms_finish(ms.status);
        return;
    }
    if (ms.status && ms.done < ms.dlen) {        /* a refused read: padding, 64 bytes at a time */
        uint32_t n = ms.dlen - ms.done > 64u ? 64u : ms.dlen - ms.done;
        usb_msc_send(ms.buf, n);
        ms.done += n;
        if (ms.done < ms.dlen)
            return;
    }
    ms_finish(ms.status);
}

/* data from the host: a block at a time into the app's write, or thrown away */
static void ms_data_out(void)
{
    uint32_t want = ms.dlen - ms.done, n;
    if (!want) {
        ms_finish(ms.status);
        return;
    }
    if (ms.left) {
        n = usb_msc_read(ms.buf + ms.buf_n, 512u - ms.buf_n);
        ms.buf_n += n;
        ms.done += n;
        if (ms.buf_n == 512u) {
            if (ms.d->write(ms.lba, ms.buf) != 0) {
                ms_sense(0x03, 0x0C);            /* medium error: write error */
                ms.status = 1;
            }
            ms.lba++;
            ms.left--;
            ms.buf_n = 0;
        }
    } else {                                     /* refused: read and dropped */
        uint8_t junk[64];
        n = usb_msc_read(junk, want > 64u ? 64u : want);
        ms.done += n;
    }
    if (ms.done >= ms.dlen)
        ms_finish(ms.status);
}

void plat_disk_mode(const plat_disk_t *d)
{
    if (d) {
        memset(&ms, 0, sizeof ms);
        ms.d = d;
        ms.resets = usb_msc_resets();
        usb_msc_switch(1);
    } else {
        if (ms.d && ms.d->flush)
            ms.d->flush();
        ms.d = 0;
        usb_msc_switch(0);
    }
}

int plat_disk_poll(void)
{
    int k;
    if (!ms.d)
        return PLAT_DISK_OFF;
    if (usb_msc_resets() != ms.resets) {         /* the host reset the transport: a new command next */
        ms.resets = usb_msc_resets();
        ms.state = MS_CBW;
        ms.cbw_n = 0;
    }
    if (!usb_msc_ready()) {
        ms.state = MS_CBW;
        ms.cbw_n = 0;
        return ms.ejected ? PLAT_DISK_EJECTED : PLAT_DISK_WAITING;
    }
    for (k = 0; k < 16; k++) {                   /* a few steps a pass: the main loop goes on */
        if (ms.state == MS_CBW) {
            ms.cbw_n += usb_msc_read(ms.cbw + ms.cbw_n, 31u - ms.cbw_n);
            if (ms.cbw_n < 31u)
                break;
            ms.cbw_n = 0;
            if (ms.cbw[0] != 0x55 || ms.cbw[1] != 0x53 || ms.cbw[2] != 0x42 || ms.cbw[3] != 0x43)
                continue;                        /* not a command block: wait for the host's reset */
            ms.last_ms = plat_ms();
            ms_command();
        } else if (ms.state == MS_IN) {
            ms_data_in();
            if (ms.state == MS_IN)
                break;                           /* a packet on its way */
        } else if (ms.state == MS_OUT) {
            ms_data_out();
            if (ms.state == MS_OUT && !usb_msc_avail())
                break;
        } else {                                 /* MS_CSW */
            uint32_t residue = ms.dlen > ms.done ? ms.dlen - ms.done : 0;
            if (usb_msc_busy())
                break;
            ms.csw[0] = 0x55;
            ms.csw[1] = 0x53;
            ms.csw[2] = 0x42;
            ms.csw[3] = 0x53;
            put_le32(ms.csw + 4, ms.tag);
            put_le32(ms.csw + 8, residue);
            ms.csw[12] = ms.status;
            usb_msc_send(ms.csw, 13u);
            ms.state = MS_CBW;
        }
    }
    if (ms.ejected)
        return PLAT_DISK_EJECTED;
    return PLAT_DISK_MOUNTED;
}

uint32_t plat_disk_idle_ms(void) { return ms.d ? plat_ms() - ms.last_ms : 0; }
