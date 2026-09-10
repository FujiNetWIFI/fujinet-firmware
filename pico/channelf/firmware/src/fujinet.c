/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol itself lives in fujimail.c, which the MAME model drives too;
 * this is only the port: where a published byte goes, how a frame reaches the
 * ESP32-S3, and what to do with a committed image.
 *
 * NOT YET RUN ON HARDWARE -- there is no Channel F Videocart board yet.
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
#include "chfmap.h"

/* Every Videocart ever made is 6K or less and the window is 16K, so a pushed
 * image is buffered whole with room to spare. There is no image store and no
 * flash tier here -- the ColecoVision needed both, and the reasons (banked
 * 128K+ images, and an XIP miss eating most of a 373 ns budget) apply to
 * neither this console nor this connector. */
static uint8_t rx_image[CHFMAP_WINDOW];
static uint32_t rx_len;
static bool rx_over;

static uint8_t port_stream_open(int stream, uint32_t size)
{
    if (stream == FN_STREAM_CFG)
        return 0;               /* accepted and dropped: no mappers to hint */
    if (stream != FN_STREAM_ROM)
        return 0;
    rx_len = 0;
    rx_over = false;
    return chfmap_gate(size);
}

static void port_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    if (stream != FN_STREAM_ROM)
        return;
    if (rx_len + len > sizeof rx_image) {
        rx_over = true;         /* the gate should have caught this */
        len = (unsigned)(sizeof rx_image - rx_len);
    }
    memcpy(rx_image + rx_len, chunk, len);
    rx_len += len;
}

static uint8_t port_stream_close(int stream, uint32_t got, bool aborted)
{
    chfmap_plan_t plan;

    (void)got;                  /* rx_len is what actually landed */

    if (stream != FN_STREAM_ROM)
        return 0;
    if (aborted)
        return 0;
    if (rx_over)
        return FN_BOOT_ERR_TOOBIG;
    if (rx_len == 0 || chfmap_plan(rx_image, rx_len, &plan) != CHFMAP_OK)
        return FN_BOOT_ERR_NOMAP;
    fuji_cart_stage(rx_image, &plan);
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

    while (fuji_cart_next_event(&offset))
        fujimail_read_hotspot(offset);
}
