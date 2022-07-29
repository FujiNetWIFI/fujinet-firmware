/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_LYNX

/* SD Card */
#define PIN_CARD_DETECT 12 // fnSystem.h
#define PIN_CARD_DETECT_FIX 15 // fnSystem.h
#define PIN_SD_HOST_CS GPIO_NUM_5 //fnFsSD.cpp
#define PIN_SD_HOST_MISO GPIO_NUM_19
#define PIN_SD_HOST_MOSI GPIO_NUM_23
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
#define PIN_LED_WIFI 2 // led.cpp
#define PIN_LED_BUS 12
#define PIN_LED_BT -1 // No BT LED

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* Atari Lynx Pin assignments */
#define PIN_COMLYNX_RESET   26

#endif /* PINMAP_LYNX */
