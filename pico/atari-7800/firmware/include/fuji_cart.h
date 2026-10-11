/* fuji_cart.h -- what core1 serves, what core0 drives, and the ring between.
 *
 * core1 (a78_cart.c) owns the bus: the slot table it patches inline on a
 * mapper write, the mode flips (FN_HOT_SWAP, _CONFIG, _GO, the BIOS flip),
 * the INPTCTRL model and the HSC shadow. Everything slower -- fujimail, the
 * load sequence, POKEY sound, HSC saves, the watchdog -- is core0's, fed by
 * events through the ring.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "a78_cart.h"
#include "a78map.h"
#include "fuji_load.h"

/* One transaction is a dozen register writes plus a 320-byte stream, and the
 * console cannot start another before it sees ACKSEQ. */
#define FUJI_RING_LEN 1024

typedef struct {
    volatile a78_event_t buf[FUJI_RING_LEN];
    volatile uint16_t head;                /* written by core1 only */
    volatile uint16_t tail;                /* written by core0 only */
    volatile bool overflow;
} fuji_ring_t;

/* core0 -> core1 requests, taken between bus cycles. */
#define FUJI_REQ_BOOT 0x01                 /* console gone: the boot block when it is back */

extern fuji_ring_t fuji_ring;
extern uint8_t fuji_arena[FN_ARENA_SIZE];
extern const uint8_t *fuji_loaderrom;      /* FN_LOADER_SIZE                */
extern const uint8_t *fuji_bootblk;        /* FN_BOOTBLK_SIZE               */
extern volatile a78_bus_t fuji_bus;        /* core1's; core0 reads the model */
extern a78map_t *volatile fuji_live;       /* the image's slots, while it runs */
extern fuji_load_t fuji_loader;
extern volatile uint8_t fuji_pokey_rd[16]; /* what a POKEY read returns      */
extern uint8_t fuji_hsc_shadow[A78MAP_HSC_RAM_SIZE];
extern volatile uint32_t fuji_hsc_dirty;   /* one bit per 64-byte chunk      */
extern volatile uint32_t fuji_req;
extern volatile bool fuji_pal_seen;        /* the BIOS ran from $C000: a PAL console */
extern volatile uint32_t fuji_bus_cycle;

/* core1: record one event; inlined into the bus loop. */
static inline void fuji_cart_note(uint16_t offset, uint8_t data, uint8_t kind, uint32_t cycle)
{
    uint16_t head = fuji_ring.head;
    uint16_t next = (uint16_t)((head + 1u) % FUJI_RING_LEN);

    if (next == fuji_ring.tail) {
        fuji_ring.overflow = true;         /* core0 fell behind; drop, do not wrap */
        return;
    }
    fuji_ring.buf[head].offset = offset;
    fuji_ring.buf[head].data = data;
    fuji_ring.buf[head].kind = kind;
    fuji_ring.buf[head].cycle = cycle;
    fuji_ring.head = next;
}

/* core0 */
void fuji_cart_init(const uint8_t *loader, const uint8_t *bootblk,
                    const uint8_t *config, unsigned config_len);
bool fuji_cart_next_event(a78_event_t *ev);
void fuji_cart_poke(unsigned offset, uint8_t value);
void fuji_cart_service(void);              /* watchdog, the model, the LED */

#endif /* FUJI_CART_H */
