// main.c -- the FujiNet TRS-80 Model I card's RP2354B: core1 answers bus cycles,
// core0 runs USB, the FD1771 engine and the link to the ESP32-S3. The card's
// behaviour is m1_card.h and friends, the same C the MAME device and the host
// tests run.
#include <string.h>

#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "fuji_clock.h"
#include "m1_card.h"

#define STROBES ((1u << M1_PIN_RD) | (1u << M1_PIN_WR) | (1u << M1_PIN_IN) | (1u << M1_PIN_OUT))
#define DATA_MASK (0xFFu << M1_PIN_D0)

// No strobe for this long, or one stuck low this long, is a Model I switched off.
#define BUS_DEAD_US 300000

// The saved settings: the flash's last sector, past anything the firmware uses.
#define SETTINGS_OFF   (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC 0x3153314Du /* "M1S1" */

typedef struct {
    uint32_t magic;
    m1_settings_t settings;
    uint8_t check; /* ~settings.enabled */
} saved_t;

m1_card_t card;
static uint32_t power_ons; /* atomic: core1 counts the Model I coming on */

static inline uint32_t now_us(void)
{
    return timer_hw->timerawl;
}

/* ---- core1: the bus ---- */

// Puts val on the bus for the rest of this read: the '245 turns toward the bus
// first, so it never drives into our outputs; its OE drops at the strobe's
// rising edge whatever core1 is doing.
static inline void drive(uint8_t val)
{
    sio_hw->gpio_set = 1u << M1_PIN_DRIVE;
    sio_hw->gpio_clr = DATA_MASK & ~((uint32_t)val << M1_PIN_D0);
    sio_hw->gpio_set = (uint32_t)val << M1_PIN_D0;
    sio_hw->gpio_oe_set = DATA_MASK;
}

static inline void release(void)
{
    sio_hw->gpio_oe_clr = DATA_MASK;
    sio_hw->gpio_clr = 1u << M1_PIN_DRIVE;
}

// False if the strobe stayed low past BUS_DEAD_US: the Model I lost power mid-cycle.
static inline bool wait_high(uint32_t pin_mask)
{
    uint32_t start = now_us();
    while (!(sio_hw->gpio_in & pin_mask))
        if (now_us() - start > BUS_DEAD_US)
            return false;
    return true;
}

// The last sample taken while the strobe was low: the CPU's data is stable then.
static inline uint32_t write_sample(uint32_t first, uint32_t pin_mask)
{
    uint32_t sample = first, cur, start = now_us();
    while (!((cur = sio_hw->gpio_in) & pin_mask) && now_us() - start <= BUS_DEAD_US)
        sample = cur;
    return sample;
}

// One cycle of the CPU's; false if the bus went dead in it.
static inline bool serve(uint32_t pins, uint32_t low)
{
    uint16_t addr = (uint16_t)(pins >> M1_PIN_A0);
    uint8_t val;
    bool alive;
    if (low & (1u << M1_PIN_RD))
    {
        if (!m1_mem_peek(&card, addr, now_us(), &val))
            return wait_high(1u << M1_PIN_RD);
        drive(val);
        alive = wait_high(1u << M1_PIN_RD);
        release();
        m1_mem_after_read(&card, addr);
        return alive;
    }
    if (low & (1u << M1_PIN_IN))
    {
        if (!m1_io_peek(&card, (uint8_t)addr, now_us(), &val))
            return wait_high(1u << M1_PIN_IN);
        drive(val);
        alive = wait_high(1u << M1_PIN_IN);
        release();
        m1_io_after_read(&card, (uint8_t)addr);
        return alive;
    }
    if (low & (1u << M1_PIN_WR))
    {
        uint32_t sample = write_sample(pins, 1u << M1_PIN_WR);
        m1_mem_write(&card, addr, (uint8_t)(sample >> M1_PIN_D0), now_us());
        return (sio_hw->gpio_in & (1u << M1_PIN_WR)) != 0;
    }
    uint32_t sample = write_sample(pins, 1u << M1_PIN_OUT);
    m1_io_write(&card, (uint8_t)addr, (uint8_t)(sample >> M1_PIN_D0));
    return (sio_hw->gpio_in & (1u << M1_PIN_OUT)) != 0;
}

// The Model I's power is seen on the bus alone: a switched-off machine makes no
// cycles, or holds its strobes low. The first whole cycle after that is a power-on.
static void __not_in_flash_func(m1_core1_main)(void)
{
    bool reset_low = false;
    bool int_low = false;
    bool dead = true;
    uint32_t last_cycle = now_us();
    uint8_t chore = 0;

    for (;;)
    {
        uint32_t pins = sio_hw->gpio_in;
        if ((pins & STROBES) == STROBES)
        {
            // One chore a pass, so a strobe never waits on more than the longest.
            switch (chore++ & 3)
            {
            case 0:
                wd_apply(&card.fdc);
                break;
            case 1:
                m1_take_ticks(&card);
                break;
            case 2:
            {
                bool want = m1_int_pending(&card);
                if (want != int_low)
                {
                    if (want)
                        sio_hw->gpio_clr = 1u << M1_PIN_INT;
                    else
                        sio_hw->gpio_set = 1u << M1_PIN_INT;
                    int_low = want;
                }
                break;
            }
            default:
            {
                if (now_us() - last_cycle > BUS_DEAD_US)
                    dead = true;
                bool reset = !(pins & (1u << M1_PIN_SYSRES));
                if (reset && !reset_low)
                    m1_bus_reset(&card);
                reset_low = reset;
                break;
            }
            }
            continue;
        }
        // A strobe counts on its second low sample, so a glitch is never a cycle.
        pins = sio_hw->gpio_in;
        uint32_t low = ~pins & STROBES;
        if (!low)
            continue;
        if (dead)
        {
            // Not served: power is coming up, and this cycle is the ROM's anyway.
            if (wait_high(low))
            {
                dead = false;
                __atomic_fetch_add(&power_ons, 1, __ATOMIC_RELEASE);
            }
            last_cycle = now_us();
            continue;
        }
        if (!serve(pins, low))
            dead = true;
        last_cycle = now_us();
    }
}

