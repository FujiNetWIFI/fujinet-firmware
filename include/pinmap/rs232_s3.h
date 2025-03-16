/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_RS232_S3

/* SD Card - fnFsSD.cpp */
#define PIN_CARD_DETECT         GPIO_NUM_9 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_9 // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_10  // fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_13
#define PIN_SD_HOST_MOSI        GPIO_NUM_11
#define PIN_SD_HOST_SCK         GPIO_NUM_12

/* UART */
#define PIN_UART0_RX            GPIO_NUM_44  // USB to Serial secondary
#define PIN_UART0_TX            GPIO_NUM_43  // USB to Serial secondary

/* Buttons - keys.cpp */
#define PIN_BUTTON_A            GPIO_NUM_0  // Button 0 on DEVKITC-VE
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B
#define PIN_BUTTON_C            GPIO_NUM_39 // Safe reset

/* LEDs - leds.cpp */
#define PIN_LED_WIFI            GPIO_NUM_14
#define PIN_LED_BUS             GPIO_NUM_12
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* Audio Output - samlib.h */
#define PIN_DAC1                GPIO_NUM_21

/* RS232 Pins */
#define PIN_UART1_RX            GPIO_NUM_41 // RS232 35
#define PIN_UART1_TX            GPIO_NUM_42 // RS232 36
#define PIN_RS232_RI            GPIO_NUM_16 // (OUT) Ring Indicator
#define PIN_RS232_DCD           GPIO_NUM_NC // (OUT) Data Carrier Detect
#define PIN_RS232_RTS           GPIO_NUM_NC // (IN) Request to Send
#define PIN_RS232_CTS           GPIO_NUM_NC // (OUT) Clear to Send
#define PIN_RS232_DTR           GPIO_NUM_17 // (IN) Data Terminal Ready
#define PIN_RS232_DSR           GPIO_NUM_NC  // (OUT) Data Set Ready
#define PIN_RS232_INVALID       GPIO_NUM_NC // (IN) RS232 Invalid Data (from TRS3238E)

#endif /* PINMAP_RS232_REV0 */
