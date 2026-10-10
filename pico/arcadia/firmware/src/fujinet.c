/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol itself lives in fujimail.c, which the MAME model drives too;
 * this is only the port: where a published byte goes, how a frame reaches
 * the ESP32-S3, and what to do with a committed image.
 *
 * There is no tiered store as on the Astrocade: the whole image is at most
 * 8K, so a push stream lands in one buffer and is staged from there.
 *
 * NOT YET RUN ON HARDWARE -- there is no Arcadia cartridge board yet.
 * fujimail.c is exercised against a real fujinet-pc through the MAME model,
 * which is why it is shared rather than reimplemented here.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "arcmap.h"

/* One push stream (stream 0, the ROM image) at a time; the .cfg sibling is
 * ignored. The staged image is built for core1's armed FN_HOT_SWAP, so
 * nothing is overwritten under the client while it still executes. */
static uint8_t rxbuf[ARCMAP_WINDOW];
static uint32_t rxlen;

static uint8_t port_stream_open(int stream, uint32_t size)
{
    if (stream != 0)
        return 0;               /* the .cfg sibling means nothing here */
    rxlen = 0;
    return arcmap_gate(size);
}

static void port_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    if (stream != 0)
        return;
    if (rxlen + len > sizeof rxbuf)
        len = sizeof rxbuf - rxlen;
    memcpy(rxbuf + rxlen, chunk, len);
    rxlen += len;
}

static uint8_t port_stream_close(int stream, uint32_t got, bool aborted)
{
    arcmap_plan_t plan;

    if (stream != 0)
        return 0;
    if (aborted)
        return 0;
    (void) got;                 /* rxlen is what actually landed */
    switch (arcmap_plan(rxbuf, rxlen, &plan)) {
    case ARCMAP_OK:
        break;
    case ARCMAP_ETOOBIG:
        return FN_BOOT_ERR_TOOBIG;
    default:
        return FN_BOOT_ERR_TRUNCATED;
    }
    fuji_cart_stage(rxbuf, &plan);
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
