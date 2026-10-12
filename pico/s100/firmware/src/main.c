// main.c -- the FujiNet S-100 card's RP2354B: core1 answers bus cycles, core0
// runs USB and the link to the ESP32-S3. The card's behaviour is the shared core
// (s100_regs.c and friends), the same C the SIMH graft and the host tests run.
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/timer.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "s100_bootpage.h"
#include "s100_bus.pio.h"
#include "s100_images.h"
#include "s100_regs.h"

#define BUS_PIO pio0
#define BUS_SM  0

// Write data: first sample this long into the wait, then until two agree.
#define DO_LATE_NS   300
#define DO_RECHECK_NS 50

static s100_card_t card;
static uint32_t do_late_cycles, do_recheck_cycles;

static inline uint32_t now_us(void)
{
    return timer_hw->timerawl;
}

/* ---- DIP switches, through the 74HC165 chain ---- */

static uint32_t read_dips(void)
{
    uint32_t v = 0;
    gpio_put(S100_PIN_DIP_LD, 0);
    busy_wait_us(1);
    gpio_put(S100_PIN_DIP_LD, 1);
    for (int i = 0; i < S100_DIP_BITS; i++)
    {
        v |= (uint32_t)gpio_get(S100_PIN_DIP_Q) << i;
        gpio_put(S100_PIN_DIP_CK, 1);
        busy_wait_us(1);
        gpio_put(S100_PIN_DIP_CK, 0);
    }
    return v;
}

static void dips_to_card(uint32_t v, s100_dip_t *d)
{
    d->pipe_base = (uint8_t)((v & 0x3F) << 2);
    d->page = (uint8_t)(v >> 6);
    d->console = (uint8_t)((v >> 14) & 3);
    d->phantom = (v >> 16) & 1;
    d->poj = (v >> 17) & 1;
}

/* ---- core1: the bus ---- */

static inline void pulse(uint pin)
{
    gpio_put(pin, 0);
    busy_wait_at_least_cycles(20);
    gpio_put(pin, 1);
}

// DO is stable while the CPU waits; take it late and only once it holds still.
static inline uint8_t sample_do(void)
{
    busy_wait_at_least_cycles(do_late_cycles);
    uint8_t a = (uint8_t)sio_hw->gpio_in, b;
    for (;;)
    {
        busy_wait_at_least_cycles(do_recheck_cycles);
        b = (uint8_t)sio_hw->gpio_in;
        if (a == b)
            return b;
        a = b;
    }
}

static void __not_in_flash_func(s100_core1_main)(void)
{
    bool rom_on = card.rom_on;
    bool poj = card.poj_left != 0;
    bool reset_low = false;
    bool pinte = false;

    for (;;)
    {
        if (!pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM))
        {
            uint32_t snap = pio_sm_get(BUS_PIO, BUS_SM);
            uint8_t a = (uint8_t)(snap >> S100_PIN_A0);
            uint8_t blk = (uint8_t)((snap >> S100_PIN_BLK0) & 3);
            uint8_t v = 0xFF;
            uint32_t t = now_us();
            if (snap & ((1u << S100_PIN_SINP) | (1u << S100_PIN_SMEMR)))
                v = blk == S100_BLK_PAGE ? s100_mem(&card, a) : s100_in(&card, blk, a, t);
            else
                s100_out(&card, blk, a, sample_do(), t);
            pio_sm_put(BUS_PIO, BUS_SM, v); // drives DI and releases the wait

            if (rom_on && !card.rom_on)
                pulse(S100_PIN_ROMOFF);
            rom_on = card.rom_on;
            if (poj && card.poj_left == 0)
                pulse(S100_PIN_POJCLR);
            poj = card.poj_left != 0;
        }

        // RESET* (POC* is or'd in by the glue) also sets ROM_ON and POJ in hardware.
        bool r = !gpio_get(S100_PIN_RESET);
        if (r && !reset_low)
        {
            s100_reset(&card, now_us());
            rom_on = card.rom_on;
            poj = card.poj_left != 0;
        }
        reset_low = r;

        bool p = gpio_get(S100_PIN_PINTE);
        if (p != pinte)
            fdc_set_pinte(&card.fdc, p);
        pinte = p;
    }
}

/* ---- core0: USB CDC to the ESP32-S3 ---- */

