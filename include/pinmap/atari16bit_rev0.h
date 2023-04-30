/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_ATARI16BIT_REV0

/* SD Card - fnFsSD.cpp */
#define PIN_CARD_DETECT -1 // fnSystem.h
#define PIN_CARD_DETECT_FIX -1 // fnSystem.h
#define PIN_SD_HOST_CS GPIO_NUM_5 //fnFsSD.cpp
#define PIN_SD_HOST_MISO GPIO_NUM_19
#define PIN_SD_HOST_MOSI GPIO_NUM_23
#define PIN_SD_HOST_SCK GPIO_NUM_18

/* UART - fnuart.cpp */
#define PIN_UART0_RX 3  // USB Serial
#define PIN_UART0_TX 1  // USB Serial
#define PIN_UART1_RX -1 // RS232
#define PIN_UART1_TX -1 // RS232
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

/* ACSI data Pins */
#define PIN_ACSI_DO     13
#define PIN_ACSI_D1     14
#define PIN_ACSI_D2     15
#define PIN_ACSI_D3     12
#define PIN_ACSI_D4     32
#define PIN_ACSI_D5     18
#define PIN_ACSI_D6     19
#define PIN_ACSI_D7     21
/* ACSI control pins */
#define PIN_ACSI_RESET  22    
#define PIN_ACSI_IRQ    5
#define PIN_ACSI_RW     25
#define PIN_ACSI_ACK    27
#define PIN_ACSI_DRQ    33
#define PIN_ACSI_A1     26
#define PIN_ACSI_CS     34
#endif /* PINMAP_ATARI16BIT_REV0 */
