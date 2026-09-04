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
#include "fuji_store.h"
#include "fuji_mailbox.h"
#include "astromap.h"

/* The push stream's bytes land in the store; a committed image is staged
 * for core1's armed FN_HOT_SWAP swap, so nothing is overwritten under the
 * client while it is still executing -- and the mailbox stays live until
 * then, so the BOOT_READY publish that follows the close is seen. */
static uint8_t port_stream_open(int stream, uint32_t size)
{
    uint8_t err;

    if (stream != 0)
        return 0;       /* the .cfg sibling means nothing to this cartridge */
    err = astromap_gate(size);
    if (err != 0)
        return err;
    return fuji_store_open(size);
}

static void port_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    if (stream == 0)
        fuji_store_write(chunk, len);
}

static uint8_t port_stream_close(int stream, uint32_t got, bool aborted)
{
    const uint8_t *image;
    astromap_plan_t plan;

    if (stream != 0)
        return 0;
    image = fuji_store_close(aborted);
    if (aborted)
        return 0;
    if (image == NULL || got == 0
        || astromap_plan(image, got, &plan) != ASTROMAP_OK)
        return FN_BOOT_ERR_NOMAP;
    fuji_cart_stage(image, &plan);
    return 0;
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
    .stream_open  = port_stream_open,
    .stream_write = port_stream_write,
    .stream_close = port_stream_close,
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
