/* fuji_cart.c -- core0's half of the cartridge: the ring drain, the arena,
 * the text engine's clock, and the console's power. CLEAR restarts whatever
 * is being served, so losing console power is the way back to CONFIG.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"

#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "fuji_watchdog.h"

fuji_ring_t fuji_ring;
uint8_t fuji_arena[FN_ARENA_SIZE];
fuji_load_t fuji_loader;
s2_text_t fuji_text;

static uint8_t image_buf[2][S2MAP_BUF_MAX];
static const uint8_t *config_rom;
static uint32_t config_size;
static fuji_watchdog_t watchdog;

void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (offset < FN_R_PAINT_END)
        fuji_arena[offset] = value;
    s2_text_boot_poke(&fuji_text, offset, value);
}

bool fuji_cart_next(uint16_t *entry)
{
    uint16_t tail = fuji_ring.tail;

    if (tail == fuji_ring.head)
        return false;
    *entry = fuji_ring.buf[tail];
    fuji_ring.tail = (uint16_t)((tail + 1u) % FUJI_RING_LEN);
    return true;
}

bool fuji_cart_peek(uint16_t *entry)
{
    uint16_t tail = fuji_ring.tail;

    if (tail == fuji_ring.head)
        return false;
    *entry = fuji_ring.buf[tail];
    return true;
}

/* The hotspot pages read $FF, except the hand-over stub: SEX RC; RET; $23. */
static void arena_init(void)
{
    memset(fuji_arena, 0, FN_H_REGSEL);
    memset(fuji_arena + FN_H_REGSEL, 0xFF, FN_ARENA_SIZE - FN_H_REGSEL);
    fuji_arena[FN_H_REGSEL + FN_HOT_STUB] = 0xEC;
    fuji_arena[FN_H_REGSEL + FN_HOT_STUB + 1] = 0x70;
    fuji_arena[FN_H_REGSEL + FN_HOT_T] = 0x23;
}

static void serve_config(void)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    arena_init();
    s2_text_init(&fuji_text, fuji_arena + FN_R_DATA);
    fuji_load_init(&fuji_loader, image_buf[0], image_buf[1], fuji_arena,
                   &fuji_text.disp, config_rom, config_size);
}

void fuji_cart_init(const uint8_t *config, uint32_t config_len)
{
    config_rom = config;
    config_size = config_len;
    serve_config();
    /* PWR_OK low for 10 ms, so a brown-out flicker is not a cycle */
    fuji_watchdog_init(&watchdog, -1, PWROK_PIN, 10000);
}

void fuji_cart_service(void)
{
    if (fuji_watchdog_gone(&watchdog)) {
        /* the console is off: stop core1 and serve CONFIG from the
         * next power-on; core1 waits for PWR_OK before it drives */
        multicore_reset_core1();
        gpio_set_mask(DRIVE_MASK | CLAIM_MASK);
        serve_config();
        fujimail_paint();
        multicore_launch_core1(s2_core1_main);
    }
    s2_text_tick(&fuji_text, to_ms_since_boot(get_absolute_time()));
    s2_text_compose(&fuji_text);
    fuji_cart_poke(FN_R_MODE, !fuji_loader.swaps ? FN_MODE_BOOT
                   : fuji_load_live(&fuji_loader)->mailbox ? FN_MODE_APP : FN_MODE_GAME);
    fuji_cart_poke(FN_R_SWAPS, fuji_loader.swaps);
    fuji_cart_poke(FN_R_FRAMES, fuji_bus.frames);
    gpio_put(LED_PIN, fuji_loader.swaps != 0);
}
