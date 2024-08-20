/*
 * IEC for FujiApple Rev0
 */
#ifdef PINMAP_FUJIAPPLE_IEC

#include "common.h"

#undef PIN_BUTTON_B
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B

#undef PIN_LED_BUS
#undef PIN_LED_BT
#define PIN_LED_BUS             GPIO_NUM_12 // 4 FN
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

// Reset line is available
#define IEC_HAS_RESET

#define PIN_IEC_RESET           GPIO_NUM_21
#define PIN_IEC_ATN             GPIO_NUM_22
#define PIN_IEC_CLK_IN          GPIO_NUM_33
#define PIN_IEC_CLK_OUT         GPIO_NUM_33
#define PIN_IEC_DATA_IN         GPIO_NUM_32
#define PIN_IEC_DATA_OUT        GPIO_NUM_32
#define PIN_IEC_SRQ             GPIO_NUM_26

#endif // PINMAP_FUIAPPLE_IEC
