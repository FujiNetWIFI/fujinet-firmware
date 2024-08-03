#ifndef BOARD_H_
#define BOARD_H_

#include "pico/platform.h"

#if DBG_SERIAL
#include <SoftwareSerial.h>
#endif

extern char pico_uid[];

// ADDR GPIO pins       :     GP2 - GP14
#define PINROMADDR    2
#define PINENABLE    14
#define ADDRWIDTH    13

// DATA GPIO pins       :     GP22 - GP29
#define DATAWIDTH     8
#define PINROMDATA   22

// UART debug pins      :     GP20 (RX), GP21 (TX)
#if DBG_SERIAL
#define DBG_UART_RX   20
#define DBG_UART_TX   21
#define DBG_UART_BAUDRATE   115200
extern SoftwareSerial dbgSerial;
#define dbg dbgSerial.printf
#endif

// SD card pins         :     GP16 (MISO), GP17 (CS), GP18 (SCK), GP19 (MOSI)
#if USE_SD_CARD
#define SD_MISO   16
#define SD_MOSI   19
#define SD_SCK    18
#define SD_CS     17
#define SD_SPEED  2000000L
#endif

// ESP UART pins        :     GP0 (TX), GP1 (RX), GP15 (RST)
#if USE_WIFI
#define espSerial Serial1
#define ESP_UART_TX        0
#define ESP_UART_RX        1
#define ESP_UART_BAUDRATE 115200
#define ESP_RESET_PIN     15
#endif

#endif
