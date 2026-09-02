/* fuji_cart.h -- the cartridge-side half of the FujiNet mailbox.
 *
 * Split so that core1's bus loop does as little as possible. core1 records
 * mailbox writes into a ring and returns to polling; core0 drains the ring and
 * runs the transaction. Reads need no bus-loop change at all: the mailbox is
 * painted straight into rom_table[], so serving it is the same instruction that
 * already serves ROM.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

/* One transaction is ~6 register writes plus at most a 256-byte payload, and
 * the console cannot start another until it sees ACKSEQ, so the ring only ever
 * has to hold one. */
#define FUJI_RING_LEN 512

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];   /* (addr << 8) | data */
    volatile uint16_t head;                 /* written by core1 only */
    volatile uint16_t tail;                 /* written by core0 only */
    volatile bool     overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern volatile bool fuji_mailbox_active;

/* core1: record one external-data-window write. Inlined into the bus loop, so
 * it must stay a compare and two stores. */
static inline void fuji_cart_note_write(uint8_t addr, uint8_t data)
{
    uint16_t head = fuji_ring.head;
    uint16_t next = (uint16_t)((head + 1u) % FUJI_RING_LEN);

    if (next == fuji_ring.tail) {
        fuji_ring.overflow = true;      /* core0 fell behind; drop, don't wrap */
        return;
    }
    fuji_ring.buf[head] = (uint16_t)(((uint16_t)addr << 8) | data);
    fuji_ring.head = next;
}

/* core0 */
bool fuji_cart_next_write(uint8_t *addr, uint8_t *data);
void fuji_cart_poke(unsigned prog_addr, uint8_t value);
void fuji_cart_paint(void);
void fuji_cart_init(void);

#endif /* FUJI_CART_H */
