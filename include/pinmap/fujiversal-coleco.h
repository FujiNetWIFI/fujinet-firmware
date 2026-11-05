#ifdef PINMAP_FUJIVERSAL_COLECO

// ColecoVision. The console side is an RP2040 cartridge (see pico/coleco/)
// that serves the 32K cart window and a read-hotspot mailbox; this ESP32-S3
// is the USB *host* on the other end of that link, exactly as for
// fujiversal-o2/astrocade. Nothing here is ColecoVision-specific -- the
// console identity lives entirely in the RP2040 firmware. See
// pico/coleco/README.md.
//
// Deliberately no PIN_UART1_RX/PIN_UART2_RX: their absence is what selects
// the USB transport in lib/bus/rs232/rs232.h rather than a wired UART.
//
// Pin numbers are the Freenove ESP32-S3 CAM assignments carried over from
// fujiversal-arcadia.h, and the FujiNet-ColecoVision Rev0 cart (fujinet-hardware
// ColecoVision/FujiNet-ColecoVision-Rev0: the S3 and an RP2350A on one board)
// keeps them: SD 38-41, UART0 43/44, WS2812 48. On that cart IO4 / IO5 drive the
// RP's RUN and BOOTSEL through 1k, as on the other on-cart S3 boards; with a
// separate Pico cart they go nowhere and stay inputs. The cart also wires the
// microSD's card detect to IO42; it stays NC here until that is tested, since a
// Freenove board leaves IO42 free.

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

// The RP2350A's RUN / BOOTSEL on the FujiNet-ColecoVision cart (active low, idle
// as inputs): fnPicoUpdater::forceBootselViaPins() resets it into its USB boot ROM.
#define PIN_RP2040_RUN          GPIO_NUM_4
#define PIN_RP2040_BOOTSEL      GPIO_NUM_5

// USB device filter for the cartridge. VID-only: 0xCafe is TinyUSB's
// default and what the fujicoleco cart enumerates as; the PID varies with
// the compiled interface set. Rejects an RP2040 sitting in BOOTSEL, which
// enumerates as VID 0x2E8A.
#define FN_USB_EXPECTED_VID     0xCafe

#endif /* PINMAP_FUJIVERSAL_COLECO */
