/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_A2_REV0

#include "common.h"

#ifdef MASTERIES_SPI_FIX
#undef PIN_SD_HOST_MOSI
#define PIN_SD_HOST_MOSI        GPIO_NUM_14
#endif

#undef PIN_BUTTON_B
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B

#undef PIN_LED_BUS
#undef PIN_LED_BT
#define PIN_LED_BUS             GPIO_NUM_12
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* IWM Bus Pins */
#define SP_PHI0                 GPIO_NUM_32
#define SP_PHI1                 GPIO_NUM_33
#define SP_PHI2                 GPIO_NUM_34
#define SP_PHI3                 GPIO_NUM_35
#define SP_WREQ                 GPIO_NUM_26
#define SP_ENABLE               GPIO_NUM_36
#define SP_RDDATA               GPIO_NUM_4 // tri-state gate enable line
#define SP_WRDATA               GPIO_NUM_22
#define SP_WRPROT               GPIO_NUM_27

#define SP_REQ                  SP_PHI0
#define SP_ACK                  SP_WRPROT  

// TODO: go through each line and make sure the code is OK for each one before moving to next
#define SP_DRIVE1               SP_ENABLE
#define SP_DRIVE2               GPIO_NUM_21
#define SP_EN35                 GPIO_NUM_39
#define SP_HDSEL                GPIO_NUM_13

#ifdef MASTERIES_SPI_FIX
#define SP_SPI_FIX_PIN          GPIO_NUM_23 // Pin to use for SmartPort SPI hardware mod Masteries edition
#else
#define SP_SPI_FIX_PIN          GPIO_NUM_14 // Pin to use for SmartPort SPI hardware mod FujiApple Rev0
#endif // MASTERIES_SPI_FIX

#define SP_EXTRA                SP_DRIVE2 // For extra debugging with logic analyzer
#endif /* PINMAP_A2_REV0 */
