/* fake_esp.h -- a stand-in for the ESP32's rs232 bus on the far end of the link.
 *
 * Frames the card sends are answered from in-memory Altair images (one per slot)
 * the way lib/device/rs232 does: 'N' PERCOM, 'R'/'W' 137-byte sectors, FUJI
 * mount commands, printer writes, and MODEM_WRITE with no reply. Replies queue
 * in `out` until the test pumps them into s100_link_rx(), so a test can delay,
 * split or interleave them with raw modem bytes.
 */
#ifndef FAKE_ESP_H
#define FAKE_ESP_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fujibus.h"
#include "s100_regs.h"

#define FE_SLOTS 8

typedef struct {
    uint8_t *img;
    uint32_t tracks, spt;
    int writable;
} fe_disk_t;

typedef struct {
    fe_disk_t disk[FE_SLOTS];
    uint8_t out[1 << 16];
    uint32_t outlen;
    uint8_t in[4096];
    uint32_t inlen;
    int in_frame;
    /* counters and logs */
    int reads, writes, probes, fuji, mounts;
    uint8_t printed[4096];
    uint32_t nprinted;
    uint8_t modem_tx[4096];
    uint32_t nmodem;
    int mute;      /* swallow requests without replying */
    int mute_prn;  /* a disabled printer never answers */
    uint8_t last_dev, last_cmd;
} fake_esp_t;

static fake_esp_t fe;

static inline void fe_disk(int slot, uint32_t tracks, uint32_t spt, int writable)
{
    fe_disk_t *d = &fe.disk[slot];
    free(d->img);
    d->tracks = tracks;
    d->spt = spt;
    d->writable = writable;
    d->img = malloc(tracks * spt * 137);
    for (uint32_t i = 0; i < tracks * spt * 137; i++)
        d->img[i] = (uint8_t)(i / 137 * 7 + i % 137);
}

static inline void fe_unmount(int slot)
{
    free(fe.disk[slot].img);
    fe.disk[slot].img = NULL;
}

static inline void fe_reply(uint8_t dev, int ack, const uint8_t *data, uint16_t len)
{
    uint8_t frame[2 * 1100];
    size_t n = fujibus_build_request(dev, ack ? 0x06 : 0x15, NULL, 0, data, len, frame,
                                     sizeof(frame));
    memcpy(fe.out + fe.outlen, frame, n);
    fe.outlen += (uint32_t)n;
}

/* Raw bytes the ESP32 emits outside any frame: modem output. */
static inline void fe_modem_out(const uint8_t *b, uint32_t n)
{
    memcpy(fe.out + fe.outlen, b, n);
    fe.outlen += n;
}

static inline uint32_t param0(const uint8_t *pkt)
{
    // our requests carry one descriptor and one param
    static const uint8_t sz[8] = {0, 1, 1, 1, 1, 2, 2, 4};
    uint8_t d = pkt[5] & 7;
    uint32_t v = 0;
    for (int i = 0; i < sz[d]; i++)
        v |= (uint32_t)pkt[6 + i] << (8 * i);
    return v;
}

static inline void fe_frame(uint8_t *raw, uint32_t n)
{
    // decode: SLIP unescape
    uint8_t pkt[2048];
    uint32_t w = 0;
    for (uint32_t i = 1; i + 1 < n; i++)
    {
        if (raw[i] == 0xDB)
        {
            i++;
            pkt[w++] = raw[i] == 0xDC ? 0xC0 : 0xDB;
        }
        else
            pkt[w++] = raw[i];
    }
    uint8_t copy[4096];
    memcpy(copy, raw, n);
    fb_reply_t r;
    if (!fujibus_parse_reply(copy, n, &r))
        return;
    fe.last_dev = r.device;
    fe.last_cmd = r.command;
    if (fe.mute)
        return;

    if (r.device >= 0x31 && r.device < 0x31 + FE_SLOTS)
    {
        fe_disk_t *d = &fe.disk[r.device - 0x31];
        if (r.command == 'N')
        {
            fe.probes++;
            if (!d->img)
                return fe_reply(r.device, 0, NULL, 0);
            uint8_t p[12] = {0};
            p[0] = d->tracks > 255 ? 0 : (uint8_t)d->tracks;
            p[1] = 1;
            p[2] = (uint8_t)(d->spt >> 8);
            p[3] = (uint8_t)d->spt;
            p[6] = 0;
            p[7] = 137;
            p[8] = 0xFF;
            return fe_reply(r.device, 1, p, 12);
        }
        uint32_t lba = param0(pkt);
        if (!d->img || lba >= d->tracks * d->spt)
            return fe_reply(r.device, 0, NULL, 0);
        if (r.command == 'R')
        {
            fe.reads++;
            return fe_reply(r.device, 1, d->img + lba * 137, 137);
        }
        if (r.command == 'W')
        {
            fe.writes++;
            if (!d->writable || r.data_len != 137)
                return fe_reply(r.device, 0, NULL, 0);
            memcpy(d->img + lba * 137, r.data, 137);
            return fe_reply(r.device, 1, NULL, 0);
        }
        return fe_reply(r.device, 0, NULL, 0);
    }
    if (r.device == 0x70)
    {
        fe.fuji++;
        if (r.command == 0xF8)
            fe.mounts++;
        return fe_reply(0x70, 1, (const uint8_t *)"OK", 2);
    }
    if (r.device == 0x40)
    {
        memcpy(fe.printed + fe.nprinted, r.data, r.data_len);
        fe.nprinted += r.data_len;
        if (!fe.mute_prn)
            fe_reply(0x40, 1, NULL, 0);
        return;
    }
    if (r.device == 0x50)
    {
        memcpy(fe.modem_tx + fe.nmodem, r.data, r.data_len);
        fe.nmodem += r.data_len;
        return; // the modem never answers with a frame
    }
    fe_reply(r.device, 0, NULL, 0);
}

static inline size_t fe_tx(void *ctx, const uint8_t *buf, size_t n)
{
    (void)ctx;
    for (size_t i = 0; i < n; i++)
    {
        uint8_t b = buf[i];
        if (!fe.in_frame)
        {
            if (b == 0xC0)
            {
                fe.in_frame = 1;
                fe.in[0] = b;
                fe.inlen = 1;
            }
            continue;
        }
        fe.in[fe.inlen++] = b;
        if (b == 0xC0)
        {
            if (fe.inlen == 2)
            {
                fe.inlen = 1;
                continue;
            }
            fe_frame(fe.in, fe.inlen);
            fe.in_frame = 0;
        }
    }
    return n;
}

/* Delivers everything the fake has queued. */
static inline void fe_pump(s100_card_t *c, uint32_t now)
{
    if (!fe.outlen)
        return;
    uint8_t buf[sizeof(fe.out)];
    uint32_t n = fe.outlen;
    memcpy(buf, fe.out, n);
    fe.outlen = 0;
    s100_link_rx(c, buf, n, now);
}

static inline void fe_reset(void)
{
    for (int i = 0; i < FE_SLOTS; i++)
        fe_unmount(i);
    memset(&fe, 0, sizeof(fe));
}

#endif
