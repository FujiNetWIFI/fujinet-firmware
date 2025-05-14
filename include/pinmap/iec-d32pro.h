#ifndef PINMAP_LOLIN_D32_PRO_H
#define PINMAP_LOLIN_D32_PRO_H

#ifdef PINMAP_IEC_D32PRO

/* SD Card */
#define PIN_SD_HOST_CS          GPIO_NUM_4  // LOLIN D32 Pro

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_NC  // keys.cpp
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_BUS             GPIO_NUM_5 // 4 FN
#define PIN_IEC_RESET           GPIO_NUM_34
#define PIN_IEC_CLK_OUT         PIN_IEC_CLK_IN
#define PIN_IEC_DATA_IN         GPIO_NUM_25
#define PIN_IEC_DATA_OUT        PIN_IEC_DATA_IN
#define PIN_IEC_SRQ             GPIO_NUM_26

#define PIN_DEBUG		PIN_IEC_SRQ

#include "iec-common.h"
#include "common.h"

#endif // PINMAP_IEC_D32PRO
#endif // PINMAP_LOLIN_D32_PRO_H
