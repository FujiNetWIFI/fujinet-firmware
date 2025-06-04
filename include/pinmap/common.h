#ifndef PINMAP_COMMON_H
#define PINMAP_COMMON_H

/* SD Card */
// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef JTAG
#ifndef PIN_CARD_DETECT
#define PIN_CARD_DETECT         GPIO_NUM_15                    // use count: 11
#endif /* PIN_CARD_DETECT */
#ifndef PIN_CARD_DETECT_FIX
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15                    // use count: 17
#endif /* PIN_CARD_DETECT_FIX */
#endif /* JTAG */

#ifndef PIN_SD_HOST_CS
#define PIN_SD_HOST_CS          GPIO_NUM_5                     // use count: 16
#endif /* PIN_SD_HOST_CS */
#ifndef PIN_SD_HOST_MISO
#define PIN_SD_HOST_MISO        GPIO_NUM_19                    // use count: 20
#endif /* PIN_SD_HOST_MISO */
#ifndef PIN_SD_HOST_MOSI
#define PIN_SD_HOST_MOSI        GPIO_NUM_23                    // use count: 20
#endif /* PIN_SD_HOST_MOSI */
#ifndef PIN_SD_HOST_SCK
#define PIN_SD_HOST_SCK         GPIO_NUM_18                    // use count: 20
#endif /* PIN_SD_HOST_SCK */

/* UART */
#ifndef PIN_UART0_RX
#define PIN_UART0_RX            GPIO_NUM_3                     // use count: 20
#endif /* PIN_UART0_RX */
#ifndef PIN_UART0_TX
#define PIN_UART0_TX            GPIO_NUM_1                     // use count: 20
#endif /* PIN_UART0_TX */
#ifndef PIN_UART1_RX
#define PIN_UART1_RX            GPIO_NUM_9                     // use count: 14
#endif /* PIN_UART1_RX */
#ifndef PIN_UART1_TX
#define PIN_UART1_TX            GPIO_NUM_10                    // use count: 14
#endif /* PIN_UART1_TX */
#ifndef PIN_UART2_RX
#define PIN_UART2_RX            GPIO_NUM_33                    // use count: 15
#endif /* PIN_UART2_RX */
#ifndef PIN_UART2_TX
#define PIN_UART2_TX            GPIO_NUM_21                    // use count: 17
#endif /* PIN_UART2_TX */

/* Buttons */
#ifndef PIN_BUTTON_A
#define PIN_BUTTON_A            GPIO_NUM_0                     // use count: 22
#endif /* PIN_BUTTON_A */
#ifndef PIN_BUTTON_B
#define PIN_BUTTON_B            GPIO_NUM_NC                    // use count: 20
#endif /* PIN_BUTTON_B */
#ifndef PIN_BUTTON_C
#define PIN_BUTTON_C            GPIO_NUM_14                    // use count: 11
#endif /* PIN_BUTTON_C */

/* LEDs */
#ifndef PIN_LED_WIFI
#define PIN_LED_WIFI            GPIO_NUM_2                     // use count: 17
#endif /* PIN_LED_WIFI */
#ifndef PIN_LED_BUS
#define PIN_LED_BUS             GPIO_NUM_12                    // use count: 10
#endif /* PIN_LED_BUS */

// pins 12-15 are used to interface with the JTAG debugger
// so leave them alone if we're using JTAG
#ifndef PIN_LED_BT
#ifndef JTAG
#define PIN_LED_BT              GPIO_NUM_13
#else
#define PIN_LED_BT              GPIO_NUM_4
#endif
#endif /* PIN_LED_BT */

/* Audio Output */
#ifndef PIN_DAC1
#define PIN_DAC1                GPIO_NUM_25                    // use count: 18
#endif /* PIN_DAC1 */

#endif /* PINMAP_COMMON_H */
