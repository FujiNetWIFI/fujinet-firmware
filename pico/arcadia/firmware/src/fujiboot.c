/* fujiboot.c -- boot the cartridge serving the baked-in client, the only boot ROM. */

#include <string.h>

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fujinet.h"
#include "fuji_mailbox.h"
#include "arcmap.h"
#include "fujimail.h"
#include "fujiconfigrom.h"

void fuji_config_boot(void)
{
    arcmap_plan_t plan;

    fuji_cart_init();
    memset(fuji_slot[0], 0xFF, ARCMAP_WINDOW);
    plan.mailbox_ok = false;
    plan.npages = 0;
    if (arcmap_plan(_configrom, FUJI_CONFIGROM_SIZE, &plan) == ARCMAP_OK)
        arcmap_apply(_configrom, &plan, fuji_slot[0]);
    fuji_slot_npages[0] = plan.npages;
    arcapp_reset(&fuji_app, fuji_slot[0], plan.npages, fuji_ffpage);

    fuji_mailbox_active = plan.mailbox_ok;

    fuji_service_init();
    fujimail_paint();
}
