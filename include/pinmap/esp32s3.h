#ifdef PINMAP_ESP32S3

#define PIN_SD_HOST_SCK         GPIO_NUM_4
#define PIN_SD_HOST_MOSI        GPIO_NUM_5
#define PIN_SD_HOST_MISO        GPIO_NUM_6
#define PIN_SD_HOST_CS          GPIO_NUM_7

#define PIN_UART0_RX            GPIO_NUM_NC
#define PIN_UART0_TX            GPIO_NUM_NC
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

#define PIN_BUTTON_C            GPIO_NUM_NC

#define PIN_LED_WIFI            GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_BT              GPIO_NUM_NC

#include "common.h"

#endif /* PINMAP_ESP32S3 */
