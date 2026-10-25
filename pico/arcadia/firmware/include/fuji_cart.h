/* fuji_cart.h -- the cartridge-side half of the FujiNet mailbox.
 *
 * Split so that core1's bus loop does as little as possible. core1 records
 * hotspot READS into a ring and returns to polling; core0 drains the ring
 * and runs the protocol. Replies need no bus-loop change at all: the
 * mailbox is painted straight into the served window, so serving it is the
 * same instruction that already serves ROM.
 *
 * Images live in two slots, served one at a time: a push always lands in
 * the slot that is not being served, so nothing is ever written under the
 * running client, and a client booted from one slot can stage the next
 * image in the other. The serve state (which slot, which APPBANK page,
 * the cart RAM pointer) is arcapp.h's, kept by core1. See arcadia_cart.c
 * for why the bus loop de-duplicates on a pin-state change rather than an
 * Enable edge (there is no Enable line: A12 low is the chip select and
 * stays low across consecutive instruction fetches).
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "arcapp.h"
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

/* What core1 serves: slot fuji_live, through fuji_app. core0 stages a
 * committed image in the other slot; core1 switches to it on the armed
 * FN_HOT_SWAP read. */
extern uint8_t fuji_slot[2][ARCMAP_STORE];
extern volatile uint8_t fuji_slot_npages[2];
extern volatile uint8_t fuji_live;
extern arcapp_t fuji_app;               /* core1's after boot */
extern uint8_t fuji_ffpage[FN_APP_PAGE];
extern volatile bool fuji_boot_armed;
extern volatile bool fuji_have_staged;
extern volatile bool fuji_staged_claims;

/* The slot a push lands in. */
static inline uint8_t *fuji_cart_stage_slot(void)
{
    return fuji_slot[fuji_live ^ 1u];
}

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
/* Stage a committed image (already validated) for the armed swap, in the
 * slot that is not being served; the push normally lands there already, so
 * the apply is in place. A claiming image also gets the live status page. */
void fuji_cart_stage(const uint8_t *image, const arcmap_plan_t *plan);
void fuji_cart_init(void);
/* core1, on the armed swap read: serve the staged slot from page 0. */
static inline void fuji_cart_swap(void)
{
    uint8_t s = (uint8_t)(fuji_live ^ 1u);

    arcapp_reset(&fuji_app, fuji_slot[s], fuji_slot_npages[s], fuji_ffpage);
    fuji_live = s;
    fuji_boot_armed = false;
    fuji_have_staged = false;
    fuji_mailbox_active = fuji_staged_claims;
}

#endif /* FUJI_CART_H */
