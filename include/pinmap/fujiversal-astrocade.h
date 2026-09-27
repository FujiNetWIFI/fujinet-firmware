#ifdef PINMAP_FUJIVERSAL_ASTROCADE

// Bally Astrocade: the FujiNet-Astrocade Rev0 cartridge (fujinet-hardware
// Astrocade/rev0). An RP2354A on the same board serves the 8K cart window
// and a read-hotspot mailbox (pico/astrocade/, board fujicade_rp2354); this
// ESP32-S3-WROOM-1-N16R8 is the USB *host* on the other end of that link,
// exactly as for fujiversal-o2/-intv. Nothing here is Astrocade-specific --
// the console identity lives entirely in the RP firmware. See
// pico/astrocade/README.md.
//
// Deliberately no PIN_UART1_RX/PIN_UART2_RX: their absence is what selects
// the USB transport in lib/bus/rs232/rs232.h rather than a wired UART.
//
// The SD/UART/LED pins keep the Freenove ESP32-S3 CAM assignments the
// breadboard bring-up used (fujiversal-o2.h), so the same image also runs
// on that dev board plus a separate RP cart.

#define PIN_BUTTON_A            GPIO_NUM_NC
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

#define PIN_LED_BT              GPIO_NUM_NC
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_WIFI            GPIO_NUM_NC

// WS2812B-2020-V6 (3.3V part) under the shell's light pipe, used as a
// single combined status light: white = WiFi up, fast orange flicker = bus
// activity
#define PIN_LED_STRIP           GPIO_NUM_48
#define LED_STRIP_COUNT         1
#define LED_STRIP_STATUS_LIGHT          // WS2812 acts as a combined status light
#define LED_BUS_FLICKER_US      30000   // bus LED flickers (hard-drive activity style)

// The PCB wires the TF-015 socket's card-detect switch to IO42 with a 10k
// pull-up; left unused until its polarity is confirmed on real hardware.
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

// RP2354A hardware BOOTSEL forcing (recovery path for a bricked or hung
// cart firmware; the cooperative path is the mailbox BOOTSEL doorbell over
// USB). Each pin reaches the RP through a 1k series resistor -- no
// transistor -- so lib/hardware/fnPicoUpdater's "drive low to assert,
// input when idle" is exactly what the board wants:
//   PIN_RP2040_RUN:      -> RUN (10k pull-up), pulsed low to reset the RP
//   PIN_RP2040_BOOTSEL:  -> QSPI_SS (10k pull-up), held low across the pulse
// GPIO26-32 are the S3's flash and GPIO33-37 its octal PSRAM on this N16R8
// part; strapping pins 0/3/45/46 avoided. Same pins as fujiversal-intv.
#define PIN_RP2040_RUN          GPIO_NUM_4
#define PIN_RP2040_BOOTSEL      GPIO_NUM_5

// USB device filter for the cartridge. VID-only: 0xCafe is TinyUSB's
// default and what the fujicade cart enumerates as; the PID varies with the
// compiled interface set. Rejects an RP2040/RP2350 sitting in BOOTSEL,
// which enumerates as VID 0x2E8A.
#define FN_USB_EXPECTED_VID     0xCafe

#endif /* PINMAP_FUJIVERSAL_ASTROCADE */
