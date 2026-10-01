/* fuji_cart.h -- the cartridge-side state: what core1 serves, what core0
 * drives, and the ring between them.
 *
 * core1 records every mailbox-page write into a ring and returns to the bus;
 * core0 drains the ring into fujimail.c. Mapper writes core1 applies itself,
 * because the PIO bank table has to be patched before the next fetch; only
 * the slow consequences (mirroring, write gates, the MMC3 interrupt
 * registers) cross to core0.
 *
 * The image never lives here. It is staged in fuji_store (RAM or flash) and
 * the 6502 copies it into the external SRAMs through the loader ROM, one 1K
 * slice at a time through the reply window; fuji_cart_service() runs that
 * sequence.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "nes_cart.h"
#include "nesmap.h"

/* One transaction is a dozen register writes plus at most a 320-byte stream,
 * and the console cannot start another until it sees ACKSEQ, so the ring
 * only ever has to hold one transaction's worth. */
#define FUJI_RING_LEN 1024

typedef struct {
    volatile nes_event_t buf[FUJI_RING_LEN];
    volatile uint16_t head;                /* written by core1 only */
    volatile uint16_t tail;                /* written by core0 only */
    volatile bool overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern uint8_t fuji_arena[FN_ARENA_SIZE];      /* reply, status, loader ROM */
extern uint8_t fuji_wram[NESMAP_WRAM_MAX];
extern uint8_t fuji_vectors[256];              /* $FF00 page while SRAM_EN=0 */
extern volatile nes_serve_t fuji_serve;        /* core0 writes, core1 reads  */
extern nesmap_t fuji_map;                      /* the live mapper; core1's   */
extern volatile uint32_t fuji_bus_cycle;       /* M2 rising edges, core1     */
extern volatile bool fuji_boot_armed;
extern volatile bool fuji_have_staged;
extern volatile uint8_t fuji_slow_dirty;       /* core1 -> core0: mirror/gates changed */
extern volatile uint8_t fuji_irq_dirty;        /* core1 -> core0: irq_line changed     */

/* core1: record one event. Inlined into the bus loop, so it stays a compare
 * and a few stores. */
static inline void fuji_cart_note(uint16_t offset, uint8_t data, uint8_t kind, uint32_t cycle)
{
    uint16_t head = fuji_ring.head;
    uint16_t next = (uint16_t)((head + 1u) % FUJI_RING_LEN);

    if (next == fuji_ring.tail) {
        fuji_ring.overflow = true;  /* core0 fell behind; drop, do not wrap */
        return;
    }
    fuji_ring.buf[head].offset = offset;
    fuji_ring.buf[head].data = data;
    fuji_ring.buf[head].kind = kind;
    fuji_ring.buf[head].cycle = cycle;
    fuji_ring.head = next;
}

/* core0 */
bool fuji_cart_next_event(nes_event_t *ev);
void fuji_cart_poke(unsigned offset, uint8_t value);
void fuji_cart_init(const uint8_t *loader_rom, unsigned loader_len);
void fuji_cart_set_resident(const uint8_t *image, const nesmap_plan_t *plan);
void fuji_cart_stage(const uint8_t *image, const nesmap_plan_t *plan);
bool fuji_cart_store_busy(const uint8_t *base);
void fuji_cart_request_load(void);             /* FN_HOT_SWAP, armed        */
void fuji_cart_slice_acked(void);              /* FN_REG_SLICE_ACK          */
void fuji_cart_service(void);                  /* the load sequence, gates, watchdog */
void fuji_cart_sr_write(uint8_t bits);         /* the 74HCT595              */
void fuji_cart_set_irq(bool asserted);         /* /IRQ: drive low or release */

#endif /* FUJI_CART_H */
