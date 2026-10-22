/* fujiboot.c -- bring the cartridge up serving the client baked into this firmware: at
 * power-on there is no network to load one from.
 */

#include <string.h>

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fujinet.h"
#include "fuji_mailbox.h"
#include "tmplmap.h"
#include "fujimail.h"
#include "fujiconfigrom.h"

void fuji_config_boot(void)
{
    tmplmap_plan_t plan;

    fuji_cart_init();
    memset(fuji_window, 0xFF, sizeof fuji_window);
    if (tmplmap_plan(_configrom, FUJI_CONFIGROM_SIZE, &plan) == TMPLMAP_OK)
        tmplmap_apply(_configrom, &plan, fuji_window);
    else
        plan.mailbox_ok = false;

    /* a client that claims the mailbox finds it painted */
    fuji_mailbox_active = plan.mailbox_ok;

    fuji_service_init();
    fujimail_paint();
}
