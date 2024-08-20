#ifndef PINMAP_IEC_NUGGET_H
#define PINMAP_IEC_NUGGET_H

#ifdef PINMAP_IEC_NUGGET

#include "common.h"

#undef PIN_SD_HOST_CS
#define PIN_SD_HOST_CS          GPIO_NUM_4  // LOLIN D32 Pro

#undef PIN_BUTTON_A
#undef PIN_BUTTON_B
#undef PIN_BUTTON_C
#define PIN_BUTTON_A            GPIO_NUM_NC  // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

#undef PIN_LED_WIFI
#undef PIN_LED_BUS
#define PIN_LED_WIFI            GPIO_NUM_5 // led.cpp
#define PIN_LED_BUS             GPIO_NUM_2 // 4 FN

#if defined(JTAG)
#undef PIN_LED_BT
#define PIN_LED_BT              GPIO_NUM_5  // LOLIN D32 PRO
#endif

/* Commodore IEC Pins */
//#define IEC_HAS_RESET // Reset line is available

#define PIN_IEC_RESET           GPIO_NUM_34
#define PIN_IEC_ATN             GPIO_NUM_32
#define PIN_IEC_CLK_IN          GPIO_NUM_33
#define PIN_IEC_CLK_OUT         GPIO_NUM_33
#define PIN_IEC_DATA_IN         GPIO_NUM_14
#define PIN_IEC_DATA_OUT        GPIO_NUM_14
#define PIN_IEC_SRQ             GPIO_NUM_27

/* Modem/Parallel Switch */
/* Unused with Nugget    */
#define PIN_MODEM_ENABLE        GPIO_NUM_2  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_15 // High = UP9600 enabled

#endif // PINMAP_IEC_NUGGET
#endif // PINMAP_IEC_NUGGET_H
