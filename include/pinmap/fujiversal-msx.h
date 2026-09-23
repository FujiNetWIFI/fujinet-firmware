#ifdef PINMAP_FUJIVERSAL_MSX

// MSX cartridge. The console side is an RP2350B cartridge (the
// msx_proto_260402 board in the pico/fujiversal submodule) that emulates the
// cartridge ROM and carries FujiBus traffic; this ESP32-S3 is the USB host
// on the other end of that link, exactly as for fujiversal-intv and
// fujiversal-o2. Nothing here is MSX-specific -- the console identity lives
// entirely in the cartridge firmware.
//
// Deliberately no PIN_UART1_RX/PIN_UART2_RX: their absence is what selects
// the USB transport in lib/bus/rs232/rs232.h rather than a wired UART.
//
// Pin numbers are the Freenove ESP32-S3 CAM assignments carried over from
// fujiversal-rs232.h verbatim, so the SD/UART/LED paths need no change.
// There is no combined MSX board yet: the cartridge is a separate RP2350
// board joined only by USB, so there are no RUN/BOOTSEL control lines to
// declare -- recovery there is the cartridge's own BOOTSEL button. Revisit
// when a single-PCB design exists.

#define PIN_BUTTON_A            GPIO_NUM_NC
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_WIFI            GPIO_NUM_NC

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

// The S3's own flashing/monitor UART, not the cartridge link -- that is
// native USB on the S3's host pins and has no GPIO of its own.
#define PIN_UART0_RX            GPIO_NUM_44
#define PIN_UART0_TX            GPIO_NUM_43

// USB device filter for the cartridge link, consumed by lib/bus/rs232.
// VID-only: the fujiversal firmware enumerates with the pico SDK's stock
// identity, whose product ID differs between RP2040 (0x000A) and RP2350
// (0x0009) builds. A chip in BOOTSEL shares this vendor ID but presents no
// CDC-ACM function, so it is already excluded by the interface check.
#define FN_USB_EXPECTED_VID     0x2E8A

#endif /* PINMAP_FUJIVERSAL_MSX */
