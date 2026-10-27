/* fuji_cart.h -- the cartridge-side half of the FujiNet mailbox.
 *
 * core1 records hotspot reads into a ring and goes back to polling; core0
 * drains it and runs the protocol. Replies are painted into the served
 * window, so the bus loop serves them like ROM.
 *
 * Ring entries are 13-bit cart offsets: on this port the address is the data.
 * core1 records one event per Enable assertion, since a ~500ns Z80 read spans
 * many loop iterations. The ROM swap and bank selects run inline on core1 so
 * a switch completes before the next read.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "astromap.h"

/* One transaction is a dozen paired reads plus at most a 320-byte stream,
 * and the console cannot start another until it sees ACKSEQ, so the ring
 * only ever has to hold one transaction's worth. */
#define FUJI_RING_LEN 512

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];   /* cart offset, 0x1D00-0x1FFF */
    volatile uint16_t head;                 /* written by core1 only */
    volatile uint16_t tail;                 /* written by core0 only */
    volatile bool     overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern volatile bool fuji_mailbox_active;

/* What core1 serves: bank[a >> 12][a & 0xFFF], plus the game hotspot tail
 * when hot_base is below ASTROMAP_HOT_OFF (a read there returns a & hot_mask
 * and retargets bank[1] into hot_image). app_store/app_npages arm the
 * APPBANK low-half selects. core0 builds fuji_next at stage time; core1
 * copies it into fuji_live field-by-field on the armed swap read. */
typedef struct {
    const uint8_t *volatile bank[2];
    volatile uint16_t hot_base;
    volatile uint8_t  hot_mask;
    const uint8_t *volatile hot_image;
    const uint8_t *volatile app_store;
    volatile uint8_t  app_npages;
} fuji_serve_t;

extern fuji_serve_t fuji_live, fuji_next;
extern uint8_t fuji_window[0x2000];
extern uint8_t fuji_staged[0x2000];
extern volatile bool fuji_boot_armed;
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
/* Stage a committed image for the armed swap. A banked image must stay put
 * while live (it is served in place); a FLAT image is copied. */
void fuji_cart_stage(const uint8_t *image, const astromap_plan_t *plan);
void fuji_cart_init(void);

#endif /* FUJI_CART_H */
