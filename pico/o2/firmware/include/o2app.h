/* o2app.h -- an APPBANK image as the console sees it (protocol 2; layout in
 * fuji_mailbox.h). Header-only so core1's bus loop can inline it; the o2em
 * model and host tests share it.
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

static inline void o2app_select(o2app_t *app, unsigned page)
{
    if (page >= app->npages)
        page = 0;           /* a page that is not there reads as the boot page */
    app->pagenum = page;
    app->pagebase = (uint32_t) O2_APP_FIXED_BYTES + (uint32_t) page * O2_APP_PAGE_BYTES
                  - (uint32_t) O2_APP_PAGE_BASE;
}

static inline void o2app_init(o2app_t *app, uint8_t *image, unsigned npages)
{
    app->image = image;
    app->npages = npages;
    app->regsel = 0;
    app->poke = 0;
    app->win = 0;
    o2app_select(app, 0);
}

/* True when the write was to an APPBANK register and must not reach the mailbox
 * service: a 600-byte POKE burst would overflow its ring. REGSEL is copied and
 * still passed on. */
static inline bool o2app_write(o2app_t *app, uint8_t addr, uint8_t data)
{
    if (addr == FN_W_REGSEL) {
        app->regsel = data;
        return false;
    }
    if (addr != FN_W_REGDATA)
        return false;

    switch (app->regsel) {
    case FN_REG_CODEBANK:
        o2app_select(app, data);
        return true;
    case FN_REG_POKE_LO:
        app->poke = (uint16_t)((app->poke & 0x300) | data);
        return true;
    case FN_REG_POKE_HI:
        app->poke = (uint16_t)(((data & 3u) << 8) | (app->poke & 0xFF));
        return true;
    case FN_REG_POKE:
        app->image[app->poke] = data;
        app->poke = (uint16_t)((app->poke + 1u) & (O2_APP_FIXED_BYTES - 1));
        return true;
    case FN_REG_RXWIN:
        app->win = (uint16_t)(data * O2_APP_RXWIN_UNIT);
        return true;
    case FN_REG_RXSLICE:
        app->win = (uint16_t)(data * FN_R_SLICE_LEN);
        return true;
    default:
        return false;
    }
}

/* One console fetch, $400-$FFF. `mbox` is indexed by console address; only
 * $F20-$F2F is read from it. */
static inline uint8_t o2app_read(const o2app_t *app, unsigned addr,
                                 const uint8_t *mbox,
                                 const uint8_t *reply, unsigned rxlen)
{
    unsigned idx;

    if (addr < O2_APP_PAGE_BASE)
        return app->image[addr & (O2_APP_FIXED_BYTES - 1)];
    if (addr < O2_APP_PAGE_END)
        return app->image[app->pagebase + addr];
    if (addr < FN_R_DATA)
        return mbox[addr];
    idx = app->win + (addr - FN_R_DATA);
    return (idx < rxlen) ? reply[idx] : 0;
}

#endif /* O2APP_H */
