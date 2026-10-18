/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_COCO_DEVKITC


/* UART - fnuart.cpp */
#define PIN_UART1_RX            GPIO_NUM_33 // RS232
#define PIN_UART1_TX            GPIO_NUM_21 // RS232

/* LEDs - leds.cpp */
#define PIN_LED_BUS             GPIO_NUM_4
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* Buttons - keys.cpp */
// A DevKitC has no Safe Reset button.  keys.cpp gives GPIO 14 no pull-up,
// so left floating it picks up the UART beside it and reads as presses,
// each of which reboots the FujiNet.
#define PIN_BUTTON_C            GPIO_NUM_NC

#include "coco-common.h"
#include "common.h"

#endif /* PINMAP_COCO_DEVKITC */
