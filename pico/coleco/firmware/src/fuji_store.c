/* fuji_store.c -- the two-tier image store. See fuji_store.h for why there is
 * no flash tier on this port. */

#include <string.h>

#include "fuji_store.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "colmap.h"

enum tier { TIER_NONE, TIER_STAGE, TIER_RAM };

/* Images up to the window go straight into fuji_staged, which fuji_cart owns
 * and which is a full window already -- a second 32K buffer would be pure
 * waste on a 264K part. */
static uint8_t ram_store[FUJI_STORE_RAM_SIZE];

static enum tier tier;
static uint32_t expect;
static uint32_t written;

/* Which store is the console being served out of right now? Anything flat is
 * served from the painted window, not from a store, so only the RAM tier can
 * ever be live. */
static enum tier live_tier(void)
{
    if (fuji_live.base == ram_store)
        return TIER_RAM;
    return TIER_NONE;
}

uint8_t fuji_store_open(uint32_t size)
{
    tier = TIER_NONE;
    expect = size;
    written = 0;

    if (size <= COLMAP_WINDOW) {        /* incl. 0: size unknown */
        tier = TIER_STAGE;
        return 0;
    }
    if (size <= FUJI_STORE_RAM_SIZE) {
        if (live_tier() == TIER_RAM)
            return FN_BOOT_ERR_STOREBUSY;
        tier = TIER_RAM;
        return 0;
    }
    return FN_BOOT_ERR_TOOBIG;
}

void fuji_store_write(const uint8_t *chunk, unsigned len)
{
    uint8_t *dst;
    uint32_t cap;

    switch (tier) {
    case TIER_STAGE: dst = fuji_cart_stage_window(); cap = COLMAP_WINDOW; break;
    case TIER_RAM:   dst = ram_store;   cap = FUJI_STORE_RAM_SIZE;  break;
    default:         return;
    }
    if (written + len > cap)
        len = (unsigned)(cap - written);
    memcpy(dst + written, chunk, len);
    written += len;
}

const uint8_t *fuji_store_close(bool aborted)
{
    enum tier t = tier;

    tier = TIER_NONE;
    if (aborted || t == TIER_NONE || written == 0)
        return NULL;
    if (expect != 0 && written != expect)
        return NULL;
    return (t == TIER_STAGE) ? fuji_cart_stage_window() : ram_store;
}