/* ---- core0: USB CDC to the ESP32-S3 ---- */

static size_t usb_tx(void *ctx, const uint8_t *buf, size_t len)
{
    (void)ctx;
    size_t off = 0;
    while (off < len)
    {
        if (!tud_cdc_connected())
            return 0;
        uint32_t wrote = tud_cdc_write(buf + off, (uint32_t)(len - off));
        tud_cdc_write_flush();
        off += wrote;
        if (wrote == 0)
            tud_task();
    }
    return off;
}

static void bus_pins_init(void)
{
    // Inputs carry no pulls: RP2350 pads reset with pull-downs on, and erratum
    // RP2350-E9 makes a pulled-down input latch near 2 V.
    for (uint pin = M1_PIN_A0; pin <= M1_PIN_SYSRES; pin++)
    {
        gpio_init(pin);
        gpio_disable_pulls(pin);
    }
    for (uint pin = M1_PIN_DIP0; pin < M1_PIN_DIP0 + 3; pin++)
    {
        gpio_init(pin);
        gpio_disable_pulls(pin);
    }
    const uint outs[] = {M1_PIN_DRIVE, M1_PIN_INT, M1_PIN_WAIT, M1_PIN_LED};
    for (unsigned idx = 0; idx < sizeof(outs) / sizeof(outs[0]); idx++)
    {
        gpio_init(outs[idx]);
        gpio_set_dir(outs[idx], GPIO_OUT);
    }
    gpio_put(M1_PIN_DRIVE, 0);
    gpio_put(M1_PIN_INT, 1);  // into a 74LVC07: high leaves INT* to its pull-up
    gpio_put(M1_PIN_WAIT, 1);
    gpio_put(M1_PIN_LED, 0);
    // The data pins are inputs until a read of ours, with their output latches ready.
    gpio_set_dir_masked(DATA_MASK, 0);
}

static m1_settings_t load_settings(void)
{
    saved_t saved;
    memcpy(&saved, (const void *)(XIP_BASE + SETTINGS_OFF), sizeof(saved));
    if (saved.magic == SETTINGS_MAGIC && saved.check == (uint8_t)~saved.settings.enabled)
        return saved.settings;
    m1_settings_t defaults = {M1_SET_ALL, M1_PIPE_BASE};
    return defaults;
}

// core1 runs from SRAM and never touches flash, so it keeps serving the bus.
static void store_settings(const m1_settings_t *settings)
{
    static uint8_t page[FLASH_PAGE_SIZE];
    saved_t saved = {SETTINGS_MAGIC, *settings, (uint8_t)~settings->enabled};
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &saved, sizeof(saved));
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(SETTINGS_OFF, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_OFF, page, sizeof(page));
    restore_interrupts(irq);
}

// A closed DIP switch reads low and turns its block off, whatever is saved.
static uint8_t forced_off(void)
{
    uint8_t off = 0;
    if (!gpio_get(M1_PIN_DIP0))
        off |= M1_SET_RAM;
    if (!gpio_get(M1_PIN_DIP0 + 1))
        off |= M1_SET_EI;
    if (!gpio_get(M1_PIN_DIP0 + 2))
        off |= M1_SET_RS232;
    return off;
}

int main(void)
{
    fuji_clock_init();
    bus_pins_init();

    m1_settings_t settings = load_settings();
    m1_card_init(&card, &settings, forced_off(), usb_tx, NULL, now_us());
    multicore_launch_core1(m1_core1_main);

    tusb_init();
    uint8_t buf[256];
    uint32_t power_ons_seen = 0;
    for (;;)
    {
        uint32_t ons = __atomic_load_n(&power_ons, __ATOMIC_ACQUIRE);
        if (ons != power_ons_seen)
        {
            power_ons_seen = ons;
            m1_card_power_on(&card);
        }
        if (m1_card_take_save(&card))
        {
            m1_settings_t keep = {card.saved, card.settings.pipe_base};
            store_settings(&keep);
        }
        tud_task();
        uint32_t now = now_us();
        fuji_iolink_set_up(&card.link, tud_cdc_connected(), now);
        while (tud_cdc_available())
        {
            uint32_t len = tud_cdc_read(buf, sizeof(buf));
            fuji_iolink_rx(&card.link, buf, len, now);
        }
        m1_card_service(&card, now);
        gpio_put(M1_PIN_LED, card.link.state != FUJI_LS_IDLE);
    }
}
