#include <string.h>

#include "chfmap.h"

bool chfmap_app_shaped(uint32_t size)
{
    uint32_t extra;

    if (size <= CHFMAP_WINDOW)
        return false;
    extra = size - CHFMAP_WINDOW;
    return extra % FN_APP_PAGE == 0 && extra / FN_APP_PAGE <= FN_APP_MAX_PAGES;
}

uint8_t chfmap_gate(uint32_t size)
{
    if (size > CHFMAP_WINDOW && !chfmap_app_shaped(size))
        return FN_BOOT_ERR_TOOBIG;
    return 0;
}

bool chfmap_claims(const uint8_t *image, uint32_t size)
{
    if (size != CHFMAP_WINDOW && !chfmap_app_shaped(size))
        return false;
    return memcmp(image + FN_ROM_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
}

chfmap_err_t chfmap_plan(const uint8_t *image, uint32_t size,
                         chfmap_plan_t *plan)
{
    if (size == 0)
        return CHFMAP_EEMPTY;
    if (size > CHFMAP_WINDOW && !chfmap_app_shaped(size))
        return CHFMAP_ETOOBIG;
    /* Without $55 the BIOS runs its built-in game, which looks like a dead
     * cart rather than a failed boot. */
    if (image[0] != 0x55)
        return CHFMAP_ENOSIG;

    plan->size = size;
    plan->mailbox_ok = chfmap_claims(image, size);
    plan->npages = (uint8_t)(size > CHFMAP_WINDOW
                             ? (size - CHFMAP_WINDOW) / FN_APP_PAGE : 0);
    /* Pages are selected through the mailbox, which a claimless image kills. */
    if (plan->npages && !plan->mailbox_ok)
        return CHFMAP_ENOCLAIM;
    return CHFMAP_OK;
}

void chfmap_apply(const uint8_t *image, const chfmap_plan_t *plan,
                  uint8_t store[CHFMAP_STORE])
{
    if (image != store)
        memmove(store, image, plan->size);
    if (plan->size < CHFMAP_WINDOW)
        memset(store + plan->size, 0xFF, CHFMAP_WINDOW - plan->size);
}
