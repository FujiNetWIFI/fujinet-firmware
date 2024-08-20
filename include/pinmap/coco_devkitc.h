/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_COCO_DEVKITC

#include "common.h"

/* UART - fnuart.cpp */
#undef PIN_UART1_RX
#undef PIN_UART1_TX
#define PIN_UART1_RX            GPIO_NUM_13 // RS232
#define PIN_UART1_TX            GPIO_NUM_21 // RS232

#undef PIN_BUTTON_B
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B

#undef PIN_LED_BT
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* Coco */
#define PIN_CASS_MOTOR          GPIO_NUM_34 // Second motor pin is tied to +3V
#define PIN_CASS_DATA_IN        GPIO_NUM_33
#define PIN_CASS_DATA_OUT       GPIO_NUM_26
#define PIN_CD                  GPIO_NUM_22 // same as atari PROC
#define PIN_EPROM_A14           GPIO_NUM_36 // Used to set the serial baud rate
#define PIN_EPROM_A15           GPIO_NUM_39 // based on the HDB-DOS image selected
#endif /* PINMAP_COCO_DEVKITC */
