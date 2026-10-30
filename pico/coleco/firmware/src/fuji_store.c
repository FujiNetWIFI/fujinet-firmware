/* fuji_store.c -- the staging window and the RAM store; see fuji_store.h. */

#include <string.h>

#include "fuji_store.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "colmap.h"

enum tier { TIER_NONE, TIER_STAGE, TIER_RAM };

static uint8_t ram_store[FUJI_STORE_RAM_SIZE];
static enum tier open_tier;
static uint32_t expect;
static uint32_t written;

uint8_t fuji_store_open(uint32_t size)
{
    open_tier = TIER_NONE;
    expect = size;
    written = 0;
    if (size <= COLMAP_WINDOW) {        /* incl. 0: size unknown */
        open_tier = TIER_STAGE;
        return 0;
    }
    if (size > FUJI_STORE_RAM_SIZE)
        return FN_BOOT_ERR_TOOBIG;
    /* A flat image is served from a window, so only the RAM store can be live. */
    if (fuji_live.base == ram_store)
        return FN_BOOT_ERR_STOREBUSY;
    open_tier = TIER_RAM;
    return 0;
}

void fuji_store_write(const uint8_t *chunk, unsigned len)
{
    uint8_t *dst;
    uint32_t cap;

    switch (open_tier) {
    case TIER_STAGE: dst = fuji_cart_stage_window(); cap = COLMAP_WINDOW;       break;
    case TIER_RAM:   dst = ram_store;                cap = FUJI_STORE_RAM_SIZE; break;
    default:         return;
    }
    if (written + len > cap)
        len = (unsigned)(cap - written);
    memcpy(dst + written, chunk, len);
    written += len;
}

const uint8_t *fuji_store_close(bool aborted, uint32_t *kept)
{
    enum tier tier = open_tier;

    open_tier = TIER_NONE;
    if (kept)
        *kept = written;
    if (aborted || tier == TIER_NONE || written == 0)
        return NULL;
    if (expect != 0 && written != expect)
        return NULL;                    /* closed short of the size OPEN gave */
    return (tier == TIER_STAGE) ? fuji_cart_stage_window() : ram_store;
}
