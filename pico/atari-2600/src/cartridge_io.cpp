#include "cartridge_io.h"

// if no HARDWARE_TYPE, leave undefined for now. ELF loader will handle defining at load time
/*
#ifdef HARDWARE_TYPE
#if HARDWARE_TYPE == UNOCART
   volatile uint8_t* const  DATA_IDR = &(((uint8_t*)(&GPIOE->IDR))[1]);
   volatile uint8_t* const DATA_ODR = &(((uint8_t*)(&GPIOE->ODR))[1]);
   volatile uint16_t* const DATA_MODER = &(((uint16_t*)(&GPIOE->MODER))[1]);
   volatile uint16_t* const ADDR_IDR = ((uint16_t*)(&GPIOD->IDR));
#elif HARDWARE_TYPE == PLUSCART
#include "stm32f4xx.h"
   volatile uint8_t* const DATA_IDR = ((uint8_t*)(&GPIOC->IDR));
   volatile uint8_t* const DATA_ODR = ((uint8_t*)(&GPIOC->ODR));
   volatile uint16_t* const DATA_MODER = ((uint16_t*)(&GPIOC->MODER));
   volatile uint16_t* const ADDR_IDR = ((uint16_t*)(&GPIOD->IDR));
#elif HARDWARE_TYPE == PICOCART
*/
#include "hardware/structs/sio.h"
   const uint32_t addr_gpio_mask = ADDR_GPIO_MASK | A12_GPIO_MASK;
   const uint32_t data_gpio_mask = DATA_GPIO_MASK;
   volatile uint16_t* const ADDR_IDR = (uint16_t*)(&sio_hw->gpio_in);
   volatile uint8_t* const DATA_IDR = (uint8_t*)(&sio_hw->gpio_in);
   volatile uint8_t* const DATA_ODR = (uint8_t*)(&sio_hw->gpio_out);
   volatile uint16_t* const DATA_MODER = (uint16_t*)(&sio_hw->gpio_oe);
/*
#endif
#endif
*/

uint16_t EXIT_SWCHB_ADDR = SWCHB;
