/* fuji_cart.h -- what core1's bus loop and core0's mailbox service share. core1 rings
 * hotspot reads for core0; replies are painted into the window core1 already serves.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "tmplmap.h"

/* Holds one transaction (a dozen paired reads plus up to a 320-byte stream);
 * the console cannot start another until it sees ACKSEQ. */
#define FUJI_RING_LEN 512

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];   /* window offset, FN_H_REGSEL up */
    volatile uint16_t head;                 /* written by core1 only */
    volatile uint16_t tail;                 /* written by core0 only */
    volatile bool     overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern volatile bool fuji_mailbox_active;

/* core1 serves fuji_serve_base and repoints it at fuji_staged on the armed
 * FN_HOT_SWAP read. */
extern const uint8_t *volatile fuji_serve_base;
extern uint8_t fuji_window[TMPLMAP_WINDOW];
extern uint8_t fuji_staged[TMPLMAP_WINDOW];
extern volatile bool fuji_boot_armed;
extern volatile bool fuji_have_staged;
extern volatile bool fuji_staged_claims;

/* core1 bus loop: must stay a compare and two stores. */
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
void fuji_cart_stage(const uint8_t *image, const tmplmap_plan_t *plan);
void fuji_cart_init(void);

#endif /* FUJI_CART_H */
