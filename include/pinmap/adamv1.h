/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_ADAMV1

/* Buttons */
#define PIN_BUTTON_B            GPIO_NUM_34

/* LEDs */
#define PIN_LED_BUS             GPIO_NUM_4

/* Pins for AdamNet */
#define PIN_ADAMNET_RESET       GPIO_NUM_26

/* Pins for Adam USB */
#define PIN_USB_DP              GPIO_NUM_27      // D+
#define PIN_USB_DM              GPIO_NUM_32      // D-

#include "atari-common.h"
#include "common.h"

#endif /* PINMAP_ADAMV1 */
