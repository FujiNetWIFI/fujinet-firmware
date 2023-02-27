/* FujiLoaf REV0 */
#ifndef PINMAP_IEC_H
#define PINMAP_IEC_H

/* SD Card */
#define PIN_CARD_DETECT 35 // fnSystem.h
#define PIN_CARD_DETECT_FIX 35 // fnSystem.h

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
#define PIN_BUTTON_C 36

/* LEDs */
#define PIN_LED_WIFI 2 // led.cpp
#define PIN_LED_BUS 12 // 4 FN
#define PIN_LED_BT -1 // No BT LED

/* LED Strip NEW */
#define NUM_LEDS            5
#define DATA_PIN_1          4
#define BRIGHTNESS          125
#define LED_TYPE            WS2812B
#define COLOR_ORDER         RGB

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

// Reset line is available
#define IEC_HAS_RESET

#define PIN_IEC_RESET       GPIO_NUM_14
#define PIN_IEC_ATN         GPIO_NUM_32
#define PIN_IEC_CLK_IN		GPIO_NUM_33
#define PIN_IEC_CLK_OUT	    GPIO_NUM_33
#define PIN_IEC_DATA_IN    	GPIO_NUM_26
#define PIN_IEC_DATA_OUT   	GPIO_NUM_26
#define PIN_IEC_SRQ			GPIO_NUM_27 // FujiLoaf

/* Modem/Parallel Switch */
#define PIN_MDMPAR_SW1       2  // High = Modem enabled
#define PIN_MDMPAR_SW2       15 // High = UP9600 enabled

/* I2C GPIO Expander */
#define PIN_GPIOX_SDA         GPIO_NUM_21
#define PIN_GPIOX_SCL         GPIO_NUM_22
#define PIN_GPIOX_INT         GPIO_NUM_34
//#define GPIOX_ADDRESS     0x20  // PCF8575
#define GPIOX_ADDRESS     0x24  // PCA9673
//#define GPIOX_SPEED       400   // PCF8575 - 400Khz
#define GPIOX_SPEED       1000  // PCA9673 - 1000Khz / 1Mhz

#endif // PINMAP_IEC
