/* fujiboot.c -- bring the cartridge up as FujiNet.
 *
 * The console client is baked into this firmware, because at power-up there is
 * no network and nothing to load it from. There is no menu and no fallback;
 * the client is the only boot ROM (the O2/Intellivision rule).
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
    memset(fuji_win[0], 0xFF, CHFMAP_WINDOW);
    if (chfmap_plan(_configrom, FUJI_CONFIGROM_SIZE, &plan) == CHFMAP_OK)
        chfmap_apply(_configrom, &plan, fuji_win[0]);
    else
        plan.mailbox_ok = false;

    /* build.sh stamps the claim into every client it builds, so the paint
     * below lands in the arena the client is already watching. */
    fuji_mailbox_active = plan.mailbox_ok;

    fuji_service_init();
    fujimail_paint();
}
