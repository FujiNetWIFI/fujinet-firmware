#ifdef PINMAP_FUJIVERSAL_TRS80M1

// TRS-80 Model I: FujiNet-TRS80M1 Rev0 (fujinet-hardware TRS-80-Model-I/FujiNet-TRS80M1-Rev0),
// an ESP32-S3-WROOM-1-N16R8 beside the RP2354B that serves the Model I's bus (pico/trs80m1).
// The S3 is the USB *host* on the RP's link, as for the other fujiversal boards.
//
// Deliberately no PIN_UART1_RX/PIN_UART2_RX: their absence is what selects
// the USB transport in lib/bus/rs232/rs232.h rather than a wired UART.

#define PIN_BUTTON_A            GPIO_NUM_NC
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_WIFI            GPIO_NUM_NC

// WS2812B-2020 used as a single combined status light:
// white = WiFi up, fast orange flicker = bus activity
#define PIN_LED_STRIP           GPIO_NUM_48
#define LED_STRIP_COUNT         1
#define LED_STRIP_STATUS_LIGHT          // WS2812 acts as a combined status light
#define LED_BUS_FLICKER_US      30000   // bus LED flickers (hard-drive activity style)

// The board wires card-detect to IO42 with a 10k pull-up; unused until its
// polarity is confirmed on hardware.
#define PIN_CARD_DETECT         GPIO_NUM_NC
#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC
#define PIN_SD_HOST_CS          GPIO_NUM_41
#define PIN_SD_HOST_SCK         GPIO_NUM_39
#define PIN_SD_HOST_MISO        GPIO_NUM_40
#define PIN_SD_HOST_MOSI        GPIO_NUM_38

// The S3's own flashing/monitor UART, not the RP2354B link -- that is
// native USB on the S3's host pins and has no GPIO of its own.
#define PIN_UART0_RX            GPIO_NUM_44
#define PIN_UART0_TX            GPIO_NUM_43

// To the RP's RUN and QSPI_SS (BOOTSEL) through 1k each, both pulled up 10k:
// driven low to assert, input when idle. Same pins as fujiversal-atari5200.
#define PIN_RP2040_RUN          GPIO_NUM_4
#define PIN_RP2040_BOOTSEL      GPIO_NUM_5

// USB device filter for the card. VID-only: 0xCafe is TinyUSB's default and
// what the fujitrs80m1 firmware enumerates as. Rejects the RP2354B sitting in
// BOOTSEL, which enumerates as VID 0x2E8A.
#define FN_USB_EXPECTED_VID     0xCafe

#endif /* PINMAP_FUJIVERSAL_TRS80M1 */
