/* FujiLoaf REV0 */
#ifdef PINMAP_IEC

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_35 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_35 // fnSystem.h

/* Buttons */
#define PIN_BUTTON_C            GPIO_NUM_36

/* LEDs */
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED
#define PIN_IEC_RESET           GPIO_NUM_14
#define PIN_IEC_DATA_IN         GPIO_NUM_26
#define PIN_IEC_DATA_OUT        GPIO_NUM_26

/* I2C GPIO Expander */
#define PIN_GPIOX_SDA           GPIO_NUM_21
#define PIN_GPIOX_SCL           GPIO_NUM_22
#define PIN_GPIOX_INT           GPIO_NUM_34
//#define GPIOX_ADDRESS           0x20  // PCF8575
#define GPIOX_ADDRESS           0x24  // PCA9673
//#define GPIOX_SPEED             400   // PCF8575 - 400Khz
#define GPIOX_SPEED             1000  // PCA9673 - 1000Khz / 1Mhz

#include "iec-common.h"
#include "common.h"

#endif // PINMAP_IEC
