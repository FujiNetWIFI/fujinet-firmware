/* fujiboot.c -- bring the cartridge up serving the client baked into the firmware: at
 * power-up there is no network to load it from. */

#include <string.h>

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fujinet.h"
#include "fuji_mailbox.h"
#include "colmap.h"
#include "fujimail.h"
#include "fujiconfigrom.h"

void fuji_config_boot(void)
{
    colmap_plan_t plan;

    fuji_cart_init();
    memset(fuji_win[0], 0xFF, COLMAP_WINDOW);
    if (colmap_plan(_configrom, FUJI_CONFIGROM_SIZE, COLMAP_KIND_AUTO, &plan)
        == COLMAP_OK)
        colmap_apply(_configrom, &plan, fuji_win[0]);

    /* mkromh.py refuses a client without the claim, so this leaves the mailbox live. */
    fuji_mailbox_active = plan.mailbox_ok;

    fuji_service_init();
    fujimail_paint();
}
