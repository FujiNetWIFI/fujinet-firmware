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

    /* build.sh stamps the claim signature into every client it builds, so
     * the paint below lands in the window the client expects. */
    fuji_mailbox_active = plan.mailbox_ok;

    fuji_service_init();
    fujimail_paint();
}
