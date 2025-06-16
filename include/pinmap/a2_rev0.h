/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_A2_REV0

#ifdef MASTERIES_REV0
#define PIN_SD_HOST_MOSI        GPIO_NUM_14
#define SP_RDDATA               GPIO_NUM_23 // Pin to use for SmartPort SPI hardware mod Masteries edition
#else
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#define SP_RDDATA               GPIO_NUM_14 // Pin to use for SmartPort SPI hardware mod FujiApple Rev0
#endif // MASTERIES_REV0

/* LEDs */
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

#include "common.h"

/* IWM Bus Pins */
#define SP_PHI0                 GPIO_NUM_32
#define SP_PHI1                 GPIO_NUM_33
#define SP_PHI2                 GPIO_NUM_34
#define SP_PHI3                 GPIO_NUM_35
#define SP_WRPROT               GPIO_NUM_27
#define SP_WRDATA               GPIO_NUM_22
// TODO: go through each line and make sure the code is OK for each one before moving to next
#define SP_WREQ                 GPIO_NUM_26
#define SP_DRIVE1               GPIO_NUM_36
#define SP_DRIVE2               GPIO_NUM_21
#define SP_EN35                 GPIO_NUM_39
#define SP_HDSEL                GPIO_NUM_13

/* Aliases of other pins */
#define SP_REQ                  SP_PHI0
#define SP_ACK                  SP_WRPROT

#define SP_RD_BUFFER            GPIO_NUM_4 // tri-state gate enable line

/* SP_PHIn pins must all be in same GPIO register in ascending order */
#define IWM_PHASE_COMBINE() {(uint8_t) (GPIO.in1.val & (uint32_t) 0b1111)}

#define SP_EXTRA                SP_DRIVE2 // For extra debugging with logic analyzer
#endif /* PINMAP_A2_REV0 */
