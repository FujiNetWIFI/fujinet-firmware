/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol itself lives in fujimail.c, which the o2em model drives too;
 * this is only the port: where a published byte goes, how a frame reaches the
 * ESP32-S3, and what to do with a committed image.
 *
 * NOT YET RUN ON HARDWARE -- there is no Odyssey 2 or cartridge board yet.
 * fujimail.c has been exercised against a real fujinet-pc-rs232 through the
 * emulator, which is exactly why it is shared rather than reimplemented here.
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

/* Lay a committed image into the spare bank array and arm the swap. core1
 * performs the swap itself, with a single pointer store, on the console's next
 * fetch of the cartridge reset vector -- so nothing is overwritten underneath
 * the client while it is still executing. */
static void port_stream_end(int stream, const uint8_t *data, unsigned len,
                            bool aborted)
{
    o2map_plan_t plan;

    if (aborted || stream != 0 || len == 0)
        return;                 /* the .cfg sibling means nothing to an O2 cart */

    if (o2map_plan(len, &plan) != O2MAP_OK) {
        fuji_cart_poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
        fuji_cart_poke(FN_R_BOOT_ERR, FN_BOOT_ERR_NOMAP);
        return;
    }
    o2map_apply(data, &plan, new_rom_table);
    /* The mailbox lives in the program window a real game needs for its own
     * code. Keep the game, drop the mailbox for the session -- the same call
     * the Intellivision cart makes with cart.MailboxActive. */
    fuji_mailbox_active = plan.mailbox_ok;
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

    while (fuji_cart_next_write(&addr, &data))
        fujimail_write(addr, data);
}
