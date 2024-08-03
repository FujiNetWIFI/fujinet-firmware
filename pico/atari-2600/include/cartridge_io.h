#ifndef CARTRIDGE_IO_H
#define CARTRIDGE_IO_H

#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/structs/sio.h"
#include "board.h"

#define SWCHA          0x280
#define SWCHB          0x282

#define RESET_ADDR addr = addr_prev = 0xffff;

// Used to control exit function
extern uint16_t EXIT_SWCHB_ADDR;

extern volatile uint16_t* const ADDR_IDR;
extern volatile uint8_t* const DATA_IDR;
extern volatile uint8_t* const DATA_ODR;
extern volatile uint16_t* const DATA_MODER;

#define ADDR_GPIO_MASK  (0xFFF << PINROMADDR)
#define A12_GPIO_MASK   (0x1 << PINENABLE)
#define DATA_GPIO_MASK  (0xFF << PINROMDATA)

extern const uint32_t addr_gpio_mask;
extern const uint32_t data_gpio_mask;

#define ADDR_IN (sio_hw->gpio_in & addr_gpio_mask) >> PINROMADDR
#define DATA_OUT(v) sio_hw->gpio_togl = (sio_hw->gpio_out ^ (v << PINROMDATA)) & data_gpio_mask
#define DATA_IN ((sio_hw->gpio_in & data_gpio_mask) >> PINROMDATA)
#define DATA_IN_BYTE DATA_IN

#define SET_DATA_MODE_IN    sio_hw->gpio_oe_clr = data_gpio_mask;
#define SET_DATA_MODE_OUT   sio_hw->gpio_oe_set = data_gpio_mask;
#define SET_DATA_MODE(m)    sio_hw->gpio_oe_togl = (sio_hw->gpio_oe ^ (m << PINROMDATA)) & data_gpio_mask

#endif // CARTRIDGE_IO_H
