/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_COCO_CART

/* SD Card - fnFsSD.cpp */
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15 // fnSystem.h
#define PIN_SD_HOST_CS          GPIO_NUM_5  // fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_19
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#define PIN_SD_HOST_SCK         GPIO_NUM_18

/* UART - fnuart.cpp */
#define PIN_UART0_RX            GPIO_NUM_3  // USB Serial
#define PIN_UART0_TX            GPIO_NUM_1  // USB Serial
#define PIN_UART1_RX            GPIO_NUM_13 // RS232 HDSEL
#define PIN_UART1_TX            GPIO_NUM_21 // RS232 DRV2
#define PIN_UART2_RX          GPIO_NUM_13
#define PIN_UART2_TX          GPIO_NUM_21

/* Buttons - keys.cpp */
#define PIN_BUTTON_A            GPIO_NUM_0  // Button 0 on DEVKITC-VE
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B
#define PIN_BUTTON_C            GPIO_NUM_14 // Safe reset

/* LEDs - leds.cpp */
#define PIN_LED_WIFI            GPIO_NUM_2
#define PIN_LED_BUS             GPIO_NUM_12
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* Audio Output - samlib.h */
#define PIN_DAC1                GPIO_NUM_25 // not connected

/* Coco */
#define PIN_CASS_MOTOR          GPIO_NUM_34 // Second motor pin is tied to +3V
#define PIN_CASS_DATA_IN        GPIO_NUM_33
#define PIN_CASS_DATA_OUT       GPIO_NUM_26
#define PIN_CD                  GPIO_NUM_22 // same as atari PROC
#define PIN_EPROM_A14           GPIO_NUM_36 // Used to set the serial baud rate
#define PIN_EPROM_A15           GPIO_NUM_39 // based on the HDB-DOS image selected
#endif /* PINMAP_COCO_DEVKITC */
