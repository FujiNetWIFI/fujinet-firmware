/* fuji_cart.h -- the cartridge-side half of the FujiNet mailbox.
 *
 * Split so that core1's bus loop does as little as possible. core1 records
 * hotspot READS into a ring and returns to polling; core0 drains the ring
 * and runs the protocol. Replies need no bus-loop change at all: the
 * mailbox is painted straight into the served window, so serving it is the
 * same instruction that already serves ROM.
 *
 * The Arcadia has no banking (nothing >8K is addressable on the Emerson
 * connector), so the serve state is a single base pointer, and the boot
 * swap is one pointer store. See arcadia_cart.c for why the bus loop
 * de-duplicates on a pin-state change rather than an Enable edge (there is
 * no Enable line: A12 low is the chip select and stays low across
 * consecutive instruction fetches).
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "arcmap.h"

/* One transaction is a dozen paired reads plus at most a 320-byte stream,
 * and the console cannot start another until it sees ACKSEQ, so the ring
 * only ever has to hold one transaction's worth. */
#define FUJI_RING_LEN 512

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];   /* image offset, 0x1D00-0x1FFF */
    volatile uint16_t head;                 /* written by core1 only */
    volatile uint16_t tail;                 /* written by core0 only */
    volatile bool     overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern volatile bool fuji_mailbox_active;

/* What core1 serves: a single 8K image, indexed by the decoded offset.
 * core0 stages a committed image into fuji_staged; core1 repoints
 * fuji_serve_base at it on the armed FN_HOT_SWAP read (one pointer store). */
extern const uint8_t *volatile fuji_serve_base;
extern uint8_t fuji_window[0x2000];
extern uint8_t fuji_staged[0x2000];
extern volatile bool fuji_boot_armed;
extern volatile bool fuji_have_staged;
extern volatile bool fuji_staged_claims;

/* core1: record one hotspot read. Inlined into the bus loop, so it must
 * stay a compare and two stores. */
static inline void fuji_cart_note_read(uint16_t offset)
{
    uint16_t head = fuji_ring.head;
    uint16_t next = (uint16_t)((head + 1u) % FUJI_RING_LEN);

    if (next == fuji_ring.tail) {
        fuji_ring.overflow = true;      /* core0 fell behind; drop, don't wrap */
        return;
    }
    fuji_ring.buf[head] = offset;
    fuji_ring.head = next;
}

/* core0 */
bool fuji_cart_next_read(uint16_t *offset);
void fuji_cart_poke(unsigned offset, uint8_t value);
/* Stage a committed image (already validated) for the armed swap; the
 * bytes are copied into fuji_staged, 0xFF-filled above the image, so the
 * source may be transient. */
void fuji_cart_stage(const uint8_t *image, const arcmap_plan_t *plan);
void fuji_cart_init(void);

#endif /* FUJI_CART_H */
