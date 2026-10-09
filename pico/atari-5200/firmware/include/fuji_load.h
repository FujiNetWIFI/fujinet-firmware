/* fuji_load.h -- staging an image and swapping it in.
 *
 * Two view buffers, ping-ponged: core1 serves one, a push lands in the other
 * (a FujiNet app that pushes is running out of the live one). A committed
 * push is planned and laid out in place, and its view built beside it; the
 * client then arms the swap, and core1 makes it on the read of the stub's
 * last byte. Pure C: the cart, the MAME device and the host tests share it.
 */

#ifndef FUJI_LOAD_H
#define FUJI_LOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "a52map.h"

typedef struct {
    uint8_t *buf[2];                /* A52MAP_VIEW_MAX each                  */
    a52_view_t view[2];
    a52map_plan_t plan[2];
    uint8_t *arena;                 /* FN_ARENA_SIZE                         */
    volatile uint8_t live;          /* the buffer core1 serves; core1's      */
    volatile bool armed;            /* core0 sets it, the swap clears it     */
    volatile bool staged;           /* the other buffer holds a staged image */
    volatile uint8_t swaps;         /* swaps made, mod 256                   */
    bool staged_config;
} fuji_load_t;

/* Serve `config` (a laid-out-able CONFIG image) from buffer 0 with the
 * mailbox live, as at power-on. */
void fuji_load_init(fuji_load_t *l, uint8_t *buf0, uint8_t *buf1, uint8_t *arena,
                    const uint8_t *config, uint32_t config_len);

/* Where the next push goes: the buffer core1 is not serving. Disarms first,
 * so core1 can never swap to a buffer being written. */
uint8_t *fuji_load_target(fuji_load_t *l);

/* The push of `len` bytes into fuji_load_target() is complete: plan it, lay
 * it out and build its view. 0 or a FN_BOOT_ERR_*. */
uint8_t fuji_load_commit(fuji_load_t *l, uint32_t len, const char *cfg_mapper);

/* Stage CONFIG instead (an app going back to it). */
void fuji_load_stage_config(fuji_load_t *l, const uint8_t *config, uint32_t len);

void fuji_load_arm(fuji_load_t *l);

/* core1, on the stub's last read: the staged view if armed, else the live
 * one. Inline: it runs between two console reads. */
A52_HOT a52_view_t *fuji_load_swap(fuji_load_t *l)
{
    if (l->armed && l->staged) {
        l->live = (uint8_t)(l->live ^ 1u);
        l->armed = false;
        l->staged = false;
        l->swaps = (uint8_t)(l->swaps + 1u);
    }
    return &l->view[l->live];
}

A52_HOT a52_view_t *fuji_load_live(fuji_load_t *l)
{
    return &l->view[l->live];
}

#endif /* FUJI_LOAD_H */
