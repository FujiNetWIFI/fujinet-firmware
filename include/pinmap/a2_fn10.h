/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_A2_FN10

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15 // fnSystem.h
#define PIN_SD_HOST_CS          GPIO_NUM_5  // fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_19
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#define PIN_SD_HOST_SCK         GPIO_NUM_18

/* UART */
#define PIN_UART0_RX            GPIO_NUM_3  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_1
#define PIN_UART1_RX            GPIO_NUM_9
#define PIN_UART1_TX            GPIO_NUM_10
#define PIN_UART2_RX            GPIO_NUM_33
#define PIN_UART2_TX            GPIO_NUM_21

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0  // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_34
#define PIN_BUTTON_C            GPIO_NUM_14

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_2  // led.cpp
#define PIN_LED_BUS             GPIO_NUM_4
#define PIN_LED_BT              GPIO_NUM_13

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_25 // samlib.h

/* IWM Bus Pins */
//      SP BUS                  GPIO                 SIO             LA (with SIO-10IDC cable)
//      ---------               -----------          -------------   -------------------------
#define SP_WRPROT               GPIO_NUM_27
#define SP_ACK                  GPIO_NUM_27      //  CLKIN     1     D0
#define SP_REQ                  GPIO_NUM_39
#define SP_PHI0                 GPIO_NUM_39      //  CMD       7     D4
#define SP_PHI1                 GPIO_NUM_22      //  PROC      9     D6
#define SP_PHI2                 GPIO_NUM_36      //  MOTOR     8     D5
#define SP_PHI3                 GPIO_NUM_26      //  INT       13    D7
#define SP_RDDATA               GPIO_NUM_21      //  DATAIN    3     D2
#define SP_WRDATA               GPIO_NUM_33      //  DATAOUT   5     D3
#define SP_ENABLE               GPIO_NUM_32      //  CLKOUT    2     D1
#define SP_EXTRA                GPIO_NUM_32      //  CLKOUT - used for debug/diagnosing - signals when WRDATA is sampled by ESP32

#endif /* PINMAP_A2_FN10 */
