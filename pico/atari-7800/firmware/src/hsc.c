/* hsc.c -- the HSC's RAM as 32 AppKeys; see hsc.h. */

#include <string.h>

#include "hsc.h"

#define HSC_TIMEOUT_MS 1000u

static bool open_key(hsc_t *h, unsigned key, uint8_t mode)
{
    uint8_t payload[6];
    fb_reply_t reply;

    payload[0] = (uint8_t)(HSC_CREATOR & 0xFF);
    payload[1] = (uint8_t)(HSC_CREATOR >> 8);
    payload[2] = HSC_APP;
    payload[3] = (uint8_t)key;
    payload[4] = mode;
    payload[5] = 0;
    if (h->port->transact(FUJI_DEVICE_ID, CMD_APPKEY_OPEN, NULL, 0, payload, sizeof payload,
                          HSC_TIMEOUT_MS, &reply) != FB_OK)
        return false;
    return reply.command == CMD_FUJI_ACK;
}

void hsc_init(hsc_t *h, const hsc_port_t *port, uint8_t *ram, volatile uint32_t *dirty)
{
    memset(h, 0, sizeof *h);
    h->port = port;
    h->ram = ram;
    h->dirty = dirty;
}

bool hsc_restore(hsc_t *h)
{
    unsigned k;

    if (!h->port->link_up())
        return false;
    for (k = 0; k < HSC_CHUNKS; k++) {
        uint8_t *dst = h->ram + k * HSC_CHUNK;
        fb_reply_t reply;

        /* a failed read leaves the chunk as it was: the flash copy stands */
        if (open_key(h, k, 0)
            && h->port->transact(FUJI_DEVICE_ID, CMD_APPKEY_READ, NULL, 0, NULL, 0,
                                 HSC_TIMEOUT_MS, &reply) == FB_OK
            && reply.command == CMD_FUJI_ACK) {
            /* rs232 puts the key's length in front of it */
            if (reply.data_len >= 2 + HSC_CHUNK
                && (reply.data[0] | reply.data[1] << 8) == HSC_CHUNK)
                memcpy(dst, reply.data + 2, HSC_CHUNK);
            else
                memset(dst, 0xFF, HSC_CHUNK);
            h->sd_ok = true;
        } else {
            h->sd_ok = false;
        }
    }
    h->restored = true;
    *h->dirty = 0;
    h->pending = 0;
    h->seen = 0;
    return true;
}

bool hsc_save_chunk(hsc_t *h, unsigned chunk)
{
    fb_reply_t reply;

    if (!open_key(h, chunk, 1)
        || h->port->transact(FUJI_DEVICE_ID, CMD_APPKEY_WRITE, NULL, 0,
                             h->ram + chunk * HSC_CHUNK, HSC_CHUNK,
                             HSC_TIMEOUT_MS, &reply) != FB_OK
        || reply.command != CMD_FUJI_ACK) {
        h->sd_ok = false;
        return false;
    }
    h->sd_ok = true;
    return true;
}

void hsc_service(hsc_t *h, bool idle)
{
    uint32_t now = h->port->now_ms();
    uint32_t d = *h->dirty;
    unsigned k;

    if (!h->restored) {
        if (idle && h->port->link_up())
            hsc_restore(h);
        return;
    }
    if (d != h->seen) {
        h->seen = d;
        h->last_change_ms = now;
    }
    if (!(d || h->pending) || !idle || !h->port->link_up()
        || now - h->last_change_ms < HSC_DEBOUNCE_MS)
        return;
    /* take the bits first: a write that lands while we save marks it again */
    h->pending |= __atomic_exchange_n(h->dirty, 0u, __ATOMIC_SEQ_CST);
    h->seen = 0;
    for (k = 0; k < HSC_CHUNKS; k++) {
        if (!(h->pending & (1u << k)))
            continue;
        if (!hsc_save_chunk(h, k)) {
            h->last_change_ms = now;      /* keep the rest; retry after a pause */
            return;
        }
        h->pending &= ~(1u << k);
    }
}
