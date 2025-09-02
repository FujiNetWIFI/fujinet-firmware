#ifdef PINMAP_ESP32S3_XDRIVE

#define IF_ENABLE_PIN           GPIO_NUM_5

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_NC // GPIO_NUM_48 fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_21  // fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_16
#define PIN_SD_HOST_MOSI        GPIO_NUM_18
#define PIN_SD_HOST_SCK         GPIO_NUM_17

/* UART */
#define PIN_UART0_RX            GPIO_NUM_44  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_43
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_9
#define PIN_UART2_TX            GPIO_NUM_8

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_34
#define PIN_BUTTON_B            GPIO_NUM_35
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs strip */
#define PIN_LED_STRIP           GPIO_NUM_33 // fnLedStrip.cpp
#define LED_STRIP_COUNT         1

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_NC // led.cpp
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_BT              GPIO_NUM_NC

/* Atari SIO Pins */
#define PIN_INT                 GPIO_NUM_13 // sio.h
#define PIN_PROC                GPIO_NUM_12
#define PIN_CKO                 GPIO_NUM_6
#define PIN_CKI                 GPIO_NUM_7
#define PIN_MTR                 GPIO_NUM_11
#define PIN_CMD                 GPIO_NUM_10


/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC

#endif /* PINMAP_ESP32S3_XDRIVE */

