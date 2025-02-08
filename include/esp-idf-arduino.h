// -----------------------------------------------------------------------------
// Copyright (C) 2024 David Hansel
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have receikved a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
// -----------------------------------------------------------------------------

#ifndef IECESPIDF_H
#define IECESPIDF_H

#include <stdint.h>
#include <memory.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <soc/gpio_reg.h>
#include "hal/gpio_hal.h"
#include <freertos/FreeRTOS.h>

#define ARDUINO 2024
#define ESP32

#define PROGMEM

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wvolatile"

typedef void (*interruptFcn)(void *);

#define INPUT         0x0
#define OUTPUT        0x1
#define INPUT_PULLUP  0x2
#define LOW           0x0
#define HIGH          0x1
#define FALLING       GPIO_INTR_NEGEDGE
#define RISING        GPIO_INTR_POSEDGE
#define bit(n) (1<<(n))
#define digitalWrite(pin, v) gpio_set_level((gpio_num_t) pin, v);
#define pinMode(pin, mode)   { gpio_reset_pin((gpio_num_t) pin); gpio_set_direction((gpio_num_t)pin, mode==OUTPUT ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT); if( mode==INPUT_PULLUP ) gpio_pullup_en((gpio_num_t) pin); }
#define digitalPinToGPIONumber(digitalPin) (digitalPin)
#define digitalPinToBitMask(pin) (1UL << (digitalPinToGPIONumber(pin)&31))
#define portInputRegister(port)  ((volatile uint32_t*)((port)?GPIO_IN1_REG:GPIO_IN_REG))
#define portOutputRegister(port) ((volatile uint32_t*)((port)?GPIO_OUT1_REG:GPIO_OUT_REG))
#define portModeRegister(port)   ((volatile uint32_t*)((port)?GPIO_ENABLE1_REG:GPIO_ENABLE_REG))
#define digitalPinToPort(pin)    ((digitalPinToGPIONumber(pin)>31)?1:0)
#define digitalPinToInterrupt(p) ((((uint8_t)digitalPinToGPIONumber(p))<SOC_GPIO_PIN_COUNT)?digitalPinToGPIONumber(p):NOT_AN_INTERRUPT)

#define noInterrupts() portDISABLE_INTERRUPTS()
#define interrupts()   portENABLE_INTERRUPTS()
#define micros() ((uint32_t) esp_timer_get_time())
#define PSTR(x) x
#define strncmp_P strncmp
#define strcmp_P  strcmp
#define min(x, y) ((x)<(y) ? (x) : (y))
#define max(x, y) ((x)>(y) ? (x) : (y))

static void delayMicroseconds(uint32_t n) 
{ 
  uint32_t s = micros(); 
  while((micros()-s)<n); 
}

static void attachInterrupt(uint8_t pin, interruptFcn userFunc, gpio_int_type_t intr_type)
{
  static bool interrupt_initialized = false;

  if (pin >= SOC_GPIO_PIN_COUNT) return;

  if (!interrupt_initialized) {
    esp_err_t err = gpio_install_isr_service(0 /* ESP_INTR_FLAG_IRAM */);
    interrupt_initialized = (err == ESP_OK) || (err == ESP_ERR_INVALID_STATE);
  }

  gpio_set_intr_type((gpio_num_t)pin, intr_type);
  gpio_isr_handler_add((gpio_num_t)pin, userFunc, NULL);

  gpio_hal_context_t gpiohal;
  gpiohal.dev = GPIO_LL_GET_HW(GPIO_PORT_0);
  gpio_hal_input_enable(&gpiohal, pin);
}

static void detachInterrupt(uint8_t pin)
{
  gpio_isr_handler_remove((gpio_num_t)pin);
  gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_DISABLE);
}

#endif
