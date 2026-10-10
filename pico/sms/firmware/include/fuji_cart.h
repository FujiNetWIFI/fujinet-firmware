/* fuji_cart.h -- what core1 serves, what core0 drives, and the ring between.
 *
 * core1 (sms_cart.c) owns the bus: the bank bits, the mode bits it flips
 * inline (FN_HOT_SWAP, FN_HOT_CONFIG, the flip at $0000, the RAM gate) and
 * the BIOS snoop. Everything slower -- fujimail, the load sequence, the
 * watchdog -- is core0's, fed by hotspot events through the ring.
 */

#ifndef FUJI_CART_H
#define FUJI_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "sms_cart.h"
#include "smsmap.h"
#include "fuji_load.h"

/* One transaction is a dozen register writes plus a 320-byte stream, and the
 * console cannot start another before it sees ACKSEQ. */
#define FUJI_RING_LEN 1024

typedef struct {
    volatile sms_event_t buf[FUJI_RING_LEN];
    volatile uint16_t head;                /* written by core1 only */
    volatile uint16_t tail;                /* written by core0 only */
    volatile bool overflow;
} fuji_ring_t;

/* core0 -> core1 requests, taken between bus cycles. */
#define FUJI_REQ_RESIDENT 0x01             /* console gone: back to CONFIG */

extern fuji_ring_t fuji_ring;
extern uint8_t fuji_arena[FN_ARENA_SIZE];
extern uint8_t fuji_ram_shadow[SMSMAP_RAM_MAX];
extern uint8_t fuji_window[FN_LOADWIN_SIZE];
extern sms_bus_t fuji_bus;                 /* core1's; core0 reads the snoop */
extern smsmap_t fuji_resident_map;         /* RESIDENT: lut[32..39] = load bank */
extern smsmap_t *volatile fuji_live;       /* the table core1 drives from */
extern fuji_load_t fuji_loader;            /* .next / .next_mode: the flip's target */
extern const uint8_t *fuji_ptab_resident[SMS_PAGES];
extern const uint8_t *fuji_ptab_app[SMS_PAGES];
extern const uint8_t *fuji_ptab_game[SMS_PAGES];
extern volatile uint32_t fuji_req;
extern volatile bool fuji_reset_seen;      /* core1 -> core0: console reset */

/* core1: record one event; inlined into the bus loop. */
static inline void fuji_cart_note(uint16_t offset, uint8_t data, uint8_t kind)
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
    fuji_ring.head = next;
}

/* core0 */
void fuji_cart_init(const uint8_t *loader, unsigned loader_len,
                    const uint8_t *resident, unsigned resident_len);
bool fuji_cart_next_event(sms_event_t *ev);
void fuji_cart_poke(unsigned offset, uint8_t value);
void fuji_cart_service(void);              /* reset and the watchdog */

#endif /* FUJI_CART_H */
