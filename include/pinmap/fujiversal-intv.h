#ifdef PINMAP_FUJIVERSAL_INTV

// PiRTOII-Fuji: RP2040/RP2350 (Intellivision cart bus, see pico/intellivision/
// firmware/boards/fujicard.cmake) + ESP32-S3-WROOM-1-N16R8 on one board, no
// external RP2040/RP2350 USB port. Reuses the Freenove ESP32-S3 CAM pin
// numbers from fujiversal-rs232.h/fujiversal-drivewire.h verbatim for
// SD/UART/LED so those paths need no firmware change, and adds the two
// RP2040/RP2350 control signals that variant never needed. See
// ~/Workspace/PiRTOII-Fuji/HARDWARE.md for the schematic-level rationale.

#define PIN_BUTTON_A            GPIO_NUM_NC
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

#define PIN_LED_BT               GPIO_NUM_NC
#define PIN_LED_BUS              GPIO_NUM_NC
#define PIN_LED_WIFI             GPIO_NUM_NC

// Freenove ESP32-S3 CAM onboard WS2812, used as a single combined status light:
// white = WiFi up, fast orange flicker = bus activity
#define PIN_LED_STRIP            GPIO_NUM_48
#define LED_STRIP_COUNT          1
#define LED_STRIP_STATUS_LIGHT           // WS2812 acts as a combined status light
#define LED_BUS_FLICKER_US       30000   // bus LED flickers (hard-drive activity style)

#define PIN_CARD_DETECT          GPIO_NUM_NC
#define PIN_CARD_DETECT_FIX      GPIO_NUM_NC
#define PIN_SD_HOST_CS           GPIO_NUM_41
#define PIN_SD_HOST_SCK          GPIO_NUM_39
#define PIN_SD_HOST_MISO         GPIO_NUM_40
#define PIN_SD_HOST_MOSI         GPIO_NUM_38

// S3's own flashing/monitor UART, bridged off-board through a CH343P to the
// board's single USB-C connector -- not the RP2040/RP2350 link (that's native
// USB, permanently wired to the S3's USB host pins, no GPIO of its own).
#define PIN_UART0_RX             GPIO_NUM_44
#define PIN_UART0_TX             GPIO_NUM_43

// RP2040/RP2350 hardware BOOTSEL forcing (recovery path, works even if that
// firmware is bricked or hung -- see fuji_mailbox.h's FUJI_MB_BOOTSEL_DOORBELL
// for the normal, cooperative path over the internal USB link instead). Each
// drives one transistor: active low, Hi-Z/input when idle so normal
// boot/flash access is never disturbed.
//   PIN_RP2040_RUN:      -> RUN, pulsed low then released to reset it
//   PIN_RP2040_BOOTSEL:  -> QSPI_SS via 1k, held low through the pulse to
//                           mimic the BOOTSEL button
// GPIO26-32 are the S3's own flash pins and GPIO33-37 are octal PSRAM on this
// N16R8 part -- both unavailable. Picked from the unclaimed 4-18 range,
// avoiding strapping pins 0/3/45/46.
#define PIN_RP2040_RUN           GPIO_NUM_4
#define PIN_RP2040_BOOTSEL       GPIO_NUM_5

// USB device filter for the cartridge link, consumed by lib/bus/rs232.
// VID-only: any Minty build (the PID varies with MSC), while still rejecting an
// RP2040/RP2350 sitting in BOOTSEL, which enumerates as VID 0x2E8A.
#define FN_USB_EXPECTED_VID      0xCafe

#endif /* PINMAP_FUJIVERSAL_INTV */
