/* FujiNet Hardware Pin Mapping */
#ifndef PINMAP_H
#define PINMAP_H

/* SD Card */
#define PIN_CARD_DETECT 12 // fnSystem.h
#define PIN_CARD_DETECT_FIX 15 // fnSystem.h
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define PIN_SD_HOST_MISO 19
#define PIN_SD_HOST_MOSI 23
#define PIN_SD_HOST_SCK  18
#define PIN_SD_HOST_CS   GPIO_NUM_5
#else
#define PIN_SD_HOST_CS GPIO_NUM_5 //fnFsSD.cpp
#define PIN_SD_HOST_MISO GPIO_NUM_19
#define PIN_SD_HOST_MOSI GPIO_NUM_23
#define PIN_SD_HOST_SCK GPIO_NUM_18
#endif

/* UART */
#define PIN_UART0_RX 3 // fnUART.cpp
#define PIN_UART0_TX 1
#define PIN_UART1_RX 9
#define PIN_UART1_TX 10
#ifdef THOMS_GHETTO_ASS_ADAM_BREADBOARD 
#define PIN_UART2_RX 22
#else
#define PIN_UART2_RX 33
#endif /* THOMS_GHETTO_ASS_BREADBOARD*/
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
#ifndef JTAG 
#define PIN_LED_BT 13
#else
#define PIN_LED_BT 4
#endif

/* Atari SIO Pins */
#define PIN_SIO_INT         26      // SIO 13 - INT  - Interrupt
#define PIN_SIO_PROC        22      // SIO 9  - PROC - Proceed
#define PIN_SIO_CKO         32      // SIO 2  - CKO  - Clock Output
#define PIN_SIO_CKI         27      // SIO 1  - CKI  - Clock Input
#define PIN_SIO_MTR         36      // SIO 8  - MTR  - Motor Control
#define PIN_SIO_CMD         39      // SIO 7  - CMD  - Command

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define SPLIT_LINES

// CLK_OUT & DATA_OUT are inverted
#define INVERTED_LINES	false

// CBM IEC Serial Port
#define PIN_IEC_ATN			22      // SIO 9  - PROC - Proceed
#define PIN_IEC_CLK			27      // SIO 1  - CKI  - Clock Input
#define PIN_IEC_DATA		21      // SIO 3  - DI   - Data Input
#define PIN_IEC_SRQ			26      // SIO 13 - INT  - Interrupt
#define PIN_IEC_RESET       39      // SIO 7  - CMD  - Command

#ifdef SPLIT_LINES
#define PIN_IEC_CLK_OUT		32      // SIO 2  - CKO  - Clock Output
#define PIN_IEC_DATA_OUT	33      // SIO 5  - DO   - Data Output
#endif

/* Pins for AdamNet */
#define PIN_ADAMNET_RESET   26      // SIO 13 - INT  - Interrupt

/* Pins for Adam USB */
#define PIN_USB_DP          27      // D+  // SIO 1  - CKI  - Clock Input
#define PIN_USB_DM          32      // D-  // SIO 2  - CKO  - Clock Output

// Apple II IWM pin assignments


#endif