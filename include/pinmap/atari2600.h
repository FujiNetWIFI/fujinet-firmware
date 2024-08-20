/* FujiNet Atari 2600 Hardware Pin Mapping */
#ifdef PINMAP_ATARI2600

#include "common.h"

#undef PIN_CARD_DETECT
#define PIN_CARD_DETECT         GPIO_NUM_15 // fnSystem.h

#undef PIN_BUTTON_B
#undef PIN_BUTTON_C
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

#undef PIN_DAC1
#define PIN_DAC1                GPIO_NUM_NC // samlib.h

/* Atari SIO Pins */
#define PIN_INT                 GPIO_NUM_27 // sio.h
#define PIN_PROC                GPIO_NUM_26
#define PIN_CKO                 GPIO_NUM_NC
#define PIN_CKI                 GPIO_NUM_NC
#define PIN_MTR                 GPIO_NUM_NC
#define PIN_CMD                 GPIO_NUM_25

#endif /* PINMAP_ATARI2600 */
