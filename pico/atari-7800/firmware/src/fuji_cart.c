/* fuji_cart.c -- core0's half of the cartridge: the ring drain, the load
 * sequence's port (fuji_load.c), the console watchdog and the status bytes
 * that report what core1 has seen.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/structs/sio.h"

#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "a78_pio.h"

fuji_ring_t fuji_ring;
uint8_t fuji_arena[FN_ARENA_SIZE];
const uint8_t *fuji_loaderrom;
const uint8_t *fuji_bootblk;
volatile a78_bus_t fuji_bus;
a78map_t *volatile fuji_live;
fuji_load_t fuji_loader;
volatile uint8_t fuji_pokey_rd[16];
uint8_t fuji_hsc_shadow[A78MAP_HSC_RAM_SIZE];
volatile uint32_t fuji_hsc_dirty;
volatile uint32_t fuji_req;
volatile bool fuji_pal_seen;
volatile uint32_t fuji_bus_cycle;

/* console watchdog: CPU cycles counted by PWM 4 on GPIO25 */
static uint16_t wd_last;
static absolute_time_t wd_change;
static bool console_alive;

void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (offset < FN_R_PAINT_END)
        fuji_arena[offset] = value;
}

static void load_paint(const uint8_t *src, uint8_t fill)
{
    if (src)
        memcpy(fuji_arena + FN_R_DATA, src, FN_R_SLICE_LEN);
    else
        memset(fuji_arena + FN_R_DATA, fill, FN_R_SLICE_LEN);
}

static void load_window(uint16_t word)
{
    a78_pio_patch(FN_LOADWIN_BASE >> 13, word);
}

static const fuji_load_port_t load_port = { fuji_cart_poke, load_paint, load_window };

bool fuji_cart_next_event(a78_event_t *ev)
{
    uint16_t tail = fuji_ring.tail;

    if (tail == fuji_ring.head)
        return false;
    ev->offset = fuji_ring.buf[tail].offset;
    ev->data = fuji_ring.buf[tail].data;
    ev->kind = fuji_ring.buf[tail].kind;
    ev->cycle = fuji_ring.buf[tail].cycle;
    fuji_ring.tail = (uint16_t)((tail + 1u) % FUJI_RING_LEN);
    return true;
}

void fuji_cart_init(const uint8_t *loader, const uint8_t *bootblk,
                    const uint8_t *config, unsigned config_len)
{
    a78map_plan_t plan;

    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    memset(fuji_arena, 0, sizeof fuji_arena);
    memset((void *)fuji_pokey_rd, 0xFF, sizeof fuji_pokey_rd);
    memset(fuji_hsc_shadow, 0xFF, sizeof fuji_hsc_shadow);
    fuji_loaderrom = loader;
    fuji_bootblk = bootblk;
    a78_bus_reset((a78_bus_t *)&fuji_bus);
    fuji_live = NULL;

    /* CONFIG was checked when it was baked; a bad one still boots as rom */
    if (a78map_plan(config, config_len, NULL, &plan) != A78MAP_OK
        && a78map_plan(config, config_len, "a78_rom", &plan) != A78MAP_OK)
        memset(&plan, 0, sizeof plan);
    fuji_loader.port = &load_port;
    fuji_loader.bus = (const a78_bus_t *)&fuji_bus;
    fuji_loader.hsc_ram = fuji_hsc_shadow;
    fuji_load_init(&fuji_loader, config, &plan);

    /* PWM 4 counts rising edges of PHI2 on GPIO25 (channel B). */
    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&c, PWM_DIV_B_RISING);
    pwm_init(pwm_gpio_to_slice_num(PHI2_PIN), &c, true);
    gpio_set_function(PHI2_PIN, GPIO_FUNC_PWM);
    wd_change = get_absolute_time();
}

void fuji_cart_service(void)
{
    uint16_t cnt = (uint16_t)pwm_get_counter(pwm_gpio_to_slice_num(PHI2_PIN));
    absolute_time_t now = get_absolute_time();

    /* No CPU clock for 50 ms, or no console power: the boot block when it
     * is back. A power cycle is the 7800's only reset. */
    if (cnt != wd_last && gpio_get(PWROK_PIN)) {
        wd_last = cnt;
        wd_change = now;
        console_alive = true;
    } else if (console_alive && absolute_time_diff_us(wd_change, now) > 50000) {
        console_alive = false;
        fuji_load_abort(&fuji_loader);
        fuji_pal_seen = false;
        fuji_req = FUJI_REQ_BOOT;
    }

    if (fuji_pal_seen)
        fuji_loader.tv = FN_TV_PAL;
    fuji_cart_poke(FN_R_TV, fuji_loader.tv);
    fuji_cart_poke(FN_R_INPTCTRL, fuji_bus.inptctrl);
    fuji_cart_poke(FN_R_INPT_LOCK, fuji_bus.inpt_locked ? 1 : 0);
    fuji_cart_poke(FN_R_MODE, fuji_bus.mode);
    gpio_put(LED_PIN, fuji_bus.mode == FN_MODE_GAME || fuji_bus.mode == FN_MODE_APP);
}
