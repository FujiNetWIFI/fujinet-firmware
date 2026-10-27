/* fujinet.c -- the cartridge's port for the shared mailbox protocol. Not yet run on hardware.
 * An image is at most 70K, so a push lands straight in the unserved slot and is staged in place.
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

/* Stream 0, the ROM image; the .cfg sibling is ignored. */
static uint32_t rxlen;

static uint8_t port_rom_open(uint32_t size)
{
    uint8_t err = arcmap_gate(size);

    rxlen = 0;
    if (err == 0)
        fuji_have_staged = false;   /* a half-written slot can never swap */
    return err;
}

static void port_rom_write(const uint8_t *chunk, unsigned len)
{
    if (rxlen + len > ARCMAP_STORE)
        len = ARCMAP_STORE - rxlen;
    memcpy(fuji_cart_stage_slot() + rxlen, chunk, len);
    rxlen += len;
}

static uint8_t port_rom_close(uint32_t got, bool aborted, const char *mapper)
{
    arcmap_plan_t plan;

    (void) mapper;              /* the .cfg sibling means nothing here */
    if (aborted)
        return 0;
    /* Plan on got, not rxlen, so an oversized image OPEN gave no size for is refused. */
    switch (arcmap_plan(fuji_cart_stage_slot(), got, &plan)) {
    case ARCMAP_OK:
        break;
    case ARCMAP_ETOOBIG:
        return FN_BOOT_ERR_TOOBIG;
    case ARCMAP_ENOMAP:
    case ARCMAP_ENOCLAIM:
        return FN_BOOT_ERR_NOMAP;
    default:
        return FN_BOOT_ERR_TRUNCATED;
    }
    fuji_cart_stage(fuji_cart_stage_slot(), &plan);
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
