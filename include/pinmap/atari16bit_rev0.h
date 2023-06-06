/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_ATARI16BIT_REV0

/* SD Card - fnFsSD.cpp */
#define PIN_CARD_DETECT -1 // fnSystem.h
#define PIN_CARD_DETECT_FIX -1 // fnSystem.h
#define PIN_SD_HOST_CS GPIO_NUM_15 //fnFsSD.cpp
#define PIN_SD_HOST_MISO GPIO_NUM_12
#define PIN_SD_HOST_MOSI GPIO_NUM_13
#define PIN_SD_HOST_SCK GPIO_NUM_14

/* UART - fnuart.cpp */
#define PIN_UART0_RX 3  // USB Serial
#define PIN_UART0_TX 1  // USB Serial
#define PIN_UART1_RX 26 // Debug Pico
#define PIN_UART1_TX 25 // Debug Pico
//#define PIN_UART2_RX 33
//#define PIN_UART2_TX 21

/* Buttons - keys.cpp */
#define PIN_BUTTON_A 0  // Button 0 on DEVKITC-VE
#define PIN_BUTTON_B -1 // No Button B
#define PIN_BUTTON_C 39 // Safe reset

/* LEDs - leds.cpp */
#define PIN_LED_WIFI    -1
#define PIN_LED_BUS     -1
#define PIN_LED_BT      -1 // No BT LED

/* Audio Output - samlib.h */
#define PIN_DAC1 -1 // not connected

/* ACSI SPI Pins */
#define PIN_ACSI_SPI_CS 5
#define PIN_ACSI_SPI_MISO 19
#define PIN_ACSI_SPI_MOSI 23
#define PIN_ACSI_SPI_CLK 18
#endif /* PINMAP_ATARI16BIT_REV0 */
