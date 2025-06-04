/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_RS232_S3

/* SD Card - fnFsSD.cpp */
/* Card detect pin on RS232-REV1 goes high when card inserted */
#define CARD_DETECT_HIGH        1
#define PIN_CARD_DETECT         GPIO_NUM_9 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_9 // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_10  // fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_13
#define PIN_SD_HOST_MOSI        GPIO_NUM_11
#define PIN_SD_HOST_SCK         GPIO_NUM_21

/* UART */
#define PIN_UART0_RX            GPIO_NUM_44  // USB to Serial secondary
#define PIN_UART0_TX            GPIO_NUM_43  // USB to Serial secondary

/* Buttons - keys.cpp */
#define PIN_BUTTON_C            GPIO_NUM_39 // Safe reset

/* LEDs - leds.cpp */
#define PIN_LED_WIFI            GPIO_NUM_14
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* Audio Output - samlib.h */
#define PIN_DAC1                GPIO_NUM_21

/* RS232 Pins */
#define PIN_UART1_RX            GPIO_NUM_41 // (IN) ESP32S3 RX
#define PIN_UART1_TX            GPIO_NUM_42 // (OUT) ESP32S3 TX
#define PIN_RS232_RI            GPIO_NUM_16 // (OUT) Ring Indicator
#define PIN_RS232_DCD           GPIO_NUM_4  // (OUT) Data Carrier Detect
#define PIN_RS232_RTS           GPIO_NUM_15 // (IN) Request to Send
#define PIN_RS232_CTS           GPIO_NUM_7  // (OUT) Clear to Send
#define PIN_RS232_DTR           GPIO_NUM_6  // (IN) Data Terminal Ready
#define PIN_RS232_DSR           GPIO_NUM_5  // (OUT) Data Set Ready
#define PIN_RS232_INVALID       GPIO_NUM_18 // (IN) RS232 Invalid Data (from TRS3238E)

#include "common.h"

#endif /* PINMAP_RS232_REV0 */
