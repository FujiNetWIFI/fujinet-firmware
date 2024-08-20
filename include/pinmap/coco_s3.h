/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_COCO_S3

#include "common.h"

#undef PIN_SD_HOST_SCK
#undef PIN_SD_HOST_MOSI
#undef PIN_SD_HOST_MISO
#undef PIN_SD_HOST_CS
#define PIN_SD_HOST_SCK         GPIO_NUM_4
#define PIN_SD_HOST_MOSI        GPIO_NUM_5
#define PIN_SD_HOST_MISO        GPIO_NUM_6
#define PIN_SD_HOST_CS          GPIO_NUM_7

#undef PIN_UART1_RX
#undef PIN_UART1_TX
#undef PIN_UART2_RX
#undef PIN_UART2_TX
#define PIN_UART1_RX            GPIO_NUM_13 // RS232
#define PIN_UART1_TX            GPIO_NUM_21 // RS232
#define PIN_UART2_RX            GPIO_NUM_33
#define PIN_UART2_TX            GPIO_NUM_21

#undef PIN_BUTTON_B
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B

#undef PIN_LED_BT
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* Coco */
#define PIN_CASS_MOTOR          GPIO_NUM_34 // Second motor pin is tied to +3V
#define PIN_CASS_DATA_IN        GPIO_NUM_33
#define PIN_CASS_DATA_OUT       GPIO_NUM_26
#define PIN_CD                  GPIO_NUM_21 // same as atari PROC
#define PIN_EPROM_A14           GPIO_NUM_36 // Used to set the serial baud rate
#define PIN_EPROM_A15           GPIO_NUM_39 // based on the HDB-DOS image selected
#endif /* PINMAP_COCO_S3 */
