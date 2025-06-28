/*
 * IEC for FujiApple Rev0
 */
#ifdef PINMAP_FUJIAPPLE_IEC


/* LEDs */
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

#define PIN_IEC_RESET           GPIO_NUM_21
#define PIN_IEC_ATN             GPIO_NUM_22
#define PIN_IEC_CLK_IN          GPIO_NUM_33
#define PIN_IEC_CLK_OUT         GPIO_NUM_33
#define PIN_IEC_DATA_IN         GPIO_NUM_32
#define PIN_IEC_DATA_OUT        GPIO_NUM_32
#define PIN_IEC_SRQ             GPIO_NUM_26

#include "iec-common.h"
#include "common.h"

#endif // PINMAP_FUIAPPLE_IEC
