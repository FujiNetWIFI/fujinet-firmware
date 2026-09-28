/* FujiNet Hardware Pin Mapping */
#ifdef PINMAP_COCO_ES3C28P

/* LCDWiki ES3C28P 2.8" "CYD" board: ESP32-S3 N16R8 (16MB flash, 8MB octal
 * PSRAM), ILI9341V 240x320 SPI LCD, FT6336 capacitive touch, 4-bit SDMMC
 * microSD, single WS2812 RGB LED, ES8311 I2S audio codec.
 *
 * The CoCo reaches this board over Becker-over-IP (WiFi), so no bus GPIOs
 * are wired. coco-common.h is deliberately NOT included so the #ifdef'd
 * cassette/DCD/EPROM setup in drivewire.cpp stays compiled out.
 *
 * Reserved by on-board peripherals (do not reuse):
 *   LCD   CS 10, DC 46, SCK 12, MOSI 11, MISO 13, BL 45 (RST = EN)
 *   Touch SDA 16, SCL 15, RST 18, INT 17
 *   SD    CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47
 *   Audio EN 1 (low = on), MCLK 4, BCLK 5, DOUT 8, LRCK 7, DIN 6
 *   Battery ADC 9, RGB LED 42 (unused), console UART0 RX 44 / TX 43
 * Free expansion pins: 2, 3, 14, 21
 */

/* SD card - 4-bit SDMMC (fnFsSD.cpp) */
#define SDMMC_HOST_WIDTH        4
#define PIN_SD_HOST_CLK         GPIO_NUM_38
#define PIN_SD_HOST_CMD         GPIO_NUM_40
#define PIN_SD_HOST_D0          GPIO_NUM_39
#define PIN_SD_HOST_D1          GPIO_NUM_41
#define PIN_SD_HOST_D2          GPIO_NUM_48
#define PIN_SD_HOST_D3          GPIO_NUM_47
#define PIN_SD_HOST_WP          GPIO_NUM_NC
/* SPI SD pins only feed the SD-card firmware updater (mlff_update), which
 * does not support SDMMC; flash this board over USB instead. */
#define PIN_SD_HOST_CS          GPIO_NUM_NC
#define PIN_SD_HOST_MISO        GPIO_NUM_NC
#define PIN_SD_HOST_MOSI        GPIO_NUM_NC
#define PIN_SD_HOST_SCK         GPIO_NUM_NC
#define PIN_CARD_DETECT         GPIO_NUM_NC // No card detect (15 is touch SCL)
#define PIN_CARD_DETECT_FIX     GPIO_NUM_NC

/* UART - console on UART0, optional wired DriveWire on expansion pins */
#define PIN_UART0_RX            GPIO_NUM_44
#define PIN_UART0_TX            GPIO_NUM_43
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_14 // expansion
#define PIN_UART2_TX            GPIO_NUM_21 // expansion

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0  // BOOT button
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs - the onboard WS2812 (IO42) is left unused: lighting it draws enough
 * current to disturb USB power on weak hosts. The on-screen UI will show
 * WiFi and bus status instead. */
#define PIN_LED_BUS             GPIO_NUM_NC
#define PIN_LED_WIFI            GPIO_NUM_NC
#define PIN_LED_BT              GPIO_NUM_NC

/* Audio - ES8311 codec is I2S, not a DAC pin */
#define PIN_DAC1                GPIO_NUM_NC

/* Display - ILI9341V */
#define PIN_LCD_CS              GPIO_NUM_10
#define PIN_LCD_DC              GPIO_NUM_46
#define PIN_LCD_SCK             GPIO_NUM_12
#define PIN_LCD_MOSI            GPIO_NUM_11
#define PIN_LCD_MISO            GPIO_NUM_13
#define PIN_LCD_BL              GPIO_NUM_45

/* Touch - FT6336 on I2C */
#define PIN_TOUCH_SDA           GPIO_NUM_16
#define PIN_TOUCH_SCL           GPIO_NUM_15
#define PIN_TOUCH_RST           GPIO_NUM_18
#define PIN_TOUCH_INT           GPIO_NUM_17

#include "common.h"
#endif /* PINMAP_COCO_ES3C28P */
