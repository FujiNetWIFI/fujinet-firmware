#ifdef PINMAP_FUJIVERSAL_RS232

// Freenove ESP32-S3 CAM onboard WS2812, used as a single combined status light:
// white = WiFi up, fast orange flicker = bus activity
#define PIN_LED_STRIP           GPIO_NUM_48
#define LED_STRIP_COUNT         1
#define LED_STRIP_STATUS_LIGHT          // WS2812 acts as a combined status light
#define LED_BUS_FLICKER_US      30000   // bus LED flickers (hard-drive activity style)

#define PIN_CARD_DETECT         GPIO_NUM_NC
#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC
#define PIN_SD_HOST_CS          GPIO_NUM_41
#define PIN_SD_HOST_SCK         GPIO_NUM_39
#define PIN_SD_HOST_MISO        GPIO_NUM_40
#define PIN_SD_HOST_MOSI        GPIO_NUM_38

#define PIN_UART0_RX            GPIO_NUM_44
#define PIN_UART0_TX            GPIO_NUM_43

#endif /* PINMAP_FUJIVERSAL_RS232 */
