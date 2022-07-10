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
#define PIN_UART2_RX 23
#define PIN_UART2_TX 22
#else
#define PIN_UART2_RX 33
#define PIN_UART2_TX 21
#endif /* THOMS_GHETTO_ASS_BREADBOARD*/


/* Buttons */
#define PIN_BUTTON_A 0 // keys.cpp
#if defined(BUILD_LYNX) || defined(BUILD_APPLE)
// disable button B for these platforms
#define PIN_BUTTON_B -1
#else
#define PIN_BUTTON_B 34
#endif
#define PIN_BUTTON_C 14

/* LEDs */
#define PIN_LED_WIFI 2 // led.cpp
#if defined(BUILD_APPLE) && !defined(USE_ATARI_FN10)
#define PIN_LED_BUS 12 // FujiApple
#else
#define PIN_LED_BUS 4
#endif
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#if defined(BUILD_APPLE) && !defined(USE_ATARI_FN10)
#define PIN_LED_BT -1
#elif !defined(JTAG)
#define PIN_LED_BT 13
#else
#define PIN_LED_BT 4
#endif

/* Atari SIO Pins */
#define PIN_INT 26 // sio.h
#define PIN_PROC 22
#define PIN_CKO 32
#define PIN_CKI 27
#define PIN_MTR 36
#define PIN_CMD 39

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define SPLIT_LINES

// CLK_OUT & DATA_OUT are inverted
#define INVERTED_LINES	false

// CBM IEC Serial Port
#define IEC_PIN_ATN			22      // PROC
#define IEC_PIN_CLK			27      // CKI
#define IEC_PIN_DATA		21      // DI
#define IEC_PIN_SRQ			26      // INT
#define IEC_PIN_RESET       39      // CMD

#ifdef SPLIT_LINES
#define IEC_PIN_CLK_OUT		32      // CKO
#define IEC_PIN_DATA_OUT	33      // DO
#endif

/* Pins for AdamNet */
#define PIN_ADAMNET_RESET   26


/* Pins for Adam USB */
#define PIN_USB_DP          27      // D+
#define PIN_USB_DM          32      // D-

// Apple II IWM pin assignments

// Atari Lynx Pin assignments
#define PIN_COMLYNX_RESET   26

#endif