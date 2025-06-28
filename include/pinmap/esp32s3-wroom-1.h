#ifdef PINMAP_ESP32S3_WROOM_1

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_14 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_9 // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_10  // fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_13
#define PIN_SD_HOST_MOSI        GPIO_NUM_11
#define PIN_SD_HOST_SCK         GPIO_NUM_12

/* UART */
#define PIN_UART0_RX            GPIO_NUM_44  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_43
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_18
#define PIN_UART2_TX            GPIO_NUM_17

/* Buttons */
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_5 // led.cpp
#define PIN_LED_BUS             GPIO_NUM_4

// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#if !defined(JTAG)
#define PIN_LED_BT              GPIO_NUM_3
#else
#define PIN_LED_BT              GPIO_NUM_NC
#endif

/* C64 IEC Pins */
// Reset line is available
// #define IEC_HAS_RESET
// #define PIN_IEC_RESET           GPIO_NUM_14
#define PIN_IEC_ATN             GPIO_NUM_38
#define PIN_IEC_CLK_IN          GPIO_NUM_39
#define PIN_IEC_CLK_OUT         GPIO_NUM_40
#define PIN_IEC_DATA_IN         GPIO_NUM_41
#define PIN_IEC_DATA_OUT        GPIO_NUM_42
#define PIN_IEC_SRQ             GPIO_NUM_2 // FujiLoaf

/* Atari SIO Pins */
#define PIN_INT                 GPIO_NUM_38 // sio.h
#define PIN_PROC                GPIO_NUM_39
#define PIN_CKO                 GPIO_NUM_40
#define PIN_CKI                 GPIO_NUM_41
#define PIN_MTR                 GPIO_NUM_42
#define PIN_CMD                 GPIO_NUM_2

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_21 //GPIO_NUM_NC

#include "iec-common.h"
#include "atari-common.h"
#include "common.h"

#endif /* PINMAP_ESP32S3_WROOM_1 */