static size_t usb_tx(void *ctx, const uint8_t *buf, size_t n)
{
    (void)ctx;
    size_t off = 0;
    while (off < n)
    {
        if (!tud_cdc_connected())
            return 0;
        uint32_t w = tud_cdc_write(buf + off, (uint32_t)(n - off));
        tud_cdc_write_flush();
        off += w;
        if (w == 0)
            tud_task();
    }
    return off;
}

static void bus_pins_init(void)
{
    // Inputs carry no pulls: RP2350 pads reset with pull-downs on, and erratum
    // RP2350-E9 makes a pulled-down input latch near 2 V.
    for (uint pin = 0; pin <= S100_PIN_PSYNC; pin++)
    {
        if (pin == S100_PIN_WAITCLR || (pin >= S100_PIN_DI0 && pin < S100_PIN_DI0 + 8))
            continue;
        gpio_init(pin);
        gpio_disable_pulls(pin);
    }
    const uint ins[] = {S100_PIN_RESET, S100_PIN_PINTE, S100_PIN_DIP_Q};
    for (unsigned i = 0; i < sizeof(ins) / sizeof(ins[0]); i++)
    {
        gpio_init(ins[i]);
        gpio_disable_pulls(ins[i]);
    }
    const uint outs[] = {S100_PIN_DIP_LD, S100_PIN_DIP_CK, S100_PIN_ROMOFF, S100_PIN_POJCLR,
                         S100_PIN_LED};
    for (unsigned i = 0; i < sizeof(outs) / sizeof(outs[0]); i++)
    {
        gpio_init(outs[i]);
        gpio_set_dir(outs[i], GPIO_OUT);
        gpio_put(outs[i], outs[i] != S100_PIN_LED && outs[i] != S100_PIN_DIP_CK);
    }
    gpio_init(S100_PIN_INT); // open drain, unused in Rev0: leave it floating
}

static void bus_pio_init(void)
{
    uint off = pio_add_program(BUS_PIO, &s100_bus_program);
    pio_sm_config c = s100_bus_program_get_default_config(off);
    sm_config_set_in_pins(&c, S100_PIN_DO0);
    sm_config_set_in_shift(&c, false, true, 22);
    sm_config_set_out_pins(&c, S100_PIN_DI0, 8);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_sideset_pins(&c, S100_PIN_WAITCLR);
    sm_config_set_jmp_pin(&c, S100_PIN_WAITQ);
    for (uint i = 0; i < 8; i++)
        pio_gpio_init(BUS_PIO, S100_PIN_DI0 + i);
    pio_gpio_init(BUS_PIO, S100_PIN_WAITCLR);
    pio_sm_set_pins_with_mask(BUS_PIO, BUS_SM, 1u << S100_PIN_WAITCLR, 1u << S100_PIN_WAITCLR);
    pio_sm_set_consecutive_pindirs(BUS_PIO, BUS_SM, S100_PIN_DI0, 8, true);
    pio_sm_set_consecutive_pindirs(BUS_PIO, BUS_SM, S100_PIN_WAITCLR, 1, true);
    pio_sm_init(BUS_PIO, BUS_SM, off, &c);
    pio_sm_set_enabled(BUS_PIO, BUS_SM, true);
}

int main(void)
{
    uint32_t mhz = clock_get_hz(clk_sys) / 1000000;
    do_late_cycles = mhz * DO_LATE_NS / 1000;
    do_recheck_cycles = mhz * DO_RECHECK_NS / 1000;
    bus_pins_init();

    s100_dip_t dip;
    dips_to_card(read_dips(), &dip);
    s100_init(&card, &dip, s100_images, S100_IMAGE_COUNT, s100_boot_page, s100_boot_patches,
              S100_BOOT_PATCH_COUNT, now_us());
    s100_link_init(&card, usb_tx, NULL);

    bus_pio_init();
    multicore_launch_core1(s100_core1_main);

    tusb_init();
    uint8_t buf[256];
    for (;;)
    {
        tud_task();
        uint32_t t = now_us();
        s100_link_set_up(&card, tud_cdc_connected(), t);
        while (tud_cdc_available())
        {
            uint32_t n = tud_cdc_read(buf, sizeof(buf));
            s100_link_rx(&card, buf, n, t);
        }
        s100_link_poll(&card, t);
        gpio_put(S100_PIN_LED, card.link.st != LS_IDLE);
    }
}
