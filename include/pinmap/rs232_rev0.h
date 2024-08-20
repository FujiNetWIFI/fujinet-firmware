/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_RS232_REV0

#include "common.h"

#undef PIN_UART1_RX
#undef PIN_UART1_TX
#undef PIN_UART2_RX
#undef PIN_UART2_TX
#define PIN_UART1_RX            GPIO_NUM_13 // RS232
#define PIN_UART1_TX            GPIO_NUM_21 // RS232

#undef PIN_BUTTON_B
#undef PIN_BUTTON_C
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B
#define PIN_BUTTON_C            GPIO_NUM_39 // Safe reset

#undef PIN_LED_WIFI
#undef PIN_LED_BUS
#undef PIN_LED_BT
#define PIN_LED_WIFI            GPIO_NUM_14
#define PIN_LED_BUS             GPIO_NUM_12
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* RS232 Pins */
#define PIN_RS232_RI            GPIO_NUM_32 // (OUT) Ring Indicator
#define PIN_RS232_DCD           GPIO_NUM_22 // (OUT) Data Carrier Detect
#define PIN_RS232_RTS           GPIO_NUM_33 // (IN) Request to Send
#define PIN_RS232_CTS           GPIO_NUM_26 // (OUT) Clear to Send
#define PIN_RS232_DTR           GPIO_NUM_27 // (IN) Data Terminal Ready
#define PIN_RS232_DSR           GPIO_NUM_4  // (OUT) Data Set Ready
#define PIN_RS232_INVALID       GPIO_NUM_36 // (IN) RS232 Invalid Data (from TRS3238E)

#endif /* PINMAP_RS232_REV0 */
