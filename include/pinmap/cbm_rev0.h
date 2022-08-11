/* FujiApple Rev0 for C64 Hardware Pin Mapping */
#ifdef PINMAP_REV0_CBM

/* SD Card */
#define PIN_CARD_DETECT 12 // fnSystem.h
#define PIN_CARD_DETECT_FIX 15 // fnSystem.h

#define PIN_SD_HOST_CS GPIO_NUM_5
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
#define PIN_LED_BUS 12 // 4 FN
#define PIN_LED_BT -1 // No BT LED

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define IEC_SPLIT_LINES

// Line values are inverted (7406 Hex Inverter Buffer)
//#define IEC_INVERTED_LINES

// Reset line is available
#define IEC_HAS_RESET

#define PIN_IEC_RESET       GPIO_NUM_21
#define PIN_IEC_ATN         GPIO_NUM_22
#define PIN_IEC_CLK_IN		GPIO_NUM_33
#define PIN_IEC_CLK_OUT	    GPIO_NUM_33
#define PIN_IEC_DATA_IN    	GPIO_NUM_32
#define PIN_IEC_DATA_OUT   	GPIO_NUM_32
#define PIN_IEC_SRQ			GPIO_NUM_26

#endif /* PINMAP_REV0_CBM */