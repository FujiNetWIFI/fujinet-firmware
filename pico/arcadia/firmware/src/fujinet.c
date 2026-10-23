/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol itself lives in fujimail.c, which the MAME model drives too,
 * and fuji_port.c binds it to the USB link; this is only the rest of the
 * port: where a published byte goes, and what to do with a committed image.
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

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_port.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "arcmap.h"

/* One push stream (stream 0, the ROM image) at a time; the .cfg sibling is
 * ignored. The staged image is built for core1's armed FN_HOT_SWAP, so
 * nothing is overwritten under the client while it still executes. */
static uint8_t rxbuf[ARCMAP_WINDOW];
static uint32_t rxlen;

static uint8_t port_rom_open(uint32_t size)
{
    rxlen = 0;
    return arcmap_gate(size);
}

static void port_rom_write(const uint8_t *chunk, unsigned len)
{
    if (rxlen + len > sizeof rxbuf)
        len = sizeof rxbuf - rxlen;
    memcpy(rxbuf + rxlen, chunk, len);
    rxlen += len;
}

static uint8_t port_rom_close(uint32_t got, bool aborted, const char *mapper)
{
    arcmap_plan_t plan;

    (void) mapper;              /* the .cfg sibling means nothing here */
    if (aborted)
        return 0;
    /* got counts every byte pushed, rxlen only what fit: planned on got, an image OPEN
     * gave no size for is refused here rather than staged cut short */
    switch (arcmap_plan(rxbuf, got, &plan)) {
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

    while (fuji_cart_next_read(&offset))
        fujimail_read_hotspot(offset);
}
