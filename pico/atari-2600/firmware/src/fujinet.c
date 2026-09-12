/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol itself lives in fujimail.c, which the MAME model drives too;
 * this is only the port: where a published byte goes, how a frame reaches the
 * ESP32-S3, and what to do with a committed image.
 *
 * NOT YET RUN ON HARDWARE -- there is no Atari 2600 cartridge board yet.
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

static uint32_t rx_len;
static bool rx_over;

/* The .cfg sibling. Long enough for a name and a comment; anything past that
 * is not a scheme name. */
#define CFG_MAX 64
static char cfg_buf[CFG_MAX];
static unsigned cfg_len;

/* What can be pushed. A CLIENT is (N+1) * 2048 -- N banks then the fixed half
 * -- but a GAME is whatever its board was, and the smallest real 2600
 * cartridge is 2K. The two are told apart by the claim, which cannot be seen
 * until the bytes have arrived, so the gate accepts any whole number of 2K
 * blocks up to the buffer and lets vcs_set_image decide afterwards.
 *
 * Refusing HERE rather than at close is the point: it happens before the
 * ESP32 drags the whole file over TNFS. */
static uint8_t gate(uint32_t size)
{
    if (size < FN_BANK_SIZE || (size % FN_BANK_SIZE) != 0u)
        return FN_BOOT_ERR_NOMAP;
    if (size > FUJI_IMAGE_MAX)
        return FN_BOOT_ERR_TOOBIG;
    return 0;
}

static uint8_t port_stream_open(int stream, uint32_t size)
{
    if (stream == FN_STREAM_CFG) {
        cfg_len = 0;
        return 0;
    }
    if (stream != FN_STREAM_ROM)
        return 0;
    rx_len = 0;
    rx_over = false;
    return gate(size);
}

static void port_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    uint8_t *stage;

    if (stream == FN_STREAM_CFG) {
        unsigned room = CFG_MAX - cfg_len;

        if (len > room)
            len = room;
        memcpy(cfg_buf + cfg_len, chunk, len);
        cfg_len += len;
        return;
    }
    if (stream != FN_STREAM_ROM)
        return;
    stage = fuji_cart_stage_buffer();
    if (rx_len + len > FUJI_IMAGE_MAX) {
        rx_over = true;         /* the gate should have caught this */
        len = (unsigned)(FUJI_IMAGE_MAX - rx_len);
    }
    memcpy(stage + rx_len, chunk, len);
    rx_len += len;
}

static uint8_t port_stream_close(int stream, uint32_t got, bool aborted)
{
    (void)got;                  /* rx_len is what actually landed */

    if (stream == FN_STREAM_CFG) {
        /* Names the board for an image whose size cannot: an 8K F8, E0, UA
         * and FE are all 8192 bytes. Held until the ROM stream closes; the
         * cartridge spends it there, once. */
        vcs_set_cfg(&fuji_mem, aborted ? NULL : cfg_buf, cfg_len);
        return 0;
    }
    if (stream != FN_STREAM_ROM)
        return 0;
    if (aborted)
        return FN_BOOT_ERR_TRUNCATED;
    if (rx_over)
        return FN_BOOT_ERR_TOOBIG;
    if (gate(rx_len) != 0)
        return FN_BOOT_ERR_NOMAP;
    fuji_cart_stage(rx_len);
    return 0;
}

static void port_arm_swap(void)
{
    fuji_mem.swap_armed = true;
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

    while (fuji_cart_next_event(&offset)) {
        if (offset == FN_H_PATHTX || offset == FN_H_PATHRAW) {
            /* One ring entry expands into 256 stream bytes. core1 cannot do
             * this itself -- 256 ring pushes inside the bus loop would miss
             * cycles, and a missed cycle here serves the console a wrong byte
             * rather than merely slowing it down. Order is preserved because
             * the ring is FIFO: the path lands exactly where the client put
             * it in the stream. */
            unsigned n = (offset == FN_H_PATHTX) ? FN_PATH_MAX
                                                 : fuji_mem.path_len;
            unsigned i;

            for (i = 0; i < n; i++)
                fujimail_read_hotspot(FN_H_DATA + vcs_path_byte(&fuji_mem, i));
            continue;
        }
        fujimail_read_hotspot(offset);
    }

    fuji_cart_service_deferred();
}
