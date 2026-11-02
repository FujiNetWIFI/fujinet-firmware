/* fuji_cart.h -- the cartridge's half of the mailbox. core1 records hotspot reads in a ring
 * and goes back to the bus; core0 drains it. Replies are painted into the served window, so
 * serving them is serving ROM. core1 records once per select and swaps inline, so a swap
 * is done before the next read. A client image is always a flat 32K; there is no APPBANK.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "coleco_cart.h"
#include "colmap.h"

/* One transaction (a dozen register pairs and up to 320 TX bytes) at a time. */
#define FUJI_RING_LEN 512

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];   /* cart offset, 0x7D00-0x7FFF */
    volatile uint16_t head;                 /* written by core1 only */
    volatile uint16_t tail;                 /* written by core0 only */
    volatile bool     overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern volatile bool fuji_mailbox_active;

extern fuji_serve_t fuji_live, fuji_next;

/* Two 32K windows: one served, the next image staged in the other. They alternate, since a
 * client that was itself swapped in runs from the window it was staged in. */
extern uint8_t fuji_win[2][COLMAP_WINDOW];
extern volatile uint8_t fuji_live_win;   /* index core1 is serving; core1 owns it */
uint8_t *fuji_cart_stage_window(void);   /* core0: the one that is NOT live */

extern volatile bool fuji_boot_armed;
extern volatile bool fuji_staged_claims;
extern volatile bool fuji_have_staged;

/* core1: record one hotspot read; inlined into the bus loop. */
COLECO_HOT void fuji_cart_note_read(uint16_t offset)
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
/* Stage a committed image for the armed swap. A banked image is served in place, so it must
 * outlive the swap; a flat one is copied. */
void fuji_cart_stage(const uint8_t *image, const colmap_plan_t *plan);
void fuji_cart_init(void);

#endif /* FUJI_CART_H */
