#ifdef PINMAP_FUJIVERSAL_ATARI2600

// Atari 2600. The console side is an RP2040 cartridge (see pico/atari-2600/)
// that serves a 4K window at $1000-$1FFF -- part banked client image, part
// painted mailbox, part cart-composed text planes, because the console has no
// framebuffer of its own. This ESP32-S3 is the USB *host* on the other end of
// that link, exactly as for fujiversal-o2/astrocade/arcadia/coleco/channelf.
// Nothing here is 2600-specific: the console identity lives entirely in the
// RP2040 firmware. See pico/atari-2600/README-fujinet.md.
//
// Deliberately no PIN_UART1_RX/PIN_UART2_RX: their absence is what selects
// the USB transport in lib/bus/rs232/rs232.h rather than a wired UART.
//
// PROVISIONAL. Pin numbers are carried over unchanged from the sibling
// fujiversal boards so the SD/UART/LED paths need no change; no 2600 hardware
// exists yet.
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
// default and what the fujiarcadia cart enumerates as; the PID varies with
// the compiled interface set. Rejects an RP2040 sitting in BOOTSEL, which
// enumerates as VID 0x2E8A.
#define FN_USB_EXPECTED_VID     0xCafe

#endif /* PINMAP_FUJIVERSAL_ATARI2600 */
