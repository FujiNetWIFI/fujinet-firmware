/* fuji_cart.h -- the cartridge-side half of the FujiNet mailbox.
 *
 * Split so core1's bus loop does as little as possible. core1 serves the bus
 * and records mailbox writes into a ring; core0 drains the ring and runs the
 * protocol. Replies need no bus-loop change at all: the mailbox is painted
 * into the window core1 already serves.
 *
 * WHAT CORE1 MUST DO ITSELF, and why. A bank select and a ROM swap have to be
 * complete before the console's very next fetch -- there is no handshake and
 * no wait state on this bus -- so they are handled inline in the loop and
 * never queued. Everything else can wait for core0.
 *
 * WHAT CORE1 MUST NOT DO. Composing a text row writes 36 bytes through a font
 * lookup, which is far longer than the 838 ns until the console's next access.
 * Missing a bus cycle here does not slow anything down, it serves the wrong
 * byte -- so rendering is core0's, requested through a flag. The cartridge
 * publishes FN_B_TEXTGEN when the row is actually on the planes, which is what
 * a client polls if it cares.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "vcs_cart.h"

/* One transaction is a dozen register writes plus at most a 320-byte stream,
 * and the console cannot start another until it sees ACKSEQ, so the ring only
 * ever has to hold one transaction's worth. A register write costs two
 * entries, since it expands to the REGSEL/REGDATA pair fujimail.c decodes. */
#define FUJI_RING_LEN 1024

/* The largest image that can be served. MAME's cartridge whitelist and our own
 * (N+1)*2048 layout meet at 32K, and there is no point staging what cannot be
 * loaded. Two of them, ping-ponged, because a client that has itself been
 * booted is RUNNING out of the live one -- staging into the same buffer would
 * rewrite the code currently executing. That is the ColecoVision's lesson and
 * it applies here unchanged. */
#define FUJI_IMAGE_MAX 32768u

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];
    volatile uint16_t head;                /* written by core1 only */
    volatile uint16_t tail;                /* written by core0 only */
    volatile bool overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern vcs_mem_t fuji_mem;                 /* the served window and its decode */

extern uint8_t fuji_image[2][FUJI_IMAGE_MAX];
extern uint32_t fuji_image_len[2];
extern volatile uint8_t fuji_live_image;   /* core1 owns this */
uint8_t *fuji_cart_stage_buffer(void);     /* core0: the one that is NOT live */

extern volatile bool fuji_have_staged;
extern volatile uint32_t fuji_staged_len;

/* core0 work requested by core1, which cannot afford to do it in the loop. */
extern volatile bool fuji_render_req;
extern volatile bool fuji_blit_req;
extern volatile uint8_t fuji_blit_xform;

void fuji_cart_init(void);
void fuji_cart_poke(unsigned offset, uint8_t value);
void fuji_cart_stage(uint32_t len);        /* core0: a push is complete */
void fuji_cart_serve_staged(void);         /* core1: the swap */

/* core1: record one mailbox event. Inlined into the bus loop, so it stays a
 * compare and two stores. A full ring drops the event and latches `overflow`
 * rather than blocking: the bus will not wait. */
static inline void fuji_cart_note(uint16_t offset)
{
    uint16_t head = fuji_ring.head;
    uint16_t next = (uint16_t)((head + 1u) % FUJI_RING_LEN);

    if (next == fuji_ring.tail) {
        fuji_ring.overflow = true;
        return;
    }
    fuji_ring.buf[head] = offset;
    fuji_ring.head = next;
}

/* core0: take the next event, if any. */
static inline bool fuji_cart_next_event(uint16_t *offset)
{
    uint16_t tail = fuji_ring.tail;

    if (tail == fuji_ring.head)
        return false;
    *offset = fuji_ring.buf[tail];
    fuji_ring.tail = (uint16_t)((tail + 1u) % FUJI_RING_LEN);
    return true;
}

void vcs_core1_main(void);

#endif /* FUJI_CART_H */
