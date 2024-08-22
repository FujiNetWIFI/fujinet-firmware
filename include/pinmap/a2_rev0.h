/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_A2_REV0

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15 // fnSystem.h
#define PIN_SD_HOST_CS          GPIO_NUM_5 //fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_19
#ifdef MASTERIES_REV0
#define PIN_SD_HOST_MOSI        GPIO_NUM_14
#else
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#endif // MASTERIES_REV0
#define PIN_SD_HOST_SCK         GPIO_NUM_18

/* UART */
#define PIN_UART0_RX            GPIO_NUM_3 // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_1
#define PIN_UART1_RX            GPIO_NUM_9
#define PIN_UART1_TX            GPIO_NUM_10
#define PIN_UART2_RX            GPIO_NUM_33
#define PIN_UART2_TX            GPIO_NUM_21

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0 // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B
#define PIN_BUTTON_C            GPIO_NUM_14

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_2 // led.cpp
#define PIN_LED_BUS             GPIO_NUM_12
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_25 // samlib.h

/* IWM Bus Pins */
#define SP_PHI0                 GPIO_NUM_32
#define SP_PHI1                 GPIO_NUM_33
#define SP_PHI2                 GPIO_NUM_34
#define SP_PHI3                 GPIO_NUM_35
#define SP_WRPROT               GPIO_NUM_27
#ifdef MASTERIES_REV0
#define SP_RDDATA               GPIO_NUM_23 // Pin to use for SmartPort SPI hardware mod Masteries edition
#else
#define SP_RDDATA               GPIO_NUM_14 // Pin to use for SmartPort SPI hardware mod FujiApple Rev0
#endif // MASTERIES_REV0
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

#define SP_EXTRA                SP_DRIVE2 // For extra debugging with logic analyzer
#endif /* PINMAP_A2_REV0 */
