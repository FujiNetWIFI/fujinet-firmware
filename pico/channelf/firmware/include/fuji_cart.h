/* fuji_cart.h -- the cartridge-side half of the FujiNet mailbox.
 *
 * core1 records mailbox writes into a ring as hotspot offsets (one store
 * becomes the REGSEL/REGDATA pair fujimail.c decodes); core0 drains it and
 * paints replies into the arena core1 serves. The arena is single and
 * survives a swap. Images are ping-ponged because a swapped-in client runs
 * from its slot. core1 does the swap inline, before the next fetch.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "channelf_cart.h"
#include "chfmap.h"

/* One transaction: a dozen register writes (two entries each) plus at most a
 * 320-byte stream; the console waits for ACKSEQ before the next. */
#define FUJI_RING_LEN 1024

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];  /* arena offsets, 0x7D00-0x7FFF */
    volatile uint16_t head;                /* written by core1 only */
    volatile uint16_t tail;                /* written by core0 only */
    volatile bool overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern volatile bool fuji_mailbox_active;

extern uint8_t fuji_store[2][CHFMAP_STORE]; /* whole images, ping-ponged      */
extern volatile uint8_t fuji_slot_npages[2]; /* APPBANK pages in each slot     */
extern uint8_t fuji_arena[FN_ARENA_SIZE];    /* 32K: RAM + painted mailbox     */
extern volatile uint8_t fuji_live_win;       /* slot core1 serves; core1 owns it */
uint8_t *fuji_cart_stage_window(void);       /* core0: the slot that is NOT live */

extern volatile bool fuji_boot_armed;
extern volatile bool fuji_staged_claims;
extern volatile bool fuji_have_staged;

/* core1: record one mailbox event. */
CHF_INLINE void fuji_cart_note(uint16_t offset)
{
    uint16_t head = fuji_ring.head;
    uint16_t next = (uint16_t)((head + 1u) % FUJI_RING_LEN);

    if (next == fuji_ring.tail) {
        fuji_ring.overflow = true;  /* core0 fell behind; drop, do not wrap */
        return;
    }
    fuji_ring.buf[head] = offset;
    fuji_ring.head = next;
}

/* core0 */
bool fuji_cart_next_event(uint16_t *offset);
void fuji_cart_poke(unsigned offset, uint8_t value);
void fuji_cart_stage(const uint8_t *image, const chfmap_plan_t *plan);
void fuji_cart_init(void);

#endif /* FUJI_CART_H */
