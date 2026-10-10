#ifdef PINMAP_FUJIVERSAL_STUDIO2

// RCA Studio II. The console side is an RP2354B cartridge (see
// pico/studio2/) that serves the image's pages, the mailbox and the raster
// the 1861 displays; this ESP32-S3 is the USB *host* on the other end of
// that link, exactly as for fujiversal-channelf/arcadia. Nothing here is
// Studio II-specific -- the console identity lives entirely in the
// cartridge firmware. See pico/studio2/README.md.
//
// Deliberately no PIN_UART1_RX/PIN_UART2_RX: their absence is what selects
// the USB transport in lib/bus/rs232/rs232.h rather than a wired UART.
//
// PROVISIONAL. Pin numbers are the Freenove ESP32-S3 CAM assignments carried
// over from fujiversal-astrocade.h so the SD/UART/LED paths need no change. The
// cartridge is a separate RP2354B board joined only by USB, so there are no
// RUN/BOOTSEL control lines to declare. Revisit when hardware exists.

#define PIN_BUTTON_A            GPIO_NUM_NC
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_WIFI            GPIO_NUM_NC

// Freenove ESP32-S3 CAM onboard WS2812, used as a single combined status
// light: white = WiFi up, fast orange flicker = bus activity
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

// The S3's own flashing/monitor UART, not the cartridge link -- that is
// native USB on the S3's host pins and has no GPIO of its own.
#define PIN_UART0_RX            GPIO_NUM_44
#define PIN_UART0_TX            GPIO_NUM_43

// USB device filter for the cartridge. VID-only: 0xCafe is TinyUSB's
// default and what the fujistudio2 cart enumerates as; the PID varies with
// the compiled interface set. Rejects an RP2354B sitting in BOOTSEL, which
// enumerates as VID 0x2E8A.
#define FN_USB_EXPECTED_VID     0xCafe

#endif /* PINMAP_FUJIVERSAL_STUDIO2 */
