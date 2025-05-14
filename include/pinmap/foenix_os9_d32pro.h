/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_FOENIX_OS9_D32PRO

/* SD Card */
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef JTAG
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15 // fnSystem.h
#endif

#define PIN_SD_HOST_CS          GPIO_NUM_4  // LOLIN D32 Pro

/* UART - fnuart.cpp */
#define PIN_UART1_RX            GPIO_NUM_13 // RS232
#define PIN_UART1_TX            GPIO_NUM_21 // RS232

/* LEDs - leds.cpp */
#define PIN_LED_BUS             GPIO_NUM_4
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

#include "coco-common.h"
#include "common.h"

#endif /* PINMAP_FOENIX_OS9_D32PRO */
