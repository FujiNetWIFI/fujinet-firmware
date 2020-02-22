#include <Arduino.h>
#include <WiFi.h>
#include "tnfs.h"
#include "ssid.h"
#include "debug.h"

void setup() {
  // put your setup code here, to run once:
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
      Debug_println("Waiting for WiFi to connect...");
      delay(1000);
    }
    Debug_println("WiFi connected.");
    while (!UDP.begin(16384)) {
      Debug_println("Trying to start UDP again...");
      delay(1000);
    }
    Debug_println("UDP started.");
  
}

void loop() {
  // put your main code here, to run repeatedly:
}