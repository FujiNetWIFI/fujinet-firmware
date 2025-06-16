/* FujiNet Atari 2600 Hardware Pin Mapping */
#ifdef PINMAP_ATARI2600

/* SD Card */
#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC

/* Buttons */
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_BUS             GPIO_NUM_4
#define PIN_LED_BT              GPIO_NUM_NC

/* Atari SIO Pins */
#define PIN_INT                 GPIO_NUM_27 // sio.h
#define PIN_PROC                GPIO_NUM_26
#define PIN_CKO                 GPIO_NUM_NC
#define PIN_CKI                 GPIO_NUM_NC
#define PIN_MTR                 GPIO_NUM_NC
#define PIN_CMD                 GPIO_NUM_25

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC // samlib.h

#include "atari-common.h"
#include "common.h"

#endif /* PINMAP_ATARI2600 */
