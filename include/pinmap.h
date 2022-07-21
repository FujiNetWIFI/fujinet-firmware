/* FujiNet Hardware Pin Mapping */
#ifndef PINMAP_H
#define PINMAP_H

#ifdef PINMAP_ATARIV1
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

/* Atari SIO Pins */
#define PIN_INT 26 // sio.h
#define PIN_PROC 22
#define PIN_CKO 32
#define PIN_CKI 27
#define PIN_MTR 36
#define PIN_CMD 39

/* Audio Output */
#define PIN_DAC1 25 // samlib.h
#endif /* PINMAP_ATARIV1 */

#ifdef PINMAP_ADAMV1
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

/* Atari SIO Pins */
#define PIN_INT 26 // sio.h
#define PIN_PROC 22
#define PIN_CKO 32
#define PIN_CKI 27
#define PIN_MTR 36
#define PIN_CMD 39

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* Pins for AdamNet */
#define PIN_ADAMNET_RESET   26

/* Pins for Adam USB */
#define PIN_USB_DP          27      // D+
#define PIN_USB_DM          32      // D-
#endif /* PINMAP_ADAMV1 */

#ifdef PINMAP_A2_REV0
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
#endif /* PINMAP_A2_REV0 */

#ifdef PINMAP_A2_FN10
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
#define PIN_LED_BT 13

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* IWM Bus Pins */
//      SP BUS     GPIO       SIO               LA (with SIO-10IDC cable)
//      ---------  ----     -----------------   -------------------------
#define SP_WRPROT   27
#define SP_ACK      27      //  CLKIN     1     D0
#define SP_REQ      39
#define SP_PHI0     39      //  CMD       7     D4
#define SP_PHI1     22      //  PROC      9     D6
#define SP_PHI2     36      //  MOTOR     8     D5
#define SP_PHI3     26      //  INT       13    D7
#define SP_RDDATA   21      //  DATAIN    3     D2
#define SP_WRDATA   33      //  DATAOUT   5     D3
#define SP_ENABLE   32      //  CLKOUT    2     D1
#define SP_EXTRA    32      //  CLKOUT - used for debug/diagnosing - signals when WRDATA is sampled by ESP32
#endif /* PINMAP_A2_FN10 */

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

#ifdef PINMAP_ESP32S3
// ***** NOTE: UNTESTED ***** //
/* SD Card */
#define PIN_CARD_DETECT 12 // fnSystem.h
#define PIN_CARD_DETECT_FIX 15 // fnSystem.h
#define PIN_SD_HOST_MISO 19
#define PIN_SD_HOST_MOSI 23
#define PIN_SD_HOST_SCK  18
#define PIN_SD_HOST_CS   GPIO_NUM_5

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

/* Atari SIO Pins */
#define PIN_INT 26 // sio.h
#define PIN_PROC 22
#define PIN_CKO 32
#define PIN_CKI 27
#define PIN_MTR 36
#define PIN_CMD 39

/* Audio Output */
#define PIN_DAC1 25 // samlib.h
#endif /* PINMAP_ESP32S3 */

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
#endif /* PINMAP_H */
