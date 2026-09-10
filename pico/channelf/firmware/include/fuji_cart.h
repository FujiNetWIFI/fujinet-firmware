/* fuji_cart.h -- the cartridge-side half of the FujiNet mailbox.
 *
 * Split so core1's bus loop does as little as possible. core1 records mailbox
 * WRITES into a ring and returns to the bus; core0 drains the ring and runs
 * the protocol. Replies need no bus-loop change at all: the mailbox is painted
 * into the arena core1 already serves.
 *
 * Channel F specifics versus the ColecoVision original:
 *   - the ring carries hotspot offsets, exactly as there, even though the
 *     console writes rather than reads. core1 turns one store into the
 *     REGSEL/REGDATA pair fujimail.c decodes, so that file stays verbatim.
 *   - there is one arena, not a ping-pong pair: it is separate memory from the
 *     ROM window, so it survives a swap untouched and a staged client boots
 *     into the status pages it was already watching.
 *   - the ROM window IS ping-ponged, for the reason the ColecoVision found: a
 *     client that has itself been swapped in is running out of that window,
 *     and staging the next image into the same buffer would rewrite the code
 *     currently executing.
 *   - the swap is core1's, inline: it must be complete before the next fetch.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "channelf_cart.h"
#include "chfmap.h"

/* One transaction is a dozen register writes plus at most a 320-byte stream,
 * and the console cannot start another until it sees ACKSEQ, so the ring only
 * ever has to hold one transaction's worth. A register write costs two
 * entries, since it expands to the REGSEL/REGDATA pair. */
#define FUJI_RING_LEN 1024

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];  /* arena offsets, 0x7D00-0x7FFF */
    volatile uint16_t head;                /* written by core1 only */
    volatile uint16_t tail;                /* written by core0 only */
    volatile bool overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern volatile bool fuji_mailbox_active;

extern uint8_t fuji_win[2][CHFMAP_WINDOW]; /* the 16K ROM window, ping-ponged */
extern uint8_t fuji_arena[FN_ARENA_SIZE];  /* 32K: RAM + painted mailbox      */
extern volatile uint8_t fuji_live_win;     /* index core1 serves; core1 owns it */
uint8_t *fuji_cart_stage_window(void);     /* core0: the one that is NOT live */

extern volatile bool fuji_boot_armed;
extern volatile bool fuji_staged_claims;
extern volatile bool fuji_have_staged;

/* core1: record one mailbox event. Inlined into the bus loop, so it stays a
 * compare and two stores. */
static inline void fuji_cart_note(uint16_t offset)
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
void channelf_cart_refresh(void);

#endif /* FUJI_CART_H */
