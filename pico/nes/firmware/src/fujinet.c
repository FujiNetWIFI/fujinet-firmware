/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol itself lives in fujimail.c, which the MAME model drives too;
 * this is only the port: where a published byte goes, how a frame reaches the
 * ESP32-S3, where a pushed image is kept, and the two register writes the
 * loader adds (FN_HOT_SWAP and FN_REG_SLICE_ACK).
 *
 * NOT YET RUN ON HARDWARE -- there is no board yet. fujimail.c is exercised
 * against a real fujinet-pc through the MAME model.
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
#include "nesmap.h"

static uint8_t port_stream_open(int stream, uint32_t size)
{
    uint8_t err;

    if (stream != FN_STREAM_ROM)
        return 0;               /* the .cfg sibling: accepted and dropped */
    err = nesmap_gate(size);
    if (err)
        return err;
    return fuji_store_open(size);
}

static void port_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    if (stream == FN_STREAM_ROM)
        fuji_store_write(chunk, len);
}

static uint8_t port_stream_close(int stream, uint32_t got, bool aborted)
{
    const uint8_t *base;
    nesmap_plan_t plan;

    if (stream != FN_STREAM_ROM)
        return 0;
    base = fuji_store_close(aborted);
    if (aborted || base == NULL)
        return 0;
    if (nesmap_plan(base, got, &plan) != NESMAP_OK)
        return FN_BOOT_ERR_NOMAP;
    fuji_cart_stage(base, &plan);
    return 0;
}

static void port_arm_swap(void)
{
    fuji_boot_armed = true;
}

static void port_bootsel(void)
{
    reset_usb_boot(0, 0);
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

/* One hotspot write, decoded the way channelf_cart.h does: a store to the
 * REGSEL page is a whole register write, expanded into the REGSEL/REGDATA
 * pair the shared decoder expects; a store to the DATA page appends. */
static void mailbox_event(uint16_t offset, uint8_t data)
{
    unsigned page = offset & FN_H_PAGE_MASK;
    unsigned low = offset & 0xFF;

    if (page == FN_H_REGSEL || page == FN_H_REGDATA) {
        if (low == FN_HOT_SWAP) {
            fuji_cart_request_load();
            return;
        }
        if (low == FN_REG_SLICE_ACK) {
            fuji_cart_slice_acked();
            return;
        }
        if (low >= 0x80)
            return;             /* other special ops: undefined here */
        fujimail_read_hotspot((uint16_t)(FN_H_REGSEL + low));
        fujimail_read_hotspot((uint16_t)(FN_H_REGDATA + data));
    } else if (page == FN_H_DATA) {
        fujimail_read_hotspot((uint16_t)(FN_H_DATA + data));
    }
}

void fuji_mailbox_service(void)
{
    nes_event_t ev;

    while (fuji_cart_next_event(&ev)) {
        if (ev.kind == NES_EV_MAPPER) {
            uint32_t irq = save_and_disable_interrupts();
            nesmap_write(&fuji_map, ev.offset, ev.data, ev.cycle);
            if (fuji_map.dirty & NESMAP_DIRTY_IRQ)
                fuji_cart_set_irq(fuji_map.irq_line);
            fuji_map.dirty &= (uint8_t)~NESMAP_DIRTY_IRQ;
            restore_interrupts(irq);
        } else {
            mailbox_event(ev.offset, ev.data);
        }
    }
    fuji_cart_poke(FN_R_LINK, fujibus_link_up() ? 1 : 0);
}
