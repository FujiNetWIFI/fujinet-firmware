#include <Arduino.h>

#include "config.h"
#include "ssid.h"

#ifdef  ESP_8266
#include <FS.h>
#elif defined(ESP_32)
#include <SPIFFS.h>
#endif


void setup() {
  // put your setup code here, to run once:
}

void loop() {
  // put your main code here, to run repeatedly:
}