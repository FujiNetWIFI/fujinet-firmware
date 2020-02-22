#include <Arduino.h>
#include <WiFi.h>
#include "tnfs.h"
#include "ssid.h"
#include "debug.h"

#define TNFS_SERVER "192.168.1.12"
#define TNFS_PORT 16384

#define DASHES "--------------------------------------------------------------"

TNFSFS TNFS;

void setup() {
  Debug_start();
  // put your setup code here, to run once:
  Debug_println("Starting WiFi ...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
      Debug_println("Waiting for WiFi to connect...");
      delay(1000);
    }
    Debug_println("WiFi connected. Starting UDP ...");
    while (!UDP.begin(16384)) {
      Debug_println("Trying to start UDP again...");
      delay(1000);
    }
    Debug_println("UDP started.");
  
  Debug_println(DASHES);
  Debug_println("Open-two-files test");
  Debug_println(DASHES);
  TNFS.begin(TNFS_SERVER);
  File f1 = TNFS.open("file1.txt","r");
  File f2 = TNFS.open("file2.txt","r");
  f1.close();
  f2.close();
  TNFS.end();

  Debug_println(DASHES);
  Debug_println("Open-file-open-dir-close-dir-open-file test");
  Debug_println(DASHES);
  TNFS.begin(TNFS_SERVER);
  f1 = TNFS.open("file1.txt","r");
  File dir = TNFS.open("/","r");
  dir.close();
  f2 = TNFS.open("file2.txt","r");
  TNFS.end();
  

  while(true){}

}

void loop() {
  // put your main code here, to run repeatedly:
}