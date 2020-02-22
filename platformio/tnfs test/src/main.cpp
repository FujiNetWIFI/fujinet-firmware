#include <Arduino.h>
#include <WiFi.h>
#include "tnfs.h"
#include "ssid.h"
#include "debug.h"

#define TNFS_SERVER "192.168.1.12"
#define TNFS_PORT 16384

TNFSFS TNFS;

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
  
  TNFS.begin(TNFS_SERVER);
  File f1 = TNFS.open("file1.txt","r");
  File f2 = TNFS.open("file2.txt","r");
}

void loop() {
  // put your main code here, to run repeatedly:
}