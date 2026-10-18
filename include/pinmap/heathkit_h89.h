/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_H89

// H89-ESP32 card: an 82C55 in Mode 2 at H89 port 0xD0, port A
// through a 74LVC245 to the ESP32. Builds as BUILD_RS232; PIN_PPI_OBF selects
// PPIChannel in lib/bus/rs232/rs232.h. Self-contained: common.h would put
// card detect on IO15 and the DAC on IO25, both bus lines here.

/* SD Card - fnFsSD.cpp */
#define PIN_CARD_DETECT         GPIO_NUM_NC
#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC
#define PIN_SD_HOST_CS          GPIO_NUM_5
#define PIN_SD_HOST_MISO        GPIO_NUM_19
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#define PIN_SD_HOST_SCK         GPIO_NUM_18

/* UART - CP2102 console only */
#define PIN_UART0_RX            GPIO_NUM_3
#define PIN_UART0_TX            GPIO_NUM_1

/* Buttons - none; left undefined so keys.cpp and fnSystem treat them as NC */

/* LEDs - none on the card */
#define PIN_LED_WIFI            GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_BT              GPIO_NUM_NC

/* 82C55 port A via the 74LVC245 - PPIChannel.cpp */
#define PIN_PPI_D0              GPIO_NUM_32
#define PIN_PPI_D1              GPIO_NUM_33
#define PIN_PPI_D2              GPIO_NUM_25
#define PIN_PPI_D3              GPIO_NUM_26
#define PIN_PPI_D4              GPIO_NUM_27
#define PIN_PPI_D5              GPIO_NUM_14
#define PIN_PPI_D6              GPIO_NUM_12 // strapping pin
#define PIN_PPI_D7              GPIO_NUM_13
#define PIN_PPI_STB             GPIO_NUM_4  // (OUT) PC4 /STB
#define PIN_PPI_ACK             GPIO_NUM_15 // (OUT) PC6 /ACK
#define PIN_PPI_IBF             GPIO_NUM_34 // (IN) PC5 IBF
#define PIN_PPI_OBF             GPIO_NUM_35 // (IN) PC7 /OBF
#define PIN_PPI_DIR             GPIO_NUM_22 // (IN) PC0, 245 DIR, 1 = H89->ESP32
#define PIN_PPI_OE              GPIO_NUM_21 // (IN) PC1, 245 /OE

#endif /* PINMAP_H89 */
