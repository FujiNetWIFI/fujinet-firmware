/* fuji_cart.h -- the cartridge-side half of the FujiNet mailbox.
 *
 * Split so that core1's bus loop does as little as possible. core1 records
 * hotspot READS into a ring and returns to polling; core0 drains the ring and
 * runs the protocol. Replies need no bus-loop change at all: the mailbox is
 * painted straight into the served window, so serving it is the same
 * instruction that already serves ROM.
 *
 * ColecoVision specifics versus the Astrocade original:
 *   - the window is 32K, so entries are 15-bit cart offsets;
 *   - core1 records at most one event per chip-select assertion (it spins
 *     until the select deasserts before looking again), because a ~560 ns Z80
 *     read spans several polling-loop iterations and recording each one would
 *     append duplicate TX bytes;
 *   - the ROM swap is core1's, inline: a switch must be complete before the
 *     next read can begin, and spin-until-deassert guarantees exactly that.
 *   - there is no APPBANK scheme here. A client image is always a flat 32K,
 *     because the window is roomy enough that nothing has needed more; the
 *     banked kinds are all game mappers, and they only ever run with the
 *     mailbox already dead.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "coleco_cart.h"
#include "colmap.h"

/* One transaction is a dozen paired reads plus at most a 320-byte stream, and
 * the console cannot start another until it sees ACKSEQ, so the ring only ever
 * has to hold one transaction's worth. */
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

/* Two 32K windows, ping-ponged. Whichever one the console is being served from
 * is live; the other is where the next image is staged. They have to alternate
 * rather than being a fixed pair, because a FujiNet client that has itself been
 * swapped in is RUNNING out of the window it was staged into -- staging the
 * next image into that same buffer would rewrite the code currently executing.
 * (The Astrocade port has a fixed window/staged pair and the same latent
 * hazard; it never bit because only its baked CONFIG ever pushes.) */
extern uint8_t fuji_win[2][COLMAP_WINDOW];
extern volatile uint8_t fuji_live_win;   /* index core1 is serving; core1 owns it */
uint8_t *fuji_cart_stage_window(void);   /* core0: the one that is NOT live */

extern volatile bool fuji_boot_armed;
extern volatile bool fuji_staged_claims;
extern volatile bool fuji_have_staged;

/* core1: record one hotspot read. Inlined into the bus loop, so it must stay a
 * compare and two stores. */
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
/* Stage a committed image for the armed swap. `image` is the full raw image;
 * for the banked kinds it must be persistent storage (the serve base points
 * into it for as long as it is live) -- a flat image is copied and may be
 * transient. */
void fuji_cart_stage(const uint8_t *image, const colmap_plan_t *plan);
void fuji_cart_init(void);

#endif /* FUJI_CART_H */
