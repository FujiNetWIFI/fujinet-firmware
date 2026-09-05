/* fujiboot.c -- bring the cartridge up as FujiNet.
 *
 * The console client is baked into this firmware, because at power-up there
 * is no network and nothing to load it from. There is no menu and no
 * fallback; the client is the only boot ROM (the O2/Intellivision rule).
 */

#include <string.h>

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fujinet.h"
#include "fuji_mailbox.h"
#include "astromap.h"
#include "fujimail.h"
#include "fujiconfigrom.h"

void fuji_config_boot(void)
{
    astromap_plan_t plan;

    fuji_cart_init();
    memset(fuji_window, 0xFF, sizeof fuji_window);
    if (astromap_plan(_configrom, FUJI_CONFIGROM_SIZE, &plan) == ASTROMAP_OK)
        astromap_apply(_configrom, &plan, fuji_window);

    /* build.sh stamps the claim signature into every client it builds, so
     * the paint below lands in the window the client expects. */
    fuji_mailbox_active = plan.mailbox_ok;

    fuji_service_init();
    fujimail_paint();
}
