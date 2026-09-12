/* fuji_cart.c -- the served window, the image buffers, and the core1/core0 ring.
 *
 * See fuji_cart.h for the split.
 */

#include <string.h>

#include "pico/stdlib.h"

#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "vcs_cart.h"
#include "vcs_render.h"

/* core1 reads these on the bus path, so they live in SRAM like everything
 * else it touches. On the RP2040 that is where .data and .bss are anyway;
 * the attribute is here to say it is required, not incidental. */
vcs_mem_t fuji_mem;
fuji_ring_t fuji_ring;

uint8_t fuji_image[2][FUJI_IMAGE_MAX];
uint32_t fuji_image_len[2];
volatile uint8_t fuji_live_image;

volatile bool fuji_have_staged;
volatile uint32_t fuji_staged_len;

volatile bool fuji_render_req;
volatile bool fuji_blit_req;
volatile uint8_t fuji_blit_xform;

uint8_t *fuji_cart_stage_buffer(void)
{
    return fuji_image[fuji_live_image ^ 1u];
}

void fuji_cart_init(void)
{
    memset(&fuji_ring, 0, sizeof fuji_ring);
    memset(&fuji_mem, 0, sizeof fuji_mem);
    fuji_live_image = 0;
    fuji_have_staged = false;
}

/* fujimail pokes CONSOLE addresses, straight out of fuji_mailbox.h. */
void __not_in_flash_func(fuji_cart_poke)(unsigned offset, uint8_t value)
{
    if (offset >= FN_WINDOW_BASE && offset < FN_WINDOW_BASE + FN_WINDOW_SIZE)
        fuji_mem.win[offset - FN_WINDOW_BASE] = value;
}

/* core0: a push finished and its bytes are in the staging buffer. */
void fuji_cart_stage(uint32_t len)
{
    fuji_image_len[fuji_live_image ^ 1u] = len;
    fuji_staged_len = len;
    fuji_have_staged = true;
}

/* core1: the client armed the swap and stored to the hotspot.
 *
 * This runs INSIDE the bus loop because it has to be complete before the
 * console's next fetch -- there is no wait state on this bus. vcs_set_image
 * copies one 2K bank and the fixed half, which is a few microseconds; the
 * console is executing the swap stub out of its own RAM at that moment, with
 * A12 low, so it is not asking the cartridge for anything.
 */
void __not_in_flash_func(fuji_cart_serve_staged)(void)
{
    uint8_t next;

    if (!fuji_have_staged)
        return;

    next = fuji_live_image ^ 1u;
    fuji_live_image = next;
    fuji_have_staged = false;
    vcs_set_image(&fuji_mem, fuji_image[next], fuji_image_len[next]);
}

/* core0: do the work core1 deferred. Called from the service loop. */
void fuji_cart_service_deferred(void)
{
    if (fuji_render_req) {
        fuji_render_req = false;
        vcs_render_row(fuji_mem.win, fuji_mem.trow, fuji_mem.tbuf,
                       fuji_mem.tlen);
        /* Published AFTER the row is actually on the planes: a client that
         * cares whether its text has landed polls this, and polling it before
         * the render would race. */
        fuji_mem.win[FN_B_TEXTGEN - FN_WINDOW_BASE] =
            (uint8_t)((fuji_mem.win[FN_B_TEXTGEN - FN_WINDOW_BASE] ^ 0x80)
                      | (fuji_mem.trow & 0x1F));
    }

    if (fuji_blit_req) {
        fuji_blit_req = false;
        vcs_blit(fuji_mem.win, fuji_mem.blit_src, fuji_mem.blit_dst,
                 fuji_mem.blit_cnt, fuji_blit_xform);
    }
}
