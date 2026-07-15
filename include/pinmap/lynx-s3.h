/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_LYNX_S3

/* SD Card */
#define PIN_CARD_DETECT     GPIO_NUM_9
#define PIN_CARD_DETECT_FIX GPIO_NUM_NC
#define PIN_SD_HOST_CS      GPIO_NUM_10
#define PIN_SD_HOST_MISO    GPIO_NUM_13
#define PIN_SD_HOST_MOSI    GPIO_NUM_11
#define PIN_SD_HOST_SCK     GPIO_NUM_21

/* UART */
#define PIN_UART0_RX GPIO_NUM_NC
#define PIN_UART0_TX GPIO_NUM_NC
#define PIN_UART1_RX GPIO_NUM_NC
#define PIN_UART1_TX GPIO_NUM_NC
#define PIN_UART2_RX GPIO_NUM_41
#define PIN_UART2_TX GPIO_NUM_42

/* Buttons */
#define PIN_BUTTON_A GPIO_NUM_0
#define PIN_BUTTON_B GPIO_NUM_NC // No Button B
#define PIN_BUTTON_C GPIO_NUM_39

/* LEDs */
#define PIN_LED_WIFI    GPIO_NUM_38 
#define PIN_LED_BUS     GPIO_NUM_2
#define PIN_LED_BT      GPIO_NUM_40

/* Audio Output */
#define PIN_DAC1 GPIO_NUM_NC

#endif /* PINMAP_LYNX_S3 */