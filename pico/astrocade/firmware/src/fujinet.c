/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol itself lives in fujimail.c, which the MAME model drives too;
 * this is only the port: where a published byte goes, how a frame reaches
 * the ESP32-S3, and what to do with a committed image.
 *
 * NOT YET RUN ON HARDWARE -- there is no Astrocade cartridge board yet.
 * fujimail.c is exercised against a real fujinet-pc through the MAME model,
 * which is exactly why it is shared rather than reimplemented here.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "astromap.h"

/* Lay a committed image into the staging window. The swap itself is
 * core1's, on the client's armed FN_HOT_SWAP read, so nothing is
 * overwritten under the client while it is still executing -- and the
 * mailbox stays live until then, so the BOOT_READY publish that follows
 * this call is seen. */
static void port_stream_end(int stream, const uint8_t *data, unsigned len,
                            bool aborted)
{
    static uint8_t mapped[ASTROMAP_WINDOW];
    astromap_plan_t plan;

    if (aborted || stream != 0 || len == 0)
        return;         /* the .cfg sibling means nothing to this cartridge */

    if (astromap_plan(data, len, &plan) != ASTROMAP_OK) {
        fuji_cart_poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
        fuji_cart_poke(FN_R_BOOT_ERR, FN_BOOT_ERR_NOMAP);
        return;
    }
    astromap_apply(data, &plan, mapped);
    fuji_cart_stage(mapped, plan.mailbox_ok);
}

static void port_arm_swap(void)
{
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
    .arm_swap     = port_arm_swap,
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
    uint16_t offset;

    while (fuji_cart_next_read(&offset))
        fujimail_read_hotspot(offset);
}
