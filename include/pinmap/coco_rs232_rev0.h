/* CoCo DriveWire using the rev0 RS232 */
#ifdef PINMAP_COCO_RS232

/* UART - fnuart.cpp */
#define PIN_UART1_RX            GPIO_NUM_13 // RS232
#define PIN_UART1_TX            GPIO_NUM_21 // RS232

/* Buttons - keys.cpp */
#define PIN_BUTTON_C            GPIO_NUM_39 // Safe reset

#include "coco-common.h"
#include "common.h"

#endif /* PINMAP_COCO_RS232 */
