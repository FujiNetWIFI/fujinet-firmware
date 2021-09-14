/* FujiNet Hardware Pin Mapping */
#ifndef PINMAP_H
#define PINMAP_H

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
#define PIN_ADAMNET_RX 22
#define PIN_ADAMNET_TX 21

/* Buttons */
#define PIN_BUTTON_A 0 // keys.cpp
#define PIN_BUTTON_B 34
#define PIN_BUTTON_C 14

/* LEDs */
#define PIN_LED_WIFI 2 // led.cpp
#define PIN_LED_SIO 4
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef JTAG 
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

/* Dummy pins for AdamNet */
#define PIN_RX_DUMMY 27
#define PIN_TX_DUMMY 36

#endif