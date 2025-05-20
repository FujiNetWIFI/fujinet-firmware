/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_COCO_CART

/* UART - fnuart.cpp */
#define PIN_UART1_RX            GPIO_NUM_13 // RS232 HDSEL
#define PIN_UART1_TX            GPIO_NUM_21 // RS232 DRV2
#define PIN_UART2_RX            GPIO_NUM_13

/* LEDs - leds.cpp */
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

#include "coco-common.h"
#include "common.h"

#endif /* PINMAP_COCO_DEVKITC */
