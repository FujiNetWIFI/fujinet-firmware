#include <Arduino.h>

#include "ssid.h" // Define WIFI_SSID and WIFI_PASS in include/ssid.h. File is ignored by GIT
#include "sio.h"

#ifdef ESP_8266
#include <FS.h>
#define INPUT_PULLDOWN INPUT_PULLDOWN_16 // for motor pin
#elif defined(ESP_32)
#include <SPIFFS.h>
#endif

File atr;

void setup()
{
  SPIFFS.begin();
  atr = SPIFFS.open("/autorun.atr", "r");

  // Set up pins
#ifdef DEBUG_S
  BUG_UART.begin(DEBUG_SPEED);
  BUG_UART.println();
  BUG_UART.println("atariwifi started");
#else
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
#endif


  setup_sio();
}

void loop()
{
  handle_sio();
}
