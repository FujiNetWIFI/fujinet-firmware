/* sms_cart.c -- core1: the Z80 bus.
 *
 * Every address change puts the live table's bank on SRAM A13-A19 and picks
 * the byte the cart would serve, so a read only has to turn the pins on.
 * Writes are sampled until /WR rises and dispatched by sms_cart.h. Nothing is
 * driven without PWR_OK: an unpowered console reads as every strobe low.
 *
 * Budget at 3.58 MHz: an M1 fetch needs data ~289 ns after /RD falls and the
 * SRAM needs its bank bits ~300 ns before then; this loop is a few dozen
 * instructions at 200 MHz. Everything here runs from SRAM (copy_to_ram;
 * tools/checksram.py checks).
 *
 * PROVISIONAL until a scope says otherwise: how long write data holds after
 * /WR rises (the last sample before it rises is taken), and the 150 ns the
 * load window keeps driving after /RD rises for the SRAM's /WE edge.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

#include "sms_cart.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "smsmap.h"

#pragma GCC push_options
#pragma GCC optimize("O3")

static uint32_t cur_bank;

static inline void data_drive(uint8_t v)
{
    uint32_t bits = (uint32_t)v << D0_PIN;

    sio_hw->gpio_clr = DATA_MASK & ~bits;
    sio_hw->gpio_set = bits;
    sio_hw->gpio_oe_set = DATA_MASK;
}

static inline void data_release(void)
{
    sio_hw->gpio_oe_clr = DATA_MASK;
}

static inline void put_bank(uint32_t b)
{
    if (b != cur_bank) {
        sio_hw->gpio_hi_togl = ((b ^ cur_bank) << HI_BANK_SHIFT) & HI_BANK_MASK;
        cur_bank = b;
    }
}

static inline void to_resident(void)
{
    fuji_live = &fuji_resident_map;
    fuji_bus.ptab = fuji_ptab_resident;
    fuji_bus.mode = FN_MODE_RESIDENT;
    sio_hw->gpio_hi_clr = HI(GAME_PIN) | HI(MBOX_PIN) | HI(RAMWE_PIN);
}

static inline void flip(void)
{
    smsmap_t *m = fuji_loader.next;
    uint8_t mode = fuji_loader.next_mode;

    fuji_live = m;
    fuji_bus.mode = mode;
    if (mode == FN_MODE_RESIDENT) {
        fuji_bus.ptab = fuji_ptab_resident;
        sio_hw->gpio_hi_clr = HI(GAME_PIN) | HI(MBOX_PIN) | HI(RAMWE_PIN);
    } else {
        fuji_bus.ptab = mode == FN_MODE_APP ? fuji_ptab_app : fuji_ptab_game;
        sio_hw->gpio_hi_clr = HI(MBOX_PIN) | HI(RAMWE_PIN);
        sio_hw->gpio_hi_set = HI(GAME_PIN) | (mode == FN_MODE_APP ? HI(MBOX_PIN) : 0)
                            | (m->ram_we ? HI(RAMWE_PIN) : 0);
    }
    put_bank(m->lut[0]);
}

static void console_reset(void)
{
    data_release();
    to_resident();
    sms_bus_reset(&fuji_bus, fuji_ptab_resident);
    fuji_reset_seen = true;
    while (!(sio_hw->gpio_in & RESET_MASK))
        ;
}

static inline void mapper_write(uint16_t a, uint8_t d)
{
    smsmap_t *m = fuji_live;

    if (m->ram_we && sms_in_slot2(a))
        fuji_ram_shadow[smsmap_sram_offset(m, a) - SMSMAP_RAM_BANK * 0x2000u] = d;
    if (smsmap_write(m, a, d)) {
        if (m->ram_we)
            sio_hw->gpio_hi_set = HI(RAMWE_PIN);
        else
            sio_hw->gpio_hi_clr = HI(RAMWE_PIN);
    }
}

static inline void write_cycle(uint32_t pins)
{
    uint32_t prev;
    uint16_t a;
    uint8_t d;

    do {                                   /* the last sample while /WR is low */
        prev = pins;
        pins = sio_hw->gpio_in;
    } while (!(pins & WR_MASK));
    a = sms_addr(prev);
    d = sms_data(prev);

    if (!(prev & MREQ_MASK)) {
        switch (sms_write_kind(&fuji_bus, a, !(prev & CE_MASK))) {
        case SMS_W_MAPPER:
            mapper_write(a, d);
            break;
        case SMS_W_MAILBOX:
            fuji_cart_note((uint16_t)(a & (FN_ARENA_SIZE - 1)), d, SMS_W_MAILBOX);
            break;
        case SMS_W_SWAP:
            to_resident();
            fuji_cart_note(0, d, SMS_W_SWAP);
            break;
        case SMS_W_CONFIG:
            to_resident();
            fuji_cart_note(0, d, SMS_W_CONFIG);
            break;
        case SMS_W_GO:
            fuji_bus.go_armed = true;
            fuji_cart_note(0, d, SMS_W_GO);
            break;
        case SMS_W_C000:
            fuji_bus.c000 = d;
            break;
        default:
            break;
        }
    } else if (!(prev & IORQ_MASK)) {
        sms_io_write(&fuji_bus, (uint8_t)a, d);
    }
}

void __not_in_flash_func(sms_core1_main)(void)
{
    uint16_t last = 0xFFFF;
    const uint8_t *ptr = NULL;

    cur_bank = 0;
    for (;;) {
        uint32_t pins;
        uint16_t a;

        __asm volatile("" ::: "memory");   /* core0 rewrites the tables */
        pins = sio_hw->gpio_in;
        a = sms_addr(pins);

        if (a != last) {
            put_bank(fuji_live->lut[a >> 10]);
            ptr = sms_serve_ptr(&fuji_bus, a);
            last = a;
        }
        if (!(pins & RESET_MASK)) {
            console_reset();
            last = 0xFFFF;
            continue;
        }
        if (fuji_req) {
            if (fuji_req & FUJI_REQ_RESIDENT)
                to_resident();
            fuji_req = 0;
            last = 0xFFFF;
            continue;
        }

        if (!(pins & RD_MASK)) {
            if (!(pins & MREQ_MASK)) {
                bool ce = !(pins & CE_MASK);

                if (!(pins & M1_MASK) && a == 0 && sms_fetch(&fuji_bus, a, ce)) {
                    flip();                /* the SRAM answers this fetch */
                    ptr = NULL;
                    last = 0xFFFF;
                } else if (ptr && ce && (sio_hw->gpio_hi_in & HI(PWROK_PIN))) {
                    data_drive(*ptr);
                    while (!(sio_hw->gpio_in & RD_MASK))
                        ;
                    if (sms_in_loadwin(a))         /* /WE rises 2 gates later */
                        busy_wait_at_least_cycles(30);
                    data_release();
                    continue;
                }
            } else if (!(pins & IORQ_MASK)) {
                sms_io_read(&fuji_bus, (uint8_t)a);
            }
            while (!(sio_hw->gpio_in & RD_MASK))
                ;
            continue;
        }
        if (!(pins & WR_MASK)) {
            write_cycle(pins);
            last = 0xFFFF;                 /* the write may have moved this page */
        }
    }
}

#pragma GCC pop_options
