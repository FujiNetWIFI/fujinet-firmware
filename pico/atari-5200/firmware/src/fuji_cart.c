/* fuji_cart.c -- core0's half of the cartridge: the ring drain, the arena,
 * and the console's power. The 5200 has no reset line and no reset button,
 * so losing console power is the only way back to CONFIG.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"

#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "fujimail.h"

fuji_ring_t fuji_ring;
uint8_t fuji_arena[FN_ARENA_SIZE];
fuji_load_t fuji_loader;

static uint8_t view_buf[2][A52MAP_VIEW_MAX];
static const uint8_t *config_rom;
static uint32_t config_size;
static bool console_on;
static absolute_time_t power_change;

void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (offset < FN_R_PAINT_END)
        fuji_arena[offset] = value;
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

/* The arena's hotspot pages read $FF, except the stub: JMP ($FFFC). */
static void arena_init(void)
{
    memset(fuji_arena, 0, FN_H_REGSEL);
    memset(fuji_arena + FN_H_REGSEL, 0xFF, FN_ARENA_SIZE - FN_H_REGSEL);
    fuji_arena[FN_H_REGSEL + FN_HOT_STUB] = 0x6C;
    fuji_arena[FN_H_REGSEL + FN_HOT_STUB + 1] = 0xFC;
    fuji_arena[FN_H_REGSEL + FN_HOT_SWAP] = 0xFF;
}

static void serve_config(void)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    arena_init();
    fuji_load_init(&fuji_loader, view_buf[0], view_buf[1], fuji_arena,
                   config_rom, config_size);
}

void fuji_cart_init(const uint8_t *config, uint32_t config_len)
{
    config_rom = config;
    config_size = config_len;
    serve_config();
    console_on = gpio_get(PWROK_PIN);
    power_change = get_absolute_time();
}

void fuji_cart_service(void)
{
    bool on = gpio_get(PWROK_PIN);
    absolute_time_t now = get_absolute_time();

    if (on != console_on) {
        /* 10 ms of the new level, so a brown-out flicker is not a cycle */
        if (absolute_time_diff_us(power_change, now) > 10000) {
            console_on = on;
            power_change = now;
            if (!on) {
                /* the console is off: stop core1 and serve CONFIG from the
                 * next power-on; core1 waits for PWR_OK before it drives */
                multicore_reset_core1();
                gpio_put(BUFEN_PIN, 0);
                serve_config();
                fujimail_paint();
                multicore_launch_core1(a52_core1_main);
            }
        }
    } else {
        power_change = now;
    }
    fuji_cart_poke(FN_R_MODE, !fuji_loader.swaps ? FN_MODE_BOOT
                   : fuji_load_live(&fuji_loader)->mailbox ? FN_MODE_APP : FN_MODE_GAME);
    fuji_cart_poke(FN_R_MAPPER, fuji_loader.plan[fuji_loader.live].kind);
    fuji_cart_poke(FN_R_SWAPS, fuji_loader.swaps);
    gpio_put(LED_PIN, fuji_loader.swaps != 0);
}
