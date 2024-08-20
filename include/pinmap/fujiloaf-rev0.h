/* FujiLoaf REV0 */
#ifndef PINMAP_FUJILOAF_REV0_H
#define PINMAP_FUJILOAF_REV0_H

#include "common.h"

#ifdef PINMAP_FUJILOAF_REV0
#undef PIN_CARD_DETECT
#undef PIN_CARD_DETECT_FIX
#define PIN_CARD_DETECT         GPIO_NUM_35 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_35 // fnSystem.h

#undef PIN_BUTTON_B
#undef PIN_BUTTON_C
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B
#define PIN_BUTTON_C            GPIO_NUM_36

#undef PIN_LED_BT
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* LED Strip NEW */
#define LEDSTRIP_DATA_PIN       GPIO_NUM_4
#define LEDSTRIP_COUNT          5
#define LEDSTRIP_BRIGHTNESS     15 // max mA the LED can use determines brightness
#define LEDSTRIP_TYPE           WS2812B
#define LEDSTRIP_RGB_ORDER      GRB
// LED order on the strip starting with 0
#define LEDSTRIP_WIFI_NUM       0
#define LEDSTRIP_BUS_NUM        4
#define LEDSTRIP_BT_NUM         2

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_25 // samlib.h

// Reset line is available
#define IEC_HAS_RESET

#define PIN_IEC_RESET           GPIO_NUM_14
#define PIN_IEC_ATN             GPIO_NUM_32
#define PIN_IEC_CLK_IN          GPIO_NUM_33
#define PIN_IEC_CLK_OUT         GPIO_NUM_33
#define PIN_IEC_DATA_IN         GPIO_NUM_26
#define PIN_IEC_DATA_OUT        GPIO_NUM_26
#define PIN_IEC_SRQ             GPIO_NUM_27 // FujiLoaf
// GND - Be sure to connect GND of the IEC cable to GND on the ESP module

/* Modem/Parallel Switch */
#define PIN_MODEM_ENABLE        GPIO_NUM_2  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_15 // High = UP9600 enabled

/* I2C GPIO Expander */
#define PIN_GPIOX_SDA           GPIO_NUM_21
#define PIN_GPIOX_SCL           GPIO_NUM_22
#define PIN_GPIOX_INT           GPIO_NUM_34
//#define GPIOX_ADDRESS           0x20  // PCF8575
#define GPIOX_ADDRESS           0x24  // PCA9673
//#define GPIOX_SPEED             400   // PCF8575 - 400Khz
#define GPIOX_SPEED             1000  // PCA9673 - 1000Khz / 1Mhz

#endif // PINMAP_FUJILOAF_REV0
#endif // PINMAP_FUJILOAF_REV0_H
