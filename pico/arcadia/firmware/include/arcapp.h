/* arcapp.h -- serving an APPBANK image: the window, the bank select and
 * the cart RAM writes (protocol 2, see fuji_mailbox.h).
 *
 * Everything here runs where the bus is served -- inline on core1, and in
 * the MAME device's read handler -- because each of these reads changes what
 * the very next read returns: after a bank select the next fetch from
 * $0800-$0FFF already comes from the new page, and a byte poked into cart
 * RAM may be read back by the next instruction. The host tests drive the
 * same functions. Header-only, so core1 inlines it all into SRAM.
 *
 * A flat image is served exactly as protocol 1 served it: win == base, and
 * arcapp_hotspot() takes nothing, so every hotspot read still reaches
 * fujimail.c.
 */

#ifndef ARCAPP_H
#define ARCAPP_H

#include <stdbool.h>
#include <stdint.h>

#include "arcmap.h"
#include "fuji_mailbox.h"

#if ARCMAP_STORE != FN_APP_MAX || ARCMAP_PAGE != FN_APP_PAGE
#error "arcmap.h and fuji_mailbox.h disagree about the APPBANK image"
#endif

typedef struct {
    uint8_t *base;              /* the served image: 8K base, then pages 1.. */
    const uint8_t *win;         /* what $0800-$0FFF shows, biased by -0x0800 */
    const uint8_t *ff;          /* FN_APP_PAGE bytes of 0xFF                 */
    uint8_t npages;             /* 0: flat                                   */
    uint8_t own;                /* armed cart RAM register, or 0             */
    uint16_t ptr;               /* cart RAM pointer, 0x000-0x3FF             */
} arcapp_t;

/* Serve image `base` (npages as arcmap_plan_t has it) from page 0. */
static inline void arcapp_reset(arcapp_t *a, uint8_t *base, uint8_t npages,
                                const uint8_t *ff)
{
    a->base = base;
    a->win = base;
    a->ff = ff;
    a->npages = npages;
    a->own = 0;
    a->ptr = 0;
}

/* The byte at image offset img (0x0000-0x1FFF, arcmap_decode's result). */
static inline uint8_t arcapp_byte(const arcapp_t *a, unsigned img)
{
    const uint8_t *p = ((img & 0x1800) == FN_APP_WIN) ? a->win : a->base;

    return p[img];
}

static inline void arcapp_select(arcapp_t *a, unsigned n)
{
    if (n == 0)
        a->win = a->base;
    else if (n < a->npages)
        a->win = a->base + ARCMAP_WINDOW + (n - 1) * FN_APP_PAGE - FN_APP_WIN;
    else
        a->win = a->ff - FN_APP_WIN;
}

/* Cart RAM address -> image offset. */
static inline unsigned arcapp_ram_offset(unsigned ptr)
{
    return (ptr < FN_RAM_HALF ? FN_RAM_L : FN_RAM_H - FN_RAM_HALF) + ptr;
}

/* One hotspot read (img >= FN_H_REGSEL) while the mailbox is live. True if
 * it was taken here and must NOT be forwarded to fujimail.c. */
static inline bool arcapp_hotspot(arcapp_t *a, unsigned img)
{
    unsigned lo = img & 0xFF;

    if (a->npages == 0)
        return false;
    switch (img & FN_H_PAGE_MASK) {
    case FN_H_REGSEL:
        if (lo >= FN_HOT_BANK && lo <= FN_HOT_BANK_LAST) {
            arcapp_select(a, lo - FN_HOT_BANK);
            return true;
        }
        if (lo >= FN_REG_RAMPTR0 && lo <= FN_REG_RAMPOKE) {
            a->own = (uint8_t) lo;
            return true;
        }
        if (lo < 0x80)
            a->own = 0;         /* any other register ends a burst */
        return false;
    case FN_H_REGDATA:
        if (a->own == 0)
            return false;
        if (a->own == FN_REG_RAMPOKE) {
            a->base[arcapp_ram_offset(a->ptr)] = (uint8_t) lo;
            a->ptr = (uint16_t)((a->ptr + 1) & (FN_RAM_SIZE - 1));
        } else {
            a->ptr = (uint16_t)(((a->own - FN_REG_RAMPTR0) << 8) | lo);
            a->own = 0;
        }
        return true;
    default:
        return false;
    }
}

#endif /* ARCAPP_H */
