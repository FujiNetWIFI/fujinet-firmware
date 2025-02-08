#ifndef PINMAP_IEC_NUGGET_H
#define PINMAP_IEC_NUGGET_H

// https://www.wemos.cc/en/latest/d32/d32_pro.html
// https://www.wemos.cc/en/latest/_static/files/sch_d32_pro_v2.0.0.pdf
// https://www.espressif.com.cn/sites/default/files/documentation/esp32-wrover-e_esp32-wrover-ie_datasheet_en.pdf

#ifdef PINMAP_IEC_NUGGET

// ESP32-WROVER-E-N16R8
#define FLASH_SIZE              16
#define PSRAM_SIZE              8

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_12 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_15 // fnSystem.h

#define PIN_SD_HOST_CS          GPIO_NUM_4  // LOLIN D32 Pro
#define PIN_SD_HOST_MISO        GPIO_NUM_19
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#define PIN_SD_HOST_SCK         GPIO_NUM_18

/* UART */
#define PIN_UART0_RX            GPIO_NUM_3  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_1
#define PIN_UART1_RX            GPIO_NUM_9
#define PIN_UART1_TX            GPIO_NUM_10
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0  // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_NC
#define PIN_BUTTON_C            GPIO_NUM_NC

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_2 // led.cpp
#define PIN_LED_BUS             GPIO_NUM_5
#define PIN_LED_BT              GPIO_NUM_13
#define PIN_LED_RGB             GPIO_NUM_13

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_25 // samlib.h

/* Commodore IEC Pins */
// CLK & DATA lines in/out are split between two pins
//#define IEC_SPLIT_LINES

// Line values are inverted (7406 Hex Inverter Buffer)
//#define IEC_INVERTED_LINES

// Reset line is available
#define IEC_HAS_RESET
                                                //            WIRING
                                                //  C64    DIN6    D32Pro          TFT
#define PIN_IEC_ATN             GPIO_NUM_32     //  ATN    3       A T-LED 32      10 (PURPLE)
#define PIN_IEC_CLK_IN          GPIO_NUM_33     //  CLK    4       A T-RST 33      8  (BROWN)
#define PIN_IEC_CLK_OUT         GPIO_NUM_33     //
#define PIN_IEC_DATA_IN         GPIO_NUM_14     //  DATA   5       T-CS 14         2  (BLACK)
#define PIN_IEC_DATA_OUT        GPIO_NUM_14     //
#define PIN_IEC_SRQ             GPIO_NUM_27     //  SRQ    1       T-DC 27         7  (ORANGE)
#define PIN_IEC_RESET           GPIO_NUM_34     //  RESET  6       A 34            N/C
                                                //  GND    2       GND             9  (GREY)

/* Modem/Parallel Switch */
/* Unused with Nugget    */
#define PIN_MODEM_ENABLE        GPIO_NUM_2  // High = Modem enabled
#define PIN_MODEM_UP9600        GPIO_NUM_15 // High = UP9600 enabled

#endif // PINMAP_IEC_NUGGET
#endif // PINMAP_IEC_NUGGET_H