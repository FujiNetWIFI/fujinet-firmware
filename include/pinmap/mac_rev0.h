/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_MAC_REV0

#include "common.h"

#undef PIN_UART2_TX
#define PIN_UART2_TX            GPIO_NUM_26

#undef PIN_BUTTON_B
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B

#undef PIN_LED_BUS
#undef PIN_LED_BT
#define PIN_LED_BUS             GPIO_NUM_12
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

// TODO - call it Microfloppy Control Interface MCI_xxxxx
/* IWM Bus Pins */
// #define SP_REQ                  GPIO_NUM_32
// #define SP_PHI0                 GPIO_NUM_32
// #define SP_PHI1                 GPIO_NUM_33
// #define SP_PHI2                 GPIO_NUM_34
// #define SP_PHI3                 GPIO_NUM_35
// #define SP_WRPROT               GPIO_NUM_27
// #define SP_ACK                  GPIO_NUM_27
#define SP_RDDATA               GPIO_NUM_4  // tri-state gate enable line
#define SP_WRDATA               GPIO_NUM_22
// #define SP_WREQ                 GPIO_NUM_26
// #define SP_DRIVE1               GPIO_NUM_36
// #define SP_DRIVE2               GPIO_NUM_21
// #define SP_EN35                 GPIO_NUM_39
#define MCI_HDSEL                GPIO_NUM_32

#define SP_EXTRA                SP_DRIVE2 // For extra debugging with logic analyzer
#endif /* PINMAP_MAC_REV0 */

