#ifdef PINMAP_ESP32S3_WROOM_1

#include "common.h"

#undef PIN_CARD_DETECT
#undef PIN_CARD_DETECT_FIX
#undef PIN_SD_HOST_CS
#undef PIN_SD_HOST_MISO
#undef PIN_SD_HOST_MOSI
#undef PIN_SD_HOST_SCK
#define PIN_CARD_DETECT         GPIO_NUM_14 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_9 // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_10  // fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_13
#define PIN_SD_HOST_MOSI        GPIO_NUM_11
#define PIN_SD_HOST_SCK         GPIO_NUM_12

#undef PIN_UART0_RX
#undef PIN_UART0_TX
#undef PIN_UART1_RX
#undef PIN_UART1_TX
#undef PIN_UART2_RX
#undef PIN_UART2_TX
#define PIN_UART0_RX            GPIO_NUM_44  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_43
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_18
#define PIN_UART2_TX            GPIO_NUM_17

/* Buttons */
//#define PIN_BUTTON_A            GPIO_NUM_45 // keys.cpp
//#define PIN_BUTTON_B            GPIO_NUM_47
//#define PIN_BUTTON_C            GPIO_NUM_48
#define PIN_BUTTON_A            GPIO_NUM_0
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

#undef PIN_LED_WIFI
#define PIN_LED_WIFI            GPIO_NUM_5 // led.cpp
#define PIN_LED_BUS             GPIO_NUM_4

#undef PIN_LED_BT
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#if !defined(JTAG)
#define PIN_LED_BT              GPIO_NUM_3
#else
#define PIN_LED_BT              GPIO_NUM_NC
#endif

#undef PIN_DAC1
#define PIN_DAC1                GPIO_NUM_NC

/* Atari SIO Pins */
#define PIN_INT                 GPIO_NUM_38 // sio.h
#define PIN_PROC                GPIO_NUM_39
#define PIN_CKO                 GPIO_NUM_40
#define PIN_CKI                 GPIO_NUM_41
#define PIN_MTR                 GPIO_NUM_42
#define PIN_CMD                 GPIO_NUM_2

#endif /* PINMAP_ESP32S3_WROOM_1 */

