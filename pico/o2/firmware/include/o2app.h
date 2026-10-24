/* o2app.h -- an APPBANK image as the console sees it (protocol 2).
 *
 * Header-only and hardware-free: the cartridge's core1 bus loop inlines these
 * into SRAM, the o2em model calls them on every fetch, and the host tests drive
 * them directly. One decode, three users -- the same reason o2map.c and
 * fujimail.c are shared.
 *
 * The layout is in fuji_mailbox.h: a fixed 1K at $400-$7FF that the console can
 * write through FN_REG_POKE, pages of 2K mapped at $800-$F1F by FN_REG_CODEBANK,
 * and the mailbox at $F20-$FFF with the reply window served straight out of the
 * reply buffer.
 */

#ifndef O2APP_H
#define O2APP_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"

typedef struct {
    uint8_t *image;          /* the whole image, fixed half first; writable    */
    unsigned npages;
    uint32_t pagebase;       /* image[pagebase + addr] is console address addr
                              * of the selected page (mod 2^32: page 0 starts
                              * below $800 in console terms)                  */
    unsigned pagenum;
    uint8_t regsel;          /* a copy of the console's last FN_W_REGSEL       */
    uint16_t poke;           /* next FN_REG_POKE offset into the fixed half    */
    uint16_t win;            /* reply byte shown at FN_R_DATA                  */
} o2app_t;

static inline void o2app_select(o2app_t *a, unsigned n)
{
    if (n >= a->npages)
        n = 0;              /* a page that is not there reads as the boot page */
    a->pagenum = n;
    a->pagebase = (uint32_t) O2_APP_FIXED_BYTES + (uint32_t) n * O2_APP_PAGE_BYTES
                  - (uint32_t) O2_APP_PAGE_BASE;
}

static inline void o2app_init(o2app_t *a, uint8_t *image, unsigned npages)
{
    a->image = image;
    a->npages = npages;
    a->regsel = 0;
    a->poke = 0;
    a->win = 0;
    o2app_select(a, 0);
}

/* One console write to the mailbox register file. Returns true when the write
 * was an APPBANK register's and must NOT reach the mailbox service: a 600-byte
 * POKE burst through the service's ring would overflow it, and none of these
 * registers mean anything to the service anyway. REGSEL is copied here and
 * still passed on, so both sides agree on what the next REGDATA is for. */
static inline bool o2app_write(o2app_t *a, uint8_t addr, uint8_t data)
{
    if (addr == FN_W_REGSEL) {
        a->regsel = data;
        return false;
    }
    if (addr != FN_W_REGDATA)
        return false;

    switch (a->regsel) {
    case FN_REG_CODEBANK:
        o2app_select(a, data);
        return true;
    case FN_REG_POKE_LO:
        a->poke = (uint16_t)((a->poke & 0x300) | data);
        return true;
    case FN_REG_POKE_HI:
        a->poke = (uint16_t)(((data & 3u) << 8) | (a->poke & 0xFF));
        return true;
    case FN_REG_POKE:
        a->image[a->poke] = data;
        a->poke = (uint16_t)((a->poke + 1u) & (O2_APP_FIXED_BYTES - 1));
        return true;
    case FN_REG_RXWIN:
        a->win = (uint16_t)(data * O2_APP_RXWIN_UNIT);
        return true;
    case FN_REG_RXSLICE:
        a->win = (uint16_t)(data * FN_R_SLICE_LEN);
        return true;
    default:
        return false;
    }
}

/* One console fetch, $400-$FFF. `mbox` is the published mailbox page indexed by
 * console address ($F20-$F2F is all that is read from it); `rx`/`rxlen` the
 * reply. */
static inline uint8_t o2app_read(const o2app_t *a, unsigned addr,
                                 const uint8_t *mbox,
                                 const uint8_t *rx, unsigned rxlen)
{
    unsigned i;

    if (addr < O2_APP_PAGE_BASE)
        return a->image[addr & (O2_APP_FIXED_BYTES - 1)];
    if (addr < O2_APP_PAGE_END)
        return a->image[a->pagebase + addr];
    if (addr < FN_R_DATA)
        return mbox[addr];
    i = a->win + (addr - FN_R_DATA);
    return (i < rxlen) ? rx[i] : 0;
}

#endif /* O2APP_H */
