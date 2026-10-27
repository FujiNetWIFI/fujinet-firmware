/* fuji_cart.h -- the cartridge-side half of the FujiNet mailbox.
 * core1 records hotspot reads into a ring; core0 drains it, runs the protocol and paints
 * replies into the served window. Two image slots: a push lands in the one not served.
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

extern uint8_t fuji_slot[2][ARCMAP_STORE];
extern volatile uint8_t fuji_slot_npages[2];
extern volatile uint8_t fuji_live;
extern arcapp_t fuji_app;               /* core1's after boot */
extern uint8_t fuji_ffpage[FN_APP_PAGE];
extern volatile bool fuji_boot_armed;
extern volatile bool fuji_have_staged;
extern volatile bool fuji_staged_claims;

static inline uint8_t *fuji_cart_stage_slot(void)
{
    return fuji_slot[fuji_live ^ 1u];
}

/* core1: inlined into the bus loop, so keep it a compare and two stores. */
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
/* A claiming image also gets the live status page. */
void fuji_cart_stage(const uint8_t *image, const arcmap_plan_t *plan);
void fuji_cart_init(void);
/* core1, on the armed swap read: serve the staged slot from page 0. */
static inline void fuji_cart_swap(void)
{
    uint8_t slot = (uint8_t)(fuji_live ^ 1u);

    /* Before fuji_live: the tick timer reads them in the other order. */
    fuji_mailbox_active = fuji_staged_claims;
    arcapp_reset(&fuji_app, fuji_slot[slot], fuji_slot_npages[slot], fuji_ffpage);
    fuji_live = slot;
    fuji_boot_armed = false;
    fuji_have_staged = false;
}

#endif /* FUJI_CART_H */
