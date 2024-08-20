/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_ATARIV1

#include "common.h"

/* Atari SIO Pins */
#define PIN_INT                 GPIO_NUM_26 // sio.h
#define PIN_PROC                GPIO_NUM_22
#define PIN_CKO                 GPIO_NUM_32
#define PIN_CKI                 GPIO_NUM_27
#define PIN_MTR                 GPIO_NUM_36
#define PIN_CMD                 GPIO_NUM_39

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_25 // samlib.h

#endif /* PINMAP_ATARIV1 */
