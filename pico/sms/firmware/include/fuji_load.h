/* fuji_load.h -- the load sequence: a staged image into the SRAM, one 8K
 * window at a time, then the hand-over to the loader page.
 *
 * Shared by the cart (fuji_cart.c) and the MAME device, so the soak runs the
 * sequence the cart ships. No SDK in here: the port supplies the LOAD bit.
 */

#ifndef FUJI_LOAD_H
#define FUJI_LOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "sms_cart.h"
#include "smsmap.h"

typedef struct {
    void (*poke)(unsigned offset, uint8_t value);  /* publish an arena byte */
    void (*set_load)(bool on);                     /* the LOAD glue bit */
} fuji_load_port_t;

/* outside the struct, so the C++ build in MAME sees the names */
enum { FUJI_LS_IDLE, FUJI_LS_RUN, FUJI_LS_DONE };

typedef struct {
    const fuji_load_port_t *port;
    uint8_t *window;                  /* FN_LOADWIN_SIZE, served at $8000 */
    const uint8_t **ptab_resident;    /* RESIDENT page table: the window's pages */
    smsmap_t *resident_map;           /* RESIDENT: lut of the window's pages */
    smsmap_t *game_map;               /* filled at the end of a load */
    const sms_bus_t *bus;             /* the BIOS snoop, for the hand-over */

    smsmap_t *volatile next;          /* what the flip at $0000 switches to */
    volatile uint8_t next_mode;

    const uint8_t *staged_base;
    smsmap_plan_t staged_plan;
    bool have_staged, armed;

    int state;                        /* FUJI_LS_*                    */
    const uint8_t *base;
    smsmap_plan_t plan;
    unsigned win, nwin;
    uint8_t seq;
} fuji_load_t;

void fuji_load_init(fuji_load_t *l);
void fuji_load_stage(fuji_load_t *l, const uint8_t *image, const smsmap_plan_t *plan);
void fuji_load_unstage(fuji_load_t *l);            /* a new push supersedes it */
void fuji_load_arm(fuji_load_t *l);                /* BOOTLOCK accepted */
bool fuji_load_busy(const fuji_load_t *l, const uint8_t *base);
void fuji_load_event(fuji_load_t *l, int kind);    /* SMS_W_SWAP / _CONFIG / _GO */
void fuji_load_ack(fuji_load_t *l);                /* FN_REG_SLICE_ACK */
void fuji_load_abort(fuji_load_t *l);              /* the console went away */

#endif /* FUJI_LOAD_H */
