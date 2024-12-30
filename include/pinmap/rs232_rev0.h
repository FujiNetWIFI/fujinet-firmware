/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_RS232_REV0

/* SD Card - fnFsSD.cpp */
#define PIN_CARD_DETECT         GPIO_NUM_15 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15 // fnSystem.h
#define PIN_SD_HOST_CS          GPIO_NUM_5  // fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_19
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#define PIN_SD_HOST_SCK         GPIO_NUM_18

/* UART - fnuart.cpp */
#define PIN_UART0_RX            GPIO_NUM_3  // USB Serial
#define PIN_UART0_TX            GPIO_NUM_1  // USB Serial
#define PIN_UART1_RX            GPIO_NUM_13 // RS232
#define PIN_UART1_TX            GPIO_NUM_21 // RS232

/* Buttons - keys.cpp */
#define PIN_BUTTON_A            GPIO_NUM_0  // Button 0 on DEVKITC-VE
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B
#define PIN_BUTTON_C            GPIO_NUM_39 // Safe reset

/* LEDs - leds.cpp */
#define PIN_LED_WIFI            GPIO_NUM_14
#define PIN_LED_BUS             GPIO_NUM_12
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* Audio Output - samlib.h */
#define PIN_DAC1                GPIO_NUM_25 // not connected

/* RS232 Pins */
#define PIN_RS232_RI            GPIO_NUM_32 // (OUT) Ring Indicator
#define PIN_RS232_DCD           GPIO_NUM_22 // (OUT) Data Carrier Detect
#define PIN_RS232_RTS           GPIO_NUM_33 // (IN) Request to Send
#define PIN_RS232_CTS           GPIO_NUM_26 // (OUT) Clear to Send
#define PIN_RS232_DTR           GPIO_NUM_27 // (IN) Data Terminal Ready
#define PIN_RS232_DSR           GPIO_NUM_4  // (OUT) Data Set Ready
#define PIN_RS232_INVALID       GPIO_NUM_36 // (IN) RS232 Invalid Data (from TRS3238E)

#endif /* PINMAP_RS232_REV0 */
