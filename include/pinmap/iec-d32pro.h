
#ifndef PINMAP_LOLIN_D32_PRO_H
#define PINMAP_LOLIN_D32_PRO_H

#ifdef PINMAP_IEC_D32PRO

/* SD Card */
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef JTAG
#define PIN_CARD_DETECT 12 // fnSystem.h
#define PIN_CARD_DETECT_FIX 15 // fnSystem.h
#endif

#define PIN_SD_HOST_CS GPIO_NUM_4 // LOLIN D32 Pro
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
#define PIN_BUTTON_B -1
#define PIN_BUTTON_C 14

/* LEDs */
#define PIN_LED_WIFI 2 // led.cpp
#define PIN_LED_BUS 5 // 4 FN
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef JTAG
#define PIN_LED_BT 13
#else
#define PIN_LED_BT 5 // LOLIN D32 PRO
#endif

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* Commodore IEC Pins */
#define IEC_HAS_RESET       // Reset line is available

#define PIN_IEC_RESET       GPIO_NUM_34
#define PIN_IEC_ATN         GPIO_NUM_32
#define PIN_IEC_CLK_IN		GPIO_NUM_33
#define PIN_IEC_CLK_OUT	    GPIO_NUM_33
#define PIN_IEC_DATA_IN    	GPIO_NUM_25
#define PIN_IEC_DATA_OUT   	GPIO_NUM_25
#define PIN_IEC_SRQ			GPIO_NUM_26

#endif // PINMAP_IEC_D32PRO
#endif // PINMAP_LOLIN_D32_PRO_H