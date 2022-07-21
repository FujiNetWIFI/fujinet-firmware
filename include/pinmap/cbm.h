/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_CBM

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
#define PIN_BUTTON_B 34
#define PIN_BUTTON_C 14

/* LEDs */
#define PIN_LED_WIFI 2 // led.cpp
#define PIN_LED_BUS 4
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#if !defined(JTAG)
#define PIN_LED_BT 13
#else
#define PIN_LED_BT 4
#endif

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define SPLIT_LINES

// CLK_OUT & DATA_OUT are inverted
#define INVERTED_LINES

// CBM IEC Serial Port
#define PIN_IEC_ATN			22      // SIO 9  - PROC - Proceed
#define PIN_IEC_CLK_IN  	27      // SIO 1  - CKI  - Clock Input
#define PIN_IEC_DATA_IN		21      // SIO 3  - DI   - Data Input
#define PIN_IEC_SRQ			26      // SIO 13 - INT  - Interrupt
#define PIN_IEC_RESET       39      // SIO 7  - CMD  - Command

#ifndef SPLIT_LINES
#define PIN_IEC_CLK_OUT		27      // SIO 1  - CKI  - Clock Input
#define PIN_IEC_DATA_OUT	21      // SIO 3  - DI   - Data Input
#else
#define PIN_IEC_CLK_OUT		32      // SIO 2  - CKO  - Clock Output
#define PIN_IEC_DATA_OUT	33      // SIO 5  - DO   - Data Output
#endif

#endif /* PINMAP_CBM */
