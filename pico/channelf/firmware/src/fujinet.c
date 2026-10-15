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

/* A pushed image lands straight in the slot that is not being served, which
 * is big enough for the largest banked client (CHFMAP_STORE), so there is no
 * separate push buffer. There is no flash tier either -- the ColecoVision
 * needed one, and the reasons (128K+ game images, and an XIP miss eating most
 * of a 373 ns budget) apply to neither this console nor this connector. */
static uint32_t rx_len;
static bool rx_over;

static uint8_t port_stream_open(int stream, uint32_t size)
{
    if (stream == FN_STREAM_CFG)
        return 0;               /* accepted and dropped: no mappers to hint */
    if (stream != FN_STREAM_ROM)
        return 0;
    /* The slot is about to be overwritten: whatever was staged there is gone,
     * so a swap must not be able to serve it half-written. */
    fuji_have_staged = false;
    rx_len = 0;
    rx_over = false;
    return chfmap_gate(size);
}

static void port_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    if (stream != FN_STREAM_ROM)
        return;
    if (rx_len + len > CHFMAP_STORE) {
        rx_over = true;         /* the gate should have caught this */
        len = (unsigned)(CHFMAP_STORE - rx_len);
    }
    memcpy(fuji_cart_stage_window() + rx_len, chunk, len);
    rx_len += len;
}

static uint8_t port_stream_close(int stream, uint32_t got, bool aborted)
{
    chfmap_plan_t plan;
    uint8_t *slot = fuji_cart_stage_window();

    (void)got;                  /* rx_len is what actually landed */

    if (stream != FN_STREAM_ROM)
        return 0;
    if (aborted)
        return 0;
    if (rx_over)
        return FN_BOOT_ERR_TOOBIG;
    if (rx_len == 0 || chfmap_plan(slot, rx_len, &plan) != CHFMAP_OK)
        return FN_BOOT_ERR_NOMAP;
    fuji_cart_stage(slot, &plan);
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
