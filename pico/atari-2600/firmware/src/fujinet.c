/* fujinet.c -- the cartridge's side of the mailbox. The protocol is pico/common's
 * fujimail.c, which fuji_port.c binds to the USB link; this is the rest of the port:
 * where a published byte goes, what becomes of a pushed image, and the ring core1 fills.
 */

#include <string.h>

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_port.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

static uint32_t rx_len;
static bool rx_over;

static uint8_t port_rom_open(uint32_t size)
{
    rx_len = 0;
    rx_over = false;
    return vcs_gate(size);
}

static void port_rom_write(const uint8_t *chunk, unsigned len)
{
    uint8_t *stage = fuji_cart_stage_buffer();

    if (rx_len + len > FUJI_IMAGE_MAX) {
        rx_over = true;         /* the gate should have caught this */
        len = (unsigned)(FUJI_IMAGE_MAX - rx_len);
    }
    memcpy(stage + rx_len, chunk, len);
    rx_len += len;
}

/* `mapper` names the board where size cannot: an 8K F8, E0, UA and FE are all 8192
 * bytes. vcs_set_image spends it once, at the swap. */
static uint8_t port_rom_close(uint32_t got, bool aborted, const char *mapper)
{
    if (aborted)
        return FN_BOOT_ERR_TRUNCATED;
    /* got counts every byte pushed, rx_len only what fit */
    if (rx_over || got > rx_len)
        return FN_BOOT_ERR_TOOBIG;
    if (vcs_gate(rx_len) != 0)
        return FN_BOOT_ERR_NOMAP;
    vcs_set_cfg(&fuji_mem, mapper, mapper ? (unsigned)strlen(mapper) : 0u);
    fuji_cart_stage(rx_len);
    return 0;
}

static void port_arm_swap(void)
{
    fuji_mem.swap_armed = true;
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

    while (fuji_cart_next_event(&offset)) {
        if (offset == FN_H_PATHTX || offset == FN_H_PATHRAW) {
            /* One ring entry expands into 256 stream bytes. core1
             * cannot do this itself -- 256 ring pushes inside the bus loop
             * would miss cycles, and a missed cycle here serves the console a
             * wrong byte rather than merely slowing it down. Order is
             * preserved because the ring is FIFO: the path lands exactly
             * where the client put it in the stream. */
            unsigned n = (offset == FN_H_PATHTX)
                             ? FN_PATH_MAX
                             : fuji_mem.path_len[fuji_mem.path_sel];
            unsigned i;

            for (i = 0; i < n; i++)
                fujimail_read_hotspot(FN_H_DATA + vcs_path_byte(&fuji_mem, i));
            continue;
        }
        fujimail_read_hotspot(offset);
    }

    fuji_cart_service_deferred();
}
