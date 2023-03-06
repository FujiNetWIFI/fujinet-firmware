/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_A2_REV0

/* SD Card */
#define PIN_CARD_DETECT 12 // fnSystem.h
#define PIN_CARD_DETECT_FIX 15 // fnSystem.h
#define PIN_SD_HOST_CS GPIO_NUM_5 //fnFsSD.cpp
#define PIN_SD_HOST_MISO GPIO_NUM_19
#ifdef MASTERIES_SPI_FIX
#define PIN_SD_HOST_MOSI GPIO_NUM_14
#else
#define PIN_SD_HOST_MOSI GPIO_NUM_23
#endif
#define PIN_SD_HOST_SCK GPIO_NUM_18

/* UART */
#define PIN_UART0_RX 3 // fnUART.cpp
#define PIN_UART0_TX 1
#define PIN_UART1_RX 9
#define PIN_UART1_TX 10
#define PIN_UART2_RX 33
#define PIN_UART2_TX 21

/* Buttons */
#define PIN_BUTTON_A 0 // keys.cpp
#define PIN_BUTTON_B -1 // No Button B
#define PIN_BUTTON_C 14

/* LEDs */
//#define PIN_LED_WIFI 2 // led.cpp
#define PIN_LED_WIFI -1 // led.cpp
#define PIN_LED_BUS 12
#define PIN_LED_BT -1 // No BT LED

/* LED Strip NEW */
#define NUM_LEDS            3
#define LED_DATA_PIN        2
#define LED_BRIGHTNESS      11 // max mA the LED can use determines brightness
#define LED_TYPE            WS2812B
#define RGB_ORDER           GRB
// LED order on the strip starting with 0
#define LED_WIFI_NUM        0
#define LED_BUS_NUM         2
#define LED_BT_NUM          1

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* IWM Bus Pins */
#define SP_REQ      32
#define SP_PHI0     32
#define SP_PHI1     33
#define SP_PHI2     34
#define SP_PHI3     35
#define SP_WRPROT   27
#define SP_ACK      27
#define SP_RDDATA   4 // tri-state gate enable line
#define SP_WRDATA   22
// TODO: go through each line and make sure the code is OK for each one before moving to next
#define SP_WREQ     26
#define SP_DRIVE1   36
#define SP_DRIVE2   21
#define SP_EN35     39
#define SP_HDSEL    13

#ifdef MASTERIES_SPI_FIX
#define SP_SPI_FIX_PIN  GPIO_NUM_23 // Pin to use for SmartPort SPI hardware mod Masteries edition
#else
#define SP_SPI_FIX_PIN  GPIO_NUM_14 // Pin to use for SmartPort SPI hardware mod FujiApple Rev0
#endif // MASTERIES_SPI_FIX

#define SP_EXTRA    SP_DRIVE2 // For extra debugging with logic analyzer
#endif /* PINMAP_A2_REV0 */
