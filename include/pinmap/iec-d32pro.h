#ifndef PINMAP_LOLIN_D32_PRO_H
#define PINMAP_LOLIN_D32_PRO_H

#ifdef PINMAP_IEC_D32PRO

#include "common.h"

#undef PIN_SD_HOST_CS
#define PIN_SD_HOST_CS          GPIO_NUM_4

#undef PIN_BUTTON_A
#undef PIN_BUTTON_B
#undef PIN_BUTTON_C
#define PIN_BUTTON_A            GPIO_NUM_NC
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

#undef PIN_LED_BUS
#define PIN_LED_BUS             GPIO_NUM_5
#ifdef JTAG
#undef PIN_LED_BT
#define PIN_LED_BT              GPIO_NUM_5  // LOLIN D32 PRO
#endif


/* Commodore IEC Pins */
#define IEC_HAS_RESET // Reset line is available

#define PIN_IEC_RESET           GPIO_NUM_34
#define PIN_IEC_ATN             GPIO_NUM_32
#define PIN_IEC_CLK_IN          GPIO_NUM_33
#define PIN_IEC_CLK_OUT         GPIO_NUM_33
#define PIN_IEC_DATA_IN         GPIO_NUM_25
#define PIN_IEC_DATA_OUT        GPIO_NUM_25
#define PIN_IEC_SRQ             GPIO_NUM_26

/* Color Computer */
#define PIN_CASS_MOTOR          GPIO_NUM_34 // Second motor pin is tied to +3V
#define PIN_CASS_DATA_IN        GPIO_NUM_33
#define PIN_CASS_DATA_OUT       GPIO_NUM_26

#define PIN_SERIAL_CD           GPIO_NUM_32
#define PIN_SERIAL_RX           GPIO_NUM_9  // fnUartBUS
#define PIN_SERIAL_TX           GPIO_NUM_10

#endif // PINMAP_IEC_D32PRO
#endif // PINMAP_LOLIN_D32_PRO_H
