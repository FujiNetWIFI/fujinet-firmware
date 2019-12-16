#include <Arduino.h>

#include "ssid.h" // Define WIFI_SSID and WIFI_PASS in include/ssid.h. File is ignored by GIT
#include "sio.h"
#include "disk.h"
#include "tnfs.h"
// #ifdef ESP_8266
// #include <FS.h>
// #define INPUT_PULLDOWN INPUT_PULLDOWN_16 // for motor pin
// #elif defined(ESP_32)
#include <SPIFFS.h>
// #endif

//#ifdef ESP_8266
//#include <ESP8266WiFi.h>
//#elif defined(ESP_32)
#include <WiFi.h>
//#endif

#define TNFS_SERVER "192.168.1.11"
#define TNFS_PORT 16384

File atr;
File tnfs;
sioDisk sioD1(0x31,String("D1:"));

void setup()
{
#ifdef DEBUG_S
  BUG_UART.begin(DEBUG_SPEED);
  BUG_UART.println();
  BUG_UART.println("atariwifi started");
#else
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
#endif

  WiFi.begin(WIFI_SSID, WIFI_PASS);
   while (WiFi.status() != WL_CONNECTED)
  {
    delay(10);
  }
  
  SPIFFS.begin();
  atr = SPIFFS.open("/autorun.atr", "r+");

  TNFS.begin(TNFS_SERVER,TNFS_PORT);
  tnfs = TNFS.open("/miner.atr","r");

  sioD1.mount(&tnfs);
  SIO.setup();
}

void loop()
{
  sioD1.service();
}
