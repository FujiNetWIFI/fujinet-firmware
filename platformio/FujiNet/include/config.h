#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// serial debug
#define DEBUG_S

#ifdef DEBUG_S
#ifdef ESP_8266
#define BUG_UART Serial1
#elif defined(ESP_32)
#define BUG_UART Serial
#endif
#endif

// SIO port
#ifdef ESP_8266
#define SIO_UART Serial
#elif defined(ESP_32)
#define SIO_UART Serial2
#endif

// pin configurations
// esp8266
#ifdef ESP_8266
#define PIN_LED 2
#define PIN_INT 5
#define PIN_PROC 4
#define PIN_MTR 16
#define PIN_CMD 12
// esp32
#elif defined(ESP_32)
#define PIN_LED 2
#define PIN_INT 26
#define PIN_PROC 22
#define PIN_MTR 33
#define PIN_CMD 21
#endif

//
#endif
