/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_H89

/* SD Card - fnFsSD.cpp */
#define PIN_CARD_DETECT 12 // fnSystem.h
#define PIN_CARD_DETECT_FIX 15 // fnSystem.h
#define PIN_SD_HOST_CS GPIO_NUM_5 //fnFsSD.cpp
#define PIN_SD_HOST_MISO GPIO_NUM_19
#define PIN_SD_HOST_MOSI GPIO_NUM_23
#define PIN_SD_HOST_SCK GPIO_NUM_18

/* UART - fnuart.cpp */
#define PIN_UART0_RX 3  // USB Serial
#define PIN_UART0_TX 1  // USB Serial
#define PIN_UART1_RX 33 // not connected
#define PIN_UART1_TX 21 // not connected
#define PIN_UART2_RX 13 // not connected - RC2014 SIO
#define PIN_UART2_TX 21 // not connected - RC2014 SIO


/* Buttons - keys.cpp */
#define PIN_BUTTON_A 0  // Button 0 on DEVKITC-VE
#define PIN_BUTTON_B -1 // No Button B
#define PIN_BUTTON_C 39 // Safe reset

/* LEDs - leds.cpp */
#define PIN_LED_WIFI    14
#define PIN_LED_BUS     12
#define PIN_LED_BT      -1 // No BT LED

/* Audio Output - samlib.h */
#define PIN_DAC1 25 // not connected

/* SPI BUS interface */
#define PIN_BUS_DEVICE_CS GPIO_NUM_35     // (input only)
#define PIN_BUS_DEVICE_MISO GPIO_NUM_26
#define PIN_BUS_DEVICE_MOSI GPIO_NUM_25
#define PIN_BUS_DEVICE_SCK GPIO_NUM_33

#define PIN_CMD_RDY    GPIO_NUM_27
#define PIN_CMD        32
#define PIN_DATA       34                 // input only
#define PIN_PROCEED    4

#endif /* PINMAP_RC2014SPI_REV0 */
