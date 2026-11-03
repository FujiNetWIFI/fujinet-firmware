/* fuji_cart.c -- the served window, the image buffers, and the core1/core0 ring.
 *
 * See fuji_cart.h for the split.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "fuji_watchdog.h"
#include "vcs_cart.h"
#include "vcs_pins.h"
#include "vcs_render.h"

/* core1 reads these on the bus path, so they must be in SRAM, like everything
 * else it touches; fujicart_checksram() fails the build otherwise. */
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

static const uint8_t *config_rom;
static uint32_t config_size;
static fuji_watchdog_t watchdog;

uint8_t *fuji_cart_stage_buffer(void)
{
    return fuji_image[fuji_live_image ^ 1u];
}

/* vcs_set_image decides for itself whether the mailbox comes up, by looking for the claim
 * where the image's fixed half puts it. */
static void serve_config(void)
{
    uint32_t len = config_size > FUJI_IMAGE_MAX ? FUJI_IMAGE_MAX : config_size;

    memset(&fuji_ring, 0, sizeof fuji_ring);
    memset(&fuji_mem, 0, sizeof fuji_mem);
    fuji_have_staged = false;
    fuji_render_req = false;
    fuji_blit_req = false;
    memcpy(fuji_image[0], config_rom, len);
    fuji_image_len[0] = len;
    fuji_live_image = 0;
    vcs_set_image(&fuji_mem, fuji_image[0], len);
}

void fuji_cart_init(const uint8_t *config, uint32_t config_len)
{
    config_rom = config;
    config_size = config_len;
    serve_config();
    /* VSENSE low for 10 ms, so a brown-out flicker is not a power cycle */
    fuji_watchdog_init(&watchdog, -1, VSENSE_PIN, 10000);
}

/* The console has no reset line, so its power switch is the way back to CONFIG. The cart
 * may stay up on USB power; core1 waits for VSENSE again before it drives. */
void fuji_cart_service(void)
{
    if (!fuji_watchdog_gone(&watchdog))
        return;
    multicore_reset_core1();
    serve_config();
    fujimail_paint();
    multicore_launch_core1(vcs_core1_main);
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
        if (fuji_blit_xform == FN_BLIT_TCELL)
            vcs_render_cell(fuji_mem.win,
                            (uint8_t)(fuji_mem.blit_dst / FN_T_COLS),
                            (uint8_t)(fuji_mem.blit_dst % FN_T_COLS),
                            (uint8_t)fuji_mem.blit_src);
        else if (fuji_blit_xform == FN_BLIT_PATH)
            vcs_render_path_row(fuji_mem.win,
                                fuji_mem.path[fuji_mem.path_sel],
                                fuji_mem.path_len[fuji_mem.path_sel],
                                fuji_mem.blit_src,
                                (uint8_t)fuji_mem.blit_dst,
                                fuji_mem.blit_cnt);
        else if (fuji_blit_xform == FN_BLIT_PATHPOKE)
            vcs_render_path_poke(fuji_mem.win,
                                 fuji_mem.path[fuji_mem.path_sel],
                                 fuji_mem.path_len[fuji_mem.path_sel],
                                 fuji_mem.blit_src,
                                 fuji_mem.blit_dst,
                                 fuji_mem.blit_cnt);
        else if (fuji_blit_xform == FN_BLIT_PATHTILE)
            vcs_render_path_tile(fuji_mem.win,
                                 fuji_mem.path[fuji_mem.path_sel],
                                 fuji_mem.path_len[fuji_mem.path_sel],
                                 fuji_mem.blit_src,
                                 (uint8_t)fuji_mem.blit_dst,
                                 fuji_mem.blit_cnt);
        else
            vcs_blit(fuji_mem.win, fuji_mem.board, fuji_mem.blit_src,
                     fuji_mem.blit_dst, fuji_mem.blit_cnt, fuji_blit_xform);
        /* Published AFTER the blit has landed, for the same reason as
         * FN_B_TEXTGEN above: the request slot is single and a client that
         * fires the next blit before this one ran has lost this one. */
        fuji_mem.win[FN_B_BLITGEN - FN_WINDOW_BASE]++;
    }
}
