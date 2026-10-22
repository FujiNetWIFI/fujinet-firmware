/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol lives in fujimail.c, which the MAME model drives too; this is
 * the port (fuji_port.c binds it to the USB link): where a published byte goes,
 * where a pushed image is kept, and the loader's acks.
 *
 * NOT YET RUN ON HARDWARE -- there is no board yet.
 */

#include "pico/stdlib.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_port.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_store.h"
#include "fuji_mailbox.h"
#include "smsmap.h"

static uint8_t port_rom_open(uint32_t size)
{
    uint8_t err = smsmap_gate(size);

    if (err)
        return err;
    fuji_load_unstage(&fuji_loader);       /* free its store for this one */
    return fuji_store_open(size);
}

static uint8_t port_rom_close(uint32_t got, bool aborted, const char *mapper)
{
    const uint8_t *base;
    smsmap_plan_t plan;
    int err;

    base = fuji_store_close(aborted);
    if (aborted || base == NULL)
        return 0;
    err = smsmap_plan(base, got, mapper, &plan);
    if (err == SMSMAP_ETOOBIG)
        return FN_BOOT_ERR_TOOBIG;
    if (err != SMSMAP_OK)
        return FN_BOOT_ERR_NOMAP;
    fuji_load_stage(&fuji_loader, base, &plan);
    return 0;
}

static void port_arm_swap(void)
{
    fuji_load_arm(&fuji_loader);
}

static void own_register(uint8_t reg, uint8_t data)
{
    (void) data;
    if (reg == FN_REG_SLICE_ACK)
        fuji_load_ack(&fuji_loader);
}

static const fuji_port_ops_t cart_port = {
    .poke      = fuji_cart_poke,
    .rom_open  = port_rom_open,
    .rom_write = fuji_store_write,
    .rom_close = port_rom_close,
    .arm_swap  = port_arm_swap,
    .own_reg   = own_register,
};

void fuji_service_init(void)
{
    fuji_port_init(&cart_port);
}

/* A store to the REGSEL page is a whole register write, expanded into the
 * REGSEL/REGDATA pair the shared decoder expects; a store to DATA appends. */
static void mailbox_event(uint16_t offset, uint8_t data)
{
    unsigned page = offset & FN_H_PAGE_MASK;
    unsigned low = offset & 0xFF;

    if (page == FN_H_REGSEL || page == FN_H_REGDATA) {
        if (low >= 0x80)
            return;
        fujimail_read_hotspot((uint16_t)(FN_H_REGSEL + low));
        fujimail_read_hotspot((uint16_t)(FN_H_REGDATA + data));
    } else if (page == FN_H_DATA) {
        fujimail_read_hotspot((uint16_t)(FN_H_DATA + data));
    }
}

void fuji_mailbox_service(void)
{
    sms_event_t ev;

    while (fuji_cart_next_event(&ev)) {
        if (ev.kind == SMS_W_MAILBOX)
            mailbox_event(ev.offset, ev.data);
        else
            fuji_load_event(&fuji_loader, ev.kind);
    }
    fuji_cart_poke(FN_R_LINK, fujibus_link_up() ? 1 : 0);
}
