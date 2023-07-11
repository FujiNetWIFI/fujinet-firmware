/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_RC2014SPI_REV0

/* SD Card - fnFsSD.cpp */
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15 // fnSystem.h
#define PIN_SD_HOST_CS          GPIO_NUM_5  // fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_19
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#define PIN_SD_HOST_SCK         GPIO_NUM_18

/* UART - fnuart.cpp */
#define PIN_UART0_RX            GPIO_NUM_3  // USB Serial
#define PIN_UART0_TX            GPIO_NUM_1  // USB Serial
#define PIN_UART1_RX            GPIO_NUM_33 // not connected
#define PIN_UART1_TX            GPIO_NUM_21 // not connected
#define PIN_UART2_RX            GPIO_NUM_13 // not connected - RC2014 SIO
#define PIN_UART2_TX            GPIO_NUM_21 // not connected - RC2014 SIO


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

/* SPI BUS interface */
#define PIN_BUS_DEVICE_CS       GPIO_NUM_35 // (input only)
#define PIN_BUS_DEVICE_MISO     GPIO_NUM_26
#define PIN_BUS_DEVICE_MOSI     GPIO_NUM_25
#define PIN_BUS_DEVICE_SCK      GPIO_NUM_33

#define PIN_CMD_RDY             GPIO_NUM_27
#define PIN_CMD                 GPIO_NUM_32
#define PIN_DATA                GPIO_NUM_34 // input only
#define PIN_PROCEED             GPIO_NUM_4

#endif /* PINMAP_RC2014SPI_REV0 */
