/* fujinet.c -- the cartridge's port for the mailbox protocol in fujimail.c,
 * which the o2em model shares. Not yet run on hardware.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "o2map.h"

/* core1 swaps to the staged image with one pointer store at the console's next
 * reset-vector fetch (fuji_cart_swap), so the running client is never
 * overwritten and keeps its status page until then: the reply to the MOUNT_IMAGE
 * that staged the image is for it. */
static void port_stream_end(int stream, const uint8_t *data, unsigned len,
                            bool aborted)
{
    o2map_plan_t plan;
    unsigned char (*table)[4096];

    if (aborted || stream != 0 || len == 0)
        return;                 /* the .cfg sibling means nothing to an O2 cart */

    if (o2map_plan(data, len, &plan) != O2MAP_OK) {
        fuji_cart_poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
        fuji_cart_poke(FN_R_BOOT_ERR, FN_BOOT_ERR_NOMAP);
        return;
    }

    table = fuji_cart_stage_table();

    if (plan.appbank) {
        /* Protocol 2: up to 63K, served from the stage buffer in place; there
         * is no RAM for a second copy. The fixed half is written through
         * FN_REG_POKE. The table only carries its status bytes. */
        o2app_init(&fuji_app_next, (uint8_t *) data, plan.npages);
        fujimail_stage_live = true;
        fuji_cart_stage_boot(table, true);
        fuji_app_armed = true;
        __sync_synchronize();   /* fuji_app_next is whole before core1 can see armed */
        fuji_boot_armed = true;
        return;
    }

    o2map_apply(data, &plan, table);
    /* A game needs the mailbox page for its own code, so the mailbox is off
     * once it runs unless the image claims it. */
    fuji_cart_stage_boot(table, plan.mailbox_ok);
    fuji_app_armed = false;
    __sync_synchronize();
    fuji_boot_armed = true;
}

static void port_bootsel(void)
{
    reset_usb_boot(0, 0);       /* noreturn; there is no ack to poll */
}

static const fujimail_port_t cart_port = {
    .poke         = fuji_cart_poke,
    .link_up      = fujibus_link_up,
    .transact     = fujibus_transact,
    .send_bare    = fujibus_send_bare,
    .stream_end   = port_stream_end,
    .wait_link_ms = fuji_wait_ms_pumped,
    .bootsel      = port_bootsel,
    .on_txn       = NULL,
    .on_dbc       = NULL,
};

void fuji_service_init(void)
{
    fujimail_init(&cart_port);
    fujibus_set_inbound_handler(fujimail_inbound);
}

void fuji_mailbox_service(void)
{
    uint8_t addr, data;

    while (fuji_cart_next_write(&addr, &data)) {
        /* A running game that did not claim the mailbox may write these
         * addresses for its own reasons; none of it is ours to act on. */
        if (fuji_mailbox_active)
            fujimail_write(addr, data);
    }
}
