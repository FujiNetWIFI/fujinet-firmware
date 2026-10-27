/* fuji_cart.h -- the cartridge-side mailbox. core1 rings mailbox writes for
 * core0 to run; reads need nothing, as the mailbox is painted into rom_table[].
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "o2app.h"

/* Holds one transaction (~6 register writes plus up to 256 payload bytes); the
 * console cannot start another until it sees ACKSEQ. */
#define FUJI_RING_LEN 512

typedef struct {
    volatile uint16_t buf[FUJI_RING_LEN];   /* (addr << 8) | data */
    volatile uint16_t head;                 /* written by core1 only */
    volatile uint16_t tail;                 /* written by core0 only */
    volatile bool     overflow;
} fuji_ring_t;

extern fuji_ring_t fuji_ring;
extern volatile bool fuji_mailbox_active;

/* Protocol 2: core0 fills fuji_app and arms it before the boot swap; core1 sets
 * it live at the $400 fetch that swaps the tables and owns it from then on. */
extern o2app_t fuji_app;
extern volatile bool fuji_app_armed;
extern volatile bool fuji_app_live;

/* core1 bus loop: must stay a compare and two stores. */
static inline void fuji_cart_note_write(uint8_t addr, uint8_t data)
{
    uint16_t head = fuji_ring.head;
    uint16_t next = (uint16_t)((head + 1u) % FUJI_RING_LEN);

    if (next == fuji_ring.tail) {
        fuji_ring.overflow = true;      /* core0 fell behind; drop, don't wrap */
        return;
    }
    fuji_ring.buf[head] = (uint16_t)(((uint16_t)addr << 8) | data);
    fuji_ring.head = next;
}

/* core0 */
bool fuji_cart_next_write(uint8_t *addr, uint8_t *data);
void fuji_cart_poke(unsigned prog_addr, uint8_t value);
void fuji_cart_paint(void);
void fuji_cart_stage_boot(void);
void fuji_cart_init(void);

#endif /* FUJI_CART_H */
