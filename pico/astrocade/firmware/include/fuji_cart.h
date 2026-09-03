/* fuji_cart.h -- the cartridge-side half of the FujiNet mailbox.
 *
 * Split so that core1's bus loop does as little as possible. core1 records
 * hotspot READS into a ring and returns to polling; core0 drains the ring
 * and runs the protocol. Replies need no bus-loop change at all: the
 * mailbox is painted straight into the served window, so serving it is the
 * same instruction that already serves ROM.
 *
 * Astrocade specifics versus the O2 original:
 *   - entries are 13-bit cart offsets, not (addr, data) pairs -- on this
 *     port the address IS the data;
 *   - core1 records at most one event per Enable assertion (it spins until
 *     Enable deasserts before looking again), because a ~500ns Z80 read
 *     spans many polling-loop iterations and recording each one would
 *     append duplicate TX bytes;
 *   - the ROM swap is core1's, triggered by the armed FN_HOT_SWAP read.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

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

/* The served window: core1 reads through fuji_rom, which flips to
 * fuji_staged_rom on the armed swap read. */
extern uint8_t fuji_window[0x2000];
extern uint8_t fuji_staged[0x2000];
extern uint8_t *volatile fuji_rom;
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
void fuji_cart_stage(const uint8_t window[0x2000], bool claims);
void fuji_cart_init(void);

#endif /* FUJI_CART_H */
