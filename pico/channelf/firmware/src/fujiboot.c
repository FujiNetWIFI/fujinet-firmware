/* fujiboot.c -- bring the cartridge up as FujiNet, serving the baked-in
 * console client; there is no network to load it from at power-up.
 */

#include <string.h>

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fujinet.h"
#include "fuji_mailbox.h"
#include "chfmap.h"
#include "fujimail.h"
#include "fujiconfigrom.h"

void fuji_config_boot(void)
{
    chfmap_plan_t plan;

    fuji_cart_init();
    memset(fuji_store[0], 0xFF, CHFMAP_WINDOW);
    if (chfmap_plan(_configrom, FUJI_CONFIGROM_SIZE, &plan) == CHFMAP_OK) {
        chfmap_apply(_configrom, &plan, fuji_store[0]);
        fuji_slot_npages[0] = plan.npages;
    } else
        plan.mailbox_ok = false;

    fuji_mailbox_active = plan.mailbox_ok;

    fuji_service_init();
    fujimail_paint();
}
