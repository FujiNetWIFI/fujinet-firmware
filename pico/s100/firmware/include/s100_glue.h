/* s100_glue.h -- the card's decode and wait-state logic as equations, the model
 * test_glue checks and the board's 74HCT parts implement.
 *
 *   PIPE   = (sINP | sOUT) & !sINTA & A2-A7 == pipe DIP
 *   SIO    = (sINP | sOUT) & !sINTA & A2-A7 == SIO DIP & SIO_EN
 *   FDC    = (sINP | sOUT) & !sINTA & A2-A7 == 000010b & FDC_EN
 *   PAGE   = sMEMR & !sINTA & ((ROM_ON & A8-A15 == page DIP) | POJ_ARMED)
 *   DECODE = PIPE | SIO | FDC | PAGE; the block code favors FDC, then PIPE, then SIO
 *   ARM    : D-flip-flop, D=1, clocked by pSYNC rising, cleared by /WAIT_CLR or RESET*
 *   PRDY*  = !(ARM & DECODE), open collector; WAITQ = ARM & DECODE
 *   DI_OE  = DECODE & pDBIN & (sINP | sMEMR)
 *   PHANTOM* = !(PAGE & PH_EN), open collector
 * The RP acts on WAITQ only after it has seen pSYNC rise and fall, so a decode
 * glitch while the address changes is never serviced.
 */
#ifndef S100_GLUE_H
#define S100_GLUE_H

#include <stdbool.h>
#include <stdint.h>

#include "s100_card.h"

typedef struct {
    bool sinp, sout, smemr, sinta;
    uint16_t addr;
} glue_cycle_t;

typedef struct {
    uint8_t pipe_base, sio_base, page;
    bool sio_en, fdc_en, rom_on, poj_armed, ph_en;
} glue_cfg_t;

typedef struct {
    bool decode;
    uint8_t blk;
    bool phantom;
} glue_out_t;

static inline glue_out_t glue_decode(const glue_cfg_t *g, const glue_cycle_t *c)
{
    glue_out_t o = {false, 0, false};
    bool io = (c->sinp || c->sout) && !c->sinta;
    uint8_t blk4 = (uint8_t)(c->addr & 0xFC);
    bool pipe = io && blk4 == (g->pipe_base & 0xFC);
    bool sio = io && g->sio_en && blk4 == (g->sio_base & 0xFC);
    bool fdc = io && g->fdc_en && blk4 == S100_FDC_BASE;
    bool page = c->smemr && !c->sinta &&
                ((g->rom_on && (c->addr >> 8) == g->page) || g->poj_armed);
    o.decode = pipe || sio || fdc || page;
    o.blk = page ? S100_BLK_PAGE : fdc ? S100_BLK_FDC : pipe ? S100_BLK_PIPE : S100_BLK_SIO;
    o.phantom = page && g->ph_en;
    return o;
}

/* The ARM flip-flop across one bus cycle. */
typedef struct {
    bool arm;
} glue_ff_t;

static inline void glue_psync_rise(glue_ff_t *f)
{
    f->arm = true;
}

static inline void glue_wait_clr(glue_ff_t *f)
{
    f->arm = false;
}

static inline bool glue_prdy_low(const glue_ff_t *f, const glue_out_t *o)
{
    return f->arm && o->decode;
}

static inline bool glue_di_oe(const glue_out_t *o, const glue_cycle_t *c, bool pdbin)
{
    return o->decode && pdbin && (c->sinp || c->smemr);
}

#endif /* S100_GLUE_H */
