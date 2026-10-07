/* fuji_load.h -- the load sequence: a staged image into the SRAM, one 1K
 * slice at a time through the loader, then the choice of hand-over.
 *
 * Shared by the cart (fuji_cart.c) and the MAME device, so the soak runs the
 * sequence the cart ships. No SDK in here: the port paints the reply window
 * and points the slot-2 window at the page being filled.
 *
 * What goes into the SRAM, in order: the image's pages (the $FF it is
 * top-aligned behind included), the $FF page, cart RAM cleared to 0, the
 * HSC's RAM (behind a game with the HSC on, or for the HSC itself) and, behind
 * a game, the HSC's ROM.
 */

#ifndef FUJI_LOAD_H
#define FUJI_LOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "a78_cart.h"
#include "a78map.h"

typedef struct {
    void (*poke)(unsigned offset, uint8_t value);    /* an arena status byte */
    void (*paint)(const uint8_t *src, uint8_t fill); /* the 1K reply window: src, or fill if NULL */
    void (*window)(uint16_t slot_word);              /* slot 2 while loading */
} fuji_load_port_t;

/* outside the struct, so the C++ build in MAME sees the names */
enum { FUJI_LS_IDLE, FUJI_LS_RUN, FUJI_LS_DONE };

typedef struct {
    const fuji_load_port_t *port;
    const a78_bus_t *bus;             /* the INPTCTRL model, for the hand-over */

    /* CONFIG, always loadable */
    const uint8_t *config_base;
    a78map_plan_t config_plan;

    /* the pushed image */
    const uint8_t *staged_base;
    a78map_plan_t staged_plan;
    bool have_staged, armed;

    /* the High Score Cart */
    const uint8_t *hsc_rom;           /* 4K, or NULL                          */
    const uint8_t *hsc_ram;           /* 2K: the shadow                       */
    bool hsc_on;

    uint8_t tv;                       /* FN_TV_*                              */

    /* what GO switches to */
    a78map_t next;
    volatile uint8_t next_mode;
    uint8_t next_pokey;
    bool next_hsc;
    uint8_t handover;                 /* FN_HO_* chosen at the end of the load */

    /* the load in progress */
    int state;                        /* FUJI_LS_*                            */
    const uint8_t *base;
    a78map_plan_t plan;
    bool hsc;                         /* this load includes the HSC           */
    unsigned slice, nslices;
    uint8_t seq;
} fuji_load_t;

void fuji_load_init(fuji_load_t *l, const uint8_t *config, const a78map_plan_t *config_plan);
void fuji_load_stage(fuji_load_t *l, const uint8_t *image, const a78map_plan_t *plan);
void fuji_load_unstage(fuji_load_t *l);            /* a new push supersedes it */
void fuji_load_arm(fuji_load_t *l);                /* BOOTLOCK accepted */
bool fuji_load_busy(const fuji_load_t *l, const uint8_t *base);
void fuji_load_event(fuji_load_t *l, int kind);    /* A78_W_SWAP / _CONFIG / _GO / _GO_BIOS */
void fuji_load_ack(fuji_load_t *l);                /* FN_REG_SLICE_ACK */
void fuji_load_abort(fuji_load_t *l);              /* the console went away */

/* The hand-over rule: the console's own BIOS for a game it would start
 * itself on an unlocked console; the loader for everything else. */
uint8_t fuji_load_handover(const a78map_plan_t *plan, uint8_t tv, bool locked);

/* Slice n of a load: which SRAM page and 1K within it, and the source. */
typedef struct {
    uint8_t page, k;                  /* SRAM page, 1K index 0-7              */
    const uint8_t *src;               /* or NULL: fill                        */
    uint8_t fill;
} fuji_slice_t;

unsigned fuji_load_slices(const a78map_plan_t *plan, bool hsc);
void fuji_load_slice(const fuji_load_t *l, unsigned n, fuji_slice_t *s, uint8_t *scratch);

#endif /* FUJI_LOAD_H */
