/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_H89

/* SD Card - fnFsSD.cpp */
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h

/* UART - fnuart.cpp */
#define PIN_UART1_RX            GPIO_NUM_33 // not connected
#define PIN_UART1_TX            GPIO_NUM_21 // not connected
#define PIN_UART2_RX            GPIO_NUM_13 // not connected - RC2014 SIO

/* Buttons - keys.cpp */
#define PIN_BUTTON_C            GPIO_NUM_39 // Safe reset

/* LEDs - leds.cpp */
#define PIN_LED_WIFI            GPIO_NUM_14
#define PIN_LED_BUS             GPIO_NUM_12
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* SPI BUS interface */
#define PIN_BUS_DEVICE_CS       GPIO_NUM_35 // (input only)
#define PIN_BUS_DEVICE_MISO     GPIO_NUM_26
#define PIN_BUS_DEVICE_MOSI     GPIO_NUM_25
#define PIN_BUS_DEVICE_SCK      GPIO_NUM_33

#define PIN_CMD_RDY             GPIO_NUM_27
#define PIN_CMD                 GPIO_NUM_32
#define PIN_DATA                GPIO_NUM_34 // input only
#define PIN_PROCEED             GPIO_NUM_4

#include "common.h"

#endif /* PINMAP_RC2014SPI_REV0 */
