/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_LYNX

#include "common.h"

#undef PIN_BUTTON_B
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B

/* Atari Lynx Pin assignments */
#define PIN_COMLYNX_RESET       GPIO_NUM_26

#endif /* PINMAP_LYNX */
