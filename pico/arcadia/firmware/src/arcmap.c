/* arcmap.c -- see arcmap.h. */

#include <string.h>

#include "arcmap.h"
#include "fuji_mailbox.h"

bool arcmap_app_shaped(uint32_t size)
{
    return size > ARCMAP_WINDOW && size <= FN_APP_MAX
        && (size - ARCMAP_WINDOW) % ARCMAP_PAGE == 0;
}

uint8_t arcmap_gate(uint32_t size)
{
    if (size <= ARCMAP_WINDOW || arcmap_app_shaped(size))
        return 0;
    if (size > FN_APP_MAX)
        return FN_BOOT_ERR_TOOBIG;
    return FN_BOOT_ERR_NOMAP;
}

arcmap_err_t arcmap_plan(const uint8_t *image, uint32_t size,
                         arcmap_plan_t *plan)
{
    if (size == 0)
        return ARCMAP_EEMPTY;
    if (size > FN_APP_MAX)
        return ARCMAP_ETOOBIG;
    if (size > ARCMAP_WINDOW && !arcmap_app_shaped(size))
        return ARCMAP_ENOMAP;
    plan->size = size;
    plan->mailbox_ok = size >= FN_R_CLAIM + FN_R_CLAIM_LEN
        && memcmp(image + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
    plan->npages = 0;
    if (size > ARCMAP_WINDOW) {
        /* Without the mailbox nothing could select a page. */
        if (!plan->mailbox_ok)
            return ARCMAP_ENOCLAIM;
        plan->npages = (uint8_t)(1 + (size - ARCMAP_WINDOW) / ARCMAP_PAGE);
    }
    return ARCMAP_OK;
}

void arcmap_apply(const uint8_t *image, const arcmap_plan_t *plan,
                  uint8_t *store)
{
    if (image != store)
        memmove(store, image, plan->size);
    if (plan->size < ARCMAP_WINDOW)
        memset(store + plan->size, 0xff, ARCMAP_WINDOW - plan->size);
}

void arcmap_carry_status(uint8_t *staged, const uint8_t *live)
{
    memcpy(staged + FN_R_DATA, live + FN_R_DATA, FN_R_PAINT_END - FN_R_DATA);
}
