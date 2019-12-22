#include <Arduino.h>

#include "ssid.h" // Define WIFI_SSID and WIFI_PASS in include/ssid.h. File is ignored by GIT
#include "sio.h"
#include "disk.h"
#include "tnfs.h"
#include "printer.h"
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

//File tnfs;
sioPrinter sioP;
File atr[8];
//File tnfs;
sioDisk sioD[8];

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
  SIO.addDevice(&sioP, 0x40); // P:
  for (int i = 0; i < 8; i++)
  {
    String fname = String("/file") + String(i) + String(".atr");
#ifdef DEBUG_S
    BUG_UART.println(fname);
#endif
    atr[i] = SPIFFS.open(fname, "r+");
    sioD[i].mount(&atr[i]);
    SIO.addDevice(&sioD[i], 0x31 + i);
  }
#ifdef DEBUG_S
  BUG_UART.print(SIO.numDevices());
  BUG_UART.println(" devices registered.");
#endif
      //TNFS.begin(TNFS_SERVER, TNFS_PORT);
      //tnfs = TNFS.open("/TurboBasic.atr", "r");

      SIO.setup();
}

void loop()
{
  SIO.service();
}
