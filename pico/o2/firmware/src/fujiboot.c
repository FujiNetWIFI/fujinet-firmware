/* fujiboot.c -- boot the baked-in console client instead of PicoPAC's menu;
 * at power-up there is no network to load it from.
 */

#include <string.h>

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fujinet.h"
#include "fujibus_usb.h"
#include "o2map.h"
#include "fujiconfigrom.h"

/* An APPBANK client is served from this copy in RAM: core1 must answer /PSEN in
 * about a microsecond, which the flash the image is baked into cannot promise,
 * and its fixed half takes pokes. It is not the stage buffer, so a client that
 * pushes games (the CONFIG) can. A legacy client needs no copy; it is laid out in
 * rom_table[]. */
static uint8_t config_app[FUJI_CONFIGROM_SIZE];

#if PICO_RP2040
/* 1K + 15 x 2K: more would leave the PicoPAC's 264K too little for the stack. */
_Static_assert(FUJI_CONFIGROM_SIZE <= O2_APP_FIXED_BYTES + 15 * O2_APP_PAGE_BYTES,
               "the baked CONFIG does not fit the RP2040's RAM");
#endif

void fuji_config_boot(void)
{
    o2map_plan_t plan;

    memset(rom_table, 0, sizeof rom_table);

    if (o2map_plan(_configrom, FUJI_CONFIGROM_SIZE, &plan) == O2MAP_OK) {
        if (plan.appbank) {
            memcpy(config_app, _configrom, FUJI_CONFIGROM_SIZE);
            o2app_init(&fuji_app, config_app, plan.npages);
            /* fujimail_stage_live stays false: this client pushes games. The
             * status page still lives in rom_table[0], as for a pushed app. */
            __sync_synchronize();
            fuji_app_live = true;
        } else {
            /* A 3K single-bank image lands in every bank, whatever P10/P11
             * select. */
            o2map_apply(_configrom, &plan, rom_table);
        }
    }

    /* The client owns only $F00-$F1F (to FN_R_STUB_END); painting from $F20
     * up leaves it intact. */
    fuji_mailbox_active = true;
    fuji_cart_init();

    fuji_service_init();
}
