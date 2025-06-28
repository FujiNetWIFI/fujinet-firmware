/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_MAC_REV0

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h

/* UART */
#define PIN_UART2_TX            GPIO_NUM_26
#define MCI_HDSEL                GPIO_NUM_32

#define PINMAP_A2_REV0
#include "a2_rev0.h"
#undef SP_RDDATA
#define SP_RDDATA               GPIO_NUM_4  // tri-state gate enable line
#undef PINMAP_A2_REV0

#endif /* PINMAP_MAC_REV0 */

