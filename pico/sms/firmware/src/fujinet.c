/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol lives in fujimail.c, which the MAME model drives too; this is
 * the port: where a published byte goes, how a frame reaches the ESP32-S3,
 * where a pushed image is kept, and the loader's acks.
 *
 * NOT YET RUN ON HARDWARE -- there is no board yet.
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
#include "smsmap.h"

/* The .cfg sibling arrives first; its `mapper=` applies to the next image. */
static char cfg_text[256];
static unsigned cfg_len;
static char cfg_mapper[16];

static void parse_cfg(void)
{
    const char *p = cfg_text;

    cfg_mapper[0] = '\0';
    while (p && *p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (strncmp(p, "mapper=", 7) == 0) {
            unsigned n = 0;

            p += 7;
            while (*p && *p != '\r' && *p != '\n' && *p != ' ' && n < sizeof cfg_mapper - 1)
                cfg_mapper[n++] = *p++;
            cfg_mapper[n] = '\0';
            return;
        }
        p = strchr(p, '\n');
    }
}

static uint8_t port_stream_open(int stream, uint32_t size)
{
    uint8_t err;

    if (stream != FN_STREAM_ROM) {
        cfg_len = 0;
        cfg_text[0] = '\0';
        return 0;
    }
    err = smsmap_gate(size);
    if (err)
        return err;
    fuji_load_unstage(&fuji_loader);       /* free its store for this one */
    return fuji_store_open(size);
}

static void port_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    if (stream == FN_STREAM_ROM) {
        fuji_store_write(chunk, len);
        return;
    }
    if (cfg_len + len >= sizeof cfg_text)
        len = sizeof cfg_text - 1 - cfg_len;
    memcpy(cfg_text + cfg_len, chunk, len);
    cfg_len += len;
    cfg_text[cfg_len] = '\0';
}

static uint8_t port_stream_close(int stream, uint32_t got, bool aborted)
{
    const uint8_t *base;
    smsmap_plan_t plan;
    int err;

    if (stream != FN_STREAM_ROM) {
        if (!aborted)
            parse_cfg();
        return 0;
    }
    base = fuji_store_close(aborted);
    if (aborted || base == NULL) {
        cfg_mapper[0] = '\0';
        return 0;
    }
    err = smsmap_plan(base, got, cfg_mapper[0] ? cfg_mapper : NULL, &plan);
    cfg_mapper[0] = '\0';
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

/* A store to the REGSEL page is a whole register write, expanded into the
 * REGSEL/REGDATA pair the shared decoder expects; a store to DATA appends. */
static void mailbox_event(uint16_t offset, uint8_t data)
{
    unsigned page = offset & FN_H_PAGE_MASK;
    unsigned low = offset & 0xFF;

    if (page == FN_H_REGSEL || page == FN_H_REGDATA) {
        if (low == FN_REG_SLICE_ACK) {
            fuji_load_ack(&fuji_loader);
            return;
        }
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
