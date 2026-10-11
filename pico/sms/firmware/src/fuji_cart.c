/* fuji_cart.c -- core0's half of the cartridge: the ring drain, the load
 * sequence's port (fuji_load.c) and the console watchdog.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/structs/sio.h"

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
uint8_t fuji_arena[FN_ARENA_SIZE];
uint8_t fuji_ram_shadow[SMSMAP_RAM_MAX];
uint8_t fuji_window[FN_LOADWIN_SIZE];
sms_bus_t fuji_bus;
smsmap_t fuji_resident_map;
static smsmap_t game_map;
smsmap_t *volatile fuji_live = &fuji_resident_map;
const uint8_t *fuji_ptab_resident[SMS_PAGES];
const uint8_t *fuji_ptab_app[SMS_PAGES];
const uint8_t *fuji_ptab_game[SMS_PAGES];
volatile uint32_t fuji_req;
volatile bool fuji_reset_seen;
fuji_load_t fuji_loader;

/* console watchdog: Z80 clocks counted by PWM 7 on GPIO31 */
static uint16_t wd_last;
static absolute_time_t wd_change;
static bool console_alive;

void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (offset < FN_R_PAINT_END)
        fuji_arena[offset] = value;
}

static void set_load(bool on)
{
    if (on)
        sio_hw->gpio_hi_set = HI(LOAD_PIN);
    else
        sio_hw->gpio_hi_clr = HI(LOAD_PIN);
}

static const fuji_load_port_t load_port = { fuji_cart_poke, set_load };

bool fuji_cart_next_event(sms_event_t *ev)
{
    uint16_t tail = fuji_ring.tail;

    if (tail == fuji_ring.head)
        return false;
    ev->offset = fuji_ring.buf[tail].offset;
    ev->data = fuji_ring.buf[tail].data;
    ev->kind = fuji_ring.buf[tail].kind;
    fuji_ring.tail = (uint16_t)((tail + 1u) % FUJI_RING_LEN);
    return true;
}

void fuji_cart_init(const uint8_t *loader, unsigned loader_len,
                    const uint8_t *resident, unsigned resident_len)
{
    unsigned p;

    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    memset(fuji_arena, 0, sizeof fuji_arena);
    if (loader_len > FN_LOADER_SIZE)
        loader_len = FN_LOADER_SIZE;
    memcpy(fuji_arena + FN_LOADER, loader, loader_len);
    memset(&fuji_resident_map, 0, sizeof fuji_resident_map);

    if (resident_len > FN_RESIDENT_MAX)
        resident_len = FN_RESIDENT_MAX;
    for (p = 0; p < SMS_PAGES; p++) {
        fuji_ptab_resident[p] = NULL;
        fuji_ptab_app[p] = NULL;
        fuji_ptab_game[p] = NULL;
    }
    for (p = 0; p < resident_len / 0x400; p++)
        fuji_ptab_resident[p] = resident + p * 0x400;
    for (p = FN_ARENA_BASE >> 10; p < (FN_ARENA_BASE + FN_ARENA_SIZE) >> 10; p++) {
        fuji_ptab_resident[p] = fuji_arena + ((p << 10) - FN_ARENA_BASE);
        fuji_ptab_app[p] = fuji_ptab_resident[p];
    }
    sms_bus_reset(&fuji_bus, fuji_ptab_resident);
    fuji_live = &fuji_resident_map;

    fuji_loader.port = &load_port;
    fuji_loader.window = fuji_window;
    fuji_loader.ptab_resident = fuji_ptab_resident;
    fuji_loader.resident_map = &fuji_resident_map;
    fuji_loader.game_map = &game_map;
    fuji_loader.bus = &fuji_bus;
    fuji_load_init(&fuji_loader);

    /* PWM 7 counts rising edges of the Z80 clock on GPIO31 (channel B). */
    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&c, PWM_DIV_B_RISING);
    pwm_init(pwm_gpio_to_slice_num(CLK_PIN), &c, true);
    gpio_set_function(CLK_PIN, GPIO_FUNC_PWM);
    wd_change = get_absolute_time();
}

void fuji_cart_service(void)
{
    uint16_t cnt = (uint16_t)pwm_get_counter(pwm_gpio_to_slice_num(CLK_PIN));
    absolute_time_t now = get_absolute_time();

    if (fuji_reset_seen) {
        fuji_reset_seen = false;
        fuji_load_abort(&fuji_loader);
    }

    /* No Z80 clock for 250 ms, or no console power: CONFIG when it is back. */
    if (cnt != wd_last && gpio_get(PWROK_PIN)) {
        wd_last = cnt;
        wd_change = now;
        console_alive = true;
    } else if (console_alive && absolute_time_diff_us(wd_change, now) > 250000) {
        console_alive = false;
        fuji_load_abort(&fuji_loader);
        fuji_req = FUJI_REQ_RESIDENT;
    }
    gpio_put(LED_PIN, fuji_bus.mode != FN_MODE_RESIDENT);
}
