/* arcmap.c -- see arcmap.h. */

#include <string.h>

#include "arcmap.h"
#include "fuji_mailbox.h"

uint8_t arcmap_gate(uint32_t size)
{
    if (size > ARCMAP_WINDOW)
        return FN_BOOT_ERR_TOOBIG;
    return 0;
}

arcmap_err_t arcmap_plan(const uint8_t *image, uint32_t size,
                         arcmap_plan_t *plan)
{
    if (size == 0)
        return ARCMAP_EEMPTY;
    if (size > ARCMAP_WINDOW)
        return ARCMAP_ETOOBIG;
    plan->size = size;
    plan->mailbox_ok = size >= FN_R_CLAIM + FN_R_CLAIM_LEN
        && memcmp(image + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
    return ARCMAP_OK;
}

void arcmap_apply(const uint8_t *image, const arcmap_plan_t *plan,
                  uint8_t window[ARCMAP_WINDOW])
{
    memcpy(window, image, plan->size);
    memset(window + plan->size, 0xff, ARCMAP_WINDOW - plan->size);
}
