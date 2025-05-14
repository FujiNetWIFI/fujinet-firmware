/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_RS232_REV0

/* UART - fnuart.cpp */
#define PIN_UART1_RX            GPIO_NUM_13 // RS232
#define PIN_UART1_TX            GPIO_NUM_21 // RS232

/* Buttons - keys.cpp */
#define PIN_BUTTON_C            GPIO_NUM_39 // Safe reset

/* LEDs - leds.cpp */
#define PIN_LED_WIFI            GPIO_NUM_14
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* RS232 Pins */
#define PIN_RS232_RI            GPIO_NUM_32 // (OUT) Ring Indicator
#define PIN_RS232_DCD           GPIO_NUM_22 // (OUT) Data Carrier Detect
#define PIN_RS232_RTS           GPIO_NUM_33 // (IN) Request to Send
#define PIN_RS232_CTS           GPIO_NUM_26 // (OUT) Clear to Send
#define PIN_RS232_DTR           GPIO_NUM_27 // (IN) Data Terminal Ready
#define PIN_RS232_DSR           GPIO_NUM_4  // (OUT) Data Set Ready
#define PIN_RS232_INVALID       GPIO_NUM_36 // (IN) RS232 Invalid Data (from TRS3238E)

#include "common.h"

#endif /* PINMAP_RS232_REV0 */
