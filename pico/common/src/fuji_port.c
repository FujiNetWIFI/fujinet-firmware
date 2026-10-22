/* fuji_port.c -- fujimail on a cartridge; see fuji_port.h. */

#include "pico/bootrom.h"

#include "fuji_port.h"
#include "fujimail.h"
#include "fujibus_usb.h"
#include "fuji_cfg.h"
#include "fuji_mailbox_proto.h"

static const fuji_port_ops_t *ops;
static fujimail_port_t port;

/* The .cfg sibling arrives first; its `mapper=` applies to the next image. */
static fuji_cfg_t cfg;

static uint8_t stream_open(int stream, uint32_t size)
{
    if (stream != FN_STREAM_ROM) {
        fuji_cfg_open(&cfg);
        return 0;
    }
    return ops->rom_open(size);
}

static void stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    if (stream == FN_STREAM_ROM)
        ops->rom_write(chunk, len);
    else
        fuji_cfg_write(&cfg, chunk, len);
}

static uint8_t stream_close(int stream, uint32_t got, bool aborted)
{
    uint8_t err;

    if (stream != FN_STREAM_ROM) {
        fuji_cfg_close(&cfg, aborted);
        return 0;
    }
    err = ops->rom_close(got, aborted, fuji_cfg_mapper(&cfg));
    fuji_cfg_clear(&cfg);
    return err;
}

static void bootsel(void)
{
    reset_usb_boot(0, 0);
}

void fuji_port_init(const fuji_port_ops_t *o)
{
    ops = o;
    port.poke = o->poke;
    port.link_up = fujibus_link_up;
    port.transact = fujibus_transact;
    port.send_bare = fujibus_send_bare;
    port.stream_open = stream_open;
    port.stream_write = stream_write;
    port.stream_close = stream_close;
    port.arm_swap = o->arm_swap;
    port.wait_link_ms = fuji_wait_ms_pumped;
    port.bootsel = bootsel;
    port.own_reg = o->own_reg;
    fujimail_init(&port);
    fujibus_set_inbound_handler(fujimail_inbound);
}
