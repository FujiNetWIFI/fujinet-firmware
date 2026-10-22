/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol itself lives in fujimail.c, which the MAME model drives too,
 * and fuji_port.c binds it to the USB link; this is only the rest of the port:
 * where a published byte goes, and what to do with a committed image.
 *
 * NOT YET RUN ON HARDWARE -- there is no Channel F Videocart board yet.
 * fujimail.c is exercised against a real fujinet-pc through the MAME model,
 * which is exactly why it is shared rather than reimplemented here.
 */

#include <string.h>

#include "pico/stdlib.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_port.h"
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

static uint8_t port_rom_open(uint32_t size)
{
    /* The slot is about to be overwritten: whatever was staged there is gone,
     * so a swap must not be able to serve it half-written. */
    fuji_have_staged = false;
    rx_len = 0;
    rx_over = false;
    return chfmap_gate(size);
}

static void port_rom_write(const uint8_t *chunk, unsigned len)
{
    if (rx_len + len > CHFMAP_STORE) {
        rx_over = true;         /* the gate should have caught this */
        len = (unsigned)(CHFMAP_STORE - rx_len);
    }
    memcpy(fuji_cart_stage_window() + rx_len, chunk, len);
    rx_len += len;
}

static uint8_t port_rom_close(uint32_t got, bool aborted, const char *mapper)
{
    chfmap_plan_t plan;
    uint8_t *slot = fuji_cart_stage_window();

    (void)got;                  /* rx_len is what actually landed */
    (void)mapper;               /* no mappers to hint */

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

static const fuji_port_ops_t cart_port = {
    .poke      = fuji_cart_poke,
    .rom_open  = port_rom_open,
    .rom_write = port_rom_write,
    .rom_close = port_rom_close,
    .arm_swap  = port_arm_swap,
};

void fuji_service_init(void)
{
    fuji_port_init(&cart_port);
}

void fuji_mailbox_service(void)
{
    uint16_t offset;

    while (fuji_cart_next_event(&offset))
        fujimail_read_hotspot(offset);
}
