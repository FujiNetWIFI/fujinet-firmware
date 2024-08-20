#ifdef PINMAP_ESP32S3

#include "common.h"

#undef PIN_SD_HOST_SCK
#undef PIN_SD_HOST_MOSI
#undef PIN_SD_HOST_MISO
#undef PIN_SD_HOST_CS
#undef PIN_CARD_DETECT
#define PIN_SD_HOST_SCK         GPIO_NUM_4
#define PIN_SD_HOST_MOSI        GPIO_NUM_5
#define PIN_SD_HOST_MISO        GPIO_NUM_6
#define PIN_SD_HOST_CS          GPIO_NUM_7
#define PIN_CARD_DETECT         GPIO_NUM_15

#undef PIN_UART0_RX
#undef PIN_UART0_TX
#undef PIN_UART1_RX
#undef PIN_UART1_TX
#undef PIN_UART2_RX
#undef PIN_UART2_TX
#define PIN_UART0_RX            GPIO_NUM_NC
#define PIN_UART0_TX            GPIO_NUM_NC
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

#undef PIN_BUTTON_B
#undef PIN_BUTTON_C
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

#undef PIN_LED_WIFI
#undef PIN_LED_BUS
#undef PIN_LED_BT
#define PIN_LED_WIFI            GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_BT              GPIO_NUM_NC

#endif /* PINMAP_ESP32S3 */
