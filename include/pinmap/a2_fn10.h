/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_A2_FN10

/* SD Card */
#define PIN_CARD_DETECT 12 // fnSystem.h
#define PIN_CARD_DETECT_FIX 15 // fnSystem.h
#define PIN_SD_HOST_CS GPIO_NUM_5 //fnFsSD.cpp
#define PIN_SD_HOST_MISO GPIO_NUM_19
#define PIN_SD_HOST_MOSI GPIO_NUM_23
#define PIN_SD_HOST_SCK GPIO_NUM_18

/* UART */
#define PIN_UART0_RX 3 // fnUART.cpp
#define PIN_UART0_TX 1
#define PIN_UART1_RX 9
#define PIN_UART1_TX 10
#define PIN_UART2_RX 33
#define PIN_UART2_TX 21

/* Buttons */
#define PIN_BUTTON_A 0 // keys.cpp
#define PIN_BUTTON_B 34
#define PIN_BUTTON_C 14

/* LEDs */
#define PIN_LED_WIFI 2 // led.cpp
#define PIN_LED_BUS 4
#define PIN_LED_BT 13

/* Audio Output */
#define PIN_DAC1 25 // samlib.h

/* IWM Bus Pins */
//      SP BUS     GPIO       SIO               LA (with SIO-10IDC cable)
//      ---------  ----     -----------------   -------------------------
#define SP_WRPROT   27
#define SP_ACK      27      //  CLKIN     1     D0
#define SP_REQ      39
#define SP_PHI0     39      //  CMD       7     D4
#define SP_PHI1     22      //  PROC      9     D6
#define SP_PHI2     36      //  MOTOR     8     D5
#define SP_PHI3     26      //  INT       13    D7
#define SP_RDDATA   21      //  DATAIN    3     D2
#define SP_WRDATA   33      //  DATAOUT   5     D3
#define SP_ENABLE   32      //  CLKOUT    2     D1
#define SP_EXTRA    32      //  CLKOUT - used for debug/diagnosing - signals when WRDATA is sampled by ESP32

#endif /* PINMAP_A2_FN10 */
