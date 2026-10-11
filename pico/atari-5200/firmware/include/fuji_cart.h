/* fuji_cart.h -- what core1 serves, what core0 drives, and the ring between.
 *
 * core1 (a52_cart.c) owns the bus: it serves the live view, makes the mapper
 * bank changes and the swap inline, and records mailbox reads. Everything
 * slower -- fujimail, staging, power -- is core0's, fed by the ring.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "a52_cart.h"
#include "fuji_load.h"

/* One transaction is a dozen register pairs plus a 320-byte stream, and the
 * console cannot start another before it sees ACKSEQ. */
#define FUJI_RING_LEN 1024

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];  /* window offsets, or A52_RING_SWAP */
    volatile uint16_t head;                /* written by core1 only */
    volatile uint16_t tail;                /* written by core0 only */
    volatile bool overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern uint8_t fuji_arena[FN_ARENA_SIZE];
extern fuji_load_t fuji_loader;

/* core1: record one mailbox read; inlined into the bus loop. */
A52_HOT void fuji_ring_note(uint16_t entry)
{
    uint16_t head = fuji_ring.head;
    uint16_t next = (uint16_t)((head + 1u) % FUJI_RING_LEN);

    if (next == fuji_ring.tail) {
        fuji_ring.overflow = true;         /* core0 fell behind; drop, do not wrap */
        return;
    }
    fuji_ring.buf[head] = entry;
    fuji_ring.head = next;
}

/* core0 */
void fuji_cart_init(const uint8_t *config, uint32_t config_len);
bool fuji_cart_next(uint16_t *entry);
void fuji_cart_poke(unsigned offset, uint8_t value);
void fuji_cart_service(void);              /* console power, the LED */

#endif /* FUJI_CART_H */
