/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_CX16

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h

/* Buttons */
#define PIN_BUTTON_B            GPIO_NUM_34

/* LEDs */
#define PIN_LED_BUS             GPIO_NUM_4

/* CX16 I²C */
#define PIN_SDA                 GPIO_NUM_21
#define PIN_SCL                 GPIO_NUM_22

#include "atari-common.h"
#include "common.h"

#endif /* PINMAP_CX16 */
