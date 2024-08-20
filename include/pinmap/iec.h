/* FujiLoaf REV0 */
#ifdef PINMAP_IEC

#include "common.h"

#undef PIN_CARD_DETECT
#undef PIN_CARD_DETECT_FIX
#define PIN_CARD_DETECT         GPIO_NUM_35 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_35 // fnSystem.h

#undef PIN_BUTTON_B
#undef PIN_BUTTON_C
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B
#define PIN_BUTTON_C            GPIO_NUM_36

#undef PIN_LED_BUS
#undef PIN_LED_BT
#define PIN_LED_BUS             GPIO_NUM_12 // 4 FN
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

// Reset line is available
#define IEC_HAS_RESET

#define PIN_IEC_RESET           GPIO_NUM_14
#define PIN_IEC_ATN             GPIO_NUM_32
#define PIN_IEC_CLK_IN          GPIO_NUM_33
#define PIN_IEC_CLK_OUT         GPIO_NUM_33
#define PIN_IEC_DATA_IN         GPIO_NUM_26
#define PIN_IEC_DATA_OUT        GPIO_NUM_26
#define PIN_IEC_SRQ             GPIO_NUM_27 // FujiLoaf

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

#endif // PINMAP_IEC
