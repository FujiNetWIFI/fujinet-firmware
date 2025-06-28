#ifdef PINMAP_COCO_ESP32S3

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
#define PIN_UART1_RX            GPIO_NUM_18
#define PIN_UART1_TX            GPIO_NUM_17
#define PIN_UART2_RX            GPIO_NUM_18
#define PIN_UART2_TX            GPIO_NUM_17

/* Buttons */
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_NC // led.cpp
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_BT              GPIO_NUM_NC

/* Coco */
#define PIN_CASS_MOTOR          GPIO_NUM_NC // Second motor pin is tied to +3V
#define PIN_CASS_DATA_IN        GPIO_NUM_NC
#define PIN_CASS_DATA_OUT       GPIO_NUM_NC
#define PIN_CD                  GPIO_NUM_NC // same as atari PROC
#define PIN_EPROM_A14           GPIO_NUM_NC // Used to set the serial baud rate
#define PIN_EPROM_A15           GPIO_NUM_NC // based on the HDB-DOS image selected

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC

#include "coco-common.h"
#include "common.h"

#endif /* PINMAP_COCO_ESP32S3 */

