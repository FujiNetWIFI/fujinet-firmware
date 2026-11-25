/* fuji_pipe.h -- the I/O ports an I/O card's host speaks FujiBus through.
 *
 *   +0 GETC     read: the next reply byte; empty, the last one again
 *      PUTC     write: a byte toward the ESP32
 *   +1 STATUS   read: AVAIL, the signature, TXFULL
 *      CONTROL  write: FLUSH and the card's own commands
 *   +2 TICK     read: a free-running 1 kHz counter, for the host's timeouts
 *
 * A card decodes four ports; +3 and a write to +2 do nothing.
 *
 * The bus side calls these; the link side (fuji_iolink.c) drains from_host and
 * fills to_host. Entries in from_host above a byte are marks for the link.
 */
#ifndef FUJI_PIPE_H
#define FUJI_PIPE_H

#include <stdint.h>

#include "fuji_ring.h"

#define FUJI_PIPE_GETC    0 /* read */
#define FUJI_PIPE_PUTC    0 /* write */
#define FUJI_PIPE_STATUS  1 /* read */
#define FUJI_PIPE_CONTROL 1 /* write */
#define FUJI_PIPE_TICK    2 /* read */

#define FUJI_PIPE_ST_AVAIL  0x80
#define FUJI_PIPE_ST_SIG    0x2C /* bits 5-1 = 10110b; bit 6 is always 0 */
#define FUJI_PIPE_ST_SIGMSK 0x7E
#define FUJI_PIPE_ST_TXFULL 0x01

#define FUJI_PIPE_CTL_FLUSH 0x08 /* empties both directions, as on fujiversal */

#define FUJI_PIPE_RING       4096
#define FUJI_PIPE_MARK_FLUSH 0x100 /* from_host entries above a byte */

typedef struct {
    ring16_t from_host;
    ring8_t to_host;
    uint8_t last_getc;
    uint32_t epoch_us; /* TICK origin */
    uint16_t from_host_buf[FUJI_PIPE_RING];
    uint8_t to_host_buf[FUJI_PIPE_RING];
} fuji_pipe_t;

static inline void fuji_pipe_init(fuji_pipe_t *pipe, uint32_t now)
{
    ring16_init(&pipe->from_host, pipe->from_host_buf, FUJI_PIPE_RING);
    ring8_init(&pipe->to_host, pipe->to_host_buf, FUJI_PIPE_RING);
    pipe->last_getc = 0;
    pipe->epoch_us = now;
}

/* Drops what the host hasn't read and tells the link to drop what it holds. */
static inline void fuji_pipe_flush(fuji_pipe_t *pipe)
{
    ring8_drain(&pipe->to_host);
    ring16_put(&pipe->from_host, FUJI_PIPE_MARK_FLUSH);
}

/* Drops what the host hasn't read and hands the link a card mark. */
static inline void fuji_pipe_mark(fuji_pipe_t *pipe, uint16_t mark)
{
    ring8_drain(&pipe->to_host);
    ring16_put(&pipe->from_host, mark);
}

static inline uint8_t fuji_pipe_status(fuji_pipe_t *pipe)
{
    uint8_t status = FUJI_PIPE_ST_SIG;
    if (ring8_count(&pipe->to_host))
        status |= FUJI_PIPE_ST_AVAIL;
    if (ring16_space(&pipe->from_host) < 2)
        status |= FUJI_PIPE_ST_TXFULL;
    return status;
}

/* What GETC will return, without taking it: for a bus that drives the byte
 * before the read's side effects. */
static inline uint8_t fuji_pipe_peekc(fuji_pipe_t *pipe)
{
    uint32_t tail = __atomic_load_n(&pipe->to_host.tail, __ATOMIC_RELAXED);
    if (tail == __atomic_load_n(&pipe->to_host.head, __ATOMIC_ACQUIRE))
        return pipe->last_getc;
    return pipe->to_host.buf[tail & pipe->to_host.mask];
}

/* Empty: the last byte again, so a stray read changes nothing. */
static inline uint8_t fuji_pipe_getc(fuji_pipe_t *pipe)
{
    ring8_get(&pipe->to_host, &pipe->last_getc);
    return pipe->last_getc;
}

static inline uint8_t fuji_pipe_tick(const fuji_pipe_t *pipe, uint32_t now)
{
    return (uint8_t)((now - pipe->epoch_us) / 1000);
}

static inline void fuji_pipe_putc(fuji_pipe_t *pipe, uint8_t val)
{
    ring16_put(&pipe->from_host, val);
}

#endif /* FUJI_PIPE_H */
