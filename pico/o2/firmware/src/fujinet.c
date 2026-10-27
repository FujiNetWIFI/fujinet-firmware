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

extern unsigned char new_rom_table[8][4096];
extern volatile bool fuji_boot_armed;

/* core1 swaps to the staged image with one pointer store at the console's next
 * reset-vector fetch, so the running client is never overwritten. */
static void port_stream_end(int stream, const uint8_t *data, unsigned len,
                            bool aborted)
{
    o2map_plan_t plan;

    if (aborted || stream != 0 || len == 0)
        return;                 /* the .cfg sibling means nothing to an O2 cart */

    if (o2map_plan(data, len, &plan) != O2MAP_OK) {
        fuji_cart_poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
        fuji_cart_poke(FN_R_BOOT_ERR, FN_BOOT_ERR_NOMAP);
        return;
    }

    if (plan.appbank) {
        /* Protocol 2: up to 63K, served from the stage buffer in place; there
         * is no RAM for a second copy. The fixed half is written through
         * FN_REG_POKE. */
        o2app_init(&fuji_app, (uint8_t *) data, plan.npages);
        fujimail_stage_live = true;
        fuji_app_armed = true;
        fuji_mailbox_active = true;
        fuji_cart_stage_boot();
        __sync_synchronize();   /* fuji_app is whole before core1 can see armed */
        fuji_boot_armed = true;
        fujimail_paint();
        return;
    }

    o2map_apply(data, &plan, new_rom_table);
    /* A game needs the mailbox page for its own code, so the mailbox is off
     * for the session unless the image claims it. */
    fuji_mailbox_active = plan.mailbox_ok;
    fuji_cart_stage_boot();
    fuji_boot_armed = true;

    /* o2map_apply overwrote the staged mailbox page; republish it before the
     * console fetches $400. */
    if (fuji_mailbox_active)
        fujimail_paint();
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

    while (fuji_cart_next_write(&addr, &data))
        fujimail_write(addr, data);
}
