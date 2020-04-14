/*
MAJOR REV 2 UPDATE
done        1. the FujiNet SIO 0x70 device (device slots, too)
need sio new disk        2. the SIO read/write/cmdFrame/etc. update
done        3. tnfs update  so we can have more than one server
            4. R device
            5. P: update (if needed with 2. SIO update)
done        6. percom inclusion in D devices
            7. HTTP server
hacked      8. SD card support
            9. convert debug messages

status:
moved over everything from multilator-rev2.ino except sio new disk
hacked in a special case for SD - set host as "SD" in the Atari config program
*/

#include <Arduino.h>

#include "ssid.h" // Define WIFI_SSID and WIFI_PASS in include/ssid.h. File is ignored by GIT
#include "sio.h"
#include "disk.h"
#include "tnfs.h"
#include "printer.h"
#include "modem.h"
#include "fuji.h"
#include "apetime.h"
#include "voice.h"
#include "../http/httpService.h"

//#include <WiFiUdp.h>

#ifdef ESP8266
#include <FS.h>
#include <ESP8266WiFi.h>
#include <SDFS.h>
#define INPUT_PULLDOWN INPUT_PULLDOWN_16 // for motor pin
#endif

#ifdef ESP32
#include <SPIFFS.h>
#include <SPI.h>
#include <WiFi.h>
#include "keys.h"
#include <SD.h>
#endif

#ifdef BLUETOOTH_SUPPORT
#include "bluetooth.h"
#endif

//#define TNFS_SERVER "192.168.1.12"
//#define TNFS_PORT 16384

sioModem sioR;

sioFuji theFuji;

sioApeTime apeTime;

sioVoice sioV;

// WiFiServer server(80);
// WiFiClient client;
#ifdef DEBUG_N
WiFiClient wifiDebugClient;
#endif

#ifdef ESP32
KeyManager keyMgr;
#endif

#ifdef BLUETOOTH_SUPPORT
BluetoothManager btMgr;
#endif

// We need something better than this,
// but it'll do for the moment...
// sioPrinter sioP;
// sioPrinter *getCurrentPrinter()
// {
//   return &sioP;
// }

void setup()
{

  // connect to wifi but DO NOT wait for it
  WiFi.begin(WIFI_SSID, WIFI_PASS);

#ifdef DEBUG_S
  BUG_UART.begin(DEBUG_SPEED);
  BUG_UART.println();
  BUG_UART.println("FujiNet PlatformIO Started");
#endif
  if (!SPIFFS.begin())
  {
#ifdef DEBUG
    Debug_println("SPIFFS Mount Failed");
#endif
  }

  if (!SD.begin(5))
  {
#ifdef DEBUG
    Debug_println("SD Card Mount Failed");
#endif
  }

#ifdef DEBUG
  Debug_print("SD Card Type: ");
  switch (SD.cardType())
  {
  case CARD_NONE:
    Debug_println("NONE");
    break;
  case CARD_MMC:
    Debug_println("MMC");
    break;
  case CARD_SD:
    Debug_println("SDSC");
    break;
  case CARD_SDHC:
    Debug_println("SDHC");
    break;
  default:
    Debug_println("UNKNOWN");
    break;
  }
#endif

  theFuji.begin();

  for (int i = 0; i < 8; i++)
  {
    SIO.addDevice(&sioD[i], 0x31 + i);
    SIO.addDevice(&sioN[i], 0x71 + i);
  }
  SIO.addDevice(&theFuji, 0x70); // the FUJINET!

  SIO.addDevice(&apeTime, 0x45); // apetime

  SIO.addDevice(&sioR, 0x50); // R:

  // Choose filesystem for P: device and iniitalize it
  if (SD.cardType() != CARD_NONE)
  {
    Debug_println("using SD card for printer storage");
    sioP.initPrinter(&SD);
  }
  else
  {
    Debug_println("using SPIFFS for printer storage");
    sioP.initPrinter(&SPIFFS);
  }
  SIO.addDevice(&sioP, 0x40); // P:

  // Choose filesystem for HTTP service and initialize it
  //httpServiceSetup();
  httpServiceInit();


  SIO.addDevice(&sioV, 0x43); // P3:

  if (WiFi.status() == WL_CONNECTED)
  {
#ifdef DEBUG_S
    BUG_UART.println(WiFi.localIP());
#endif
    UDP.begin(16384);
  }

#ifdef DEBUG_S
  BUG_UART.print(SIO.numDevices());
  BUG_UART.println(" devices registered.");
#endif

  SIO.setup();
#if defined(DEBUG) && defined(ESP32)
  Debug_print("SIO Voltage: ");
  Debug_println(SIO.sio_volts());
#endif

#ifdef BLUETOOTH_SUPPORT
  btMgr.setup();
#endif

  void sio_flush();
}

void loop()
{
#ifdef DEBUG_N
  /* Connect to debug server if we aren't and WiFi is connected */
  if (!wifiDebugClient.connected() && WiFi.status() == WL_CONNECTED)
  {
    wifiDebugClient.connect(DEBUG_HOST, 6502);
    wifiDebugClient.println("FujiNet PlatformIO");
  }
#endif

#ifdef ESP32
  if (WiFi.status() == WL_CONNECTED)
    digitalWrite(PIN_LED1, LOW);
  else
    digitalWrite(PIN_LED1, HIGH);

  switch (keyMgr.getBootKeyStatus())
  {
  case eKeyStatus::LONG_PRESSED:
#ifdef DEBUG
    Debug_println("LONG PRESS");
#endif
#ifdef BLUETOOTH_SUPPORT
    if (btMgr.isActive())
    {
      btMgr.stop();
    }
    else
    {
      btMgr.start();
    }
#endif
    break;
  case eKeyStatus::SHORT_PRESSED:
#ifdef DEBUG
    Debug_println("SHORT PRESS");
#endif
#ifdef BLUETOOTH_SUPPORT
    if (btMgr.isActive())
    {
      btMgr.toggleBaudrate();
    }
    else
#endif
    {
      theFuji.image_rotate();
    }
    break;
  default:
    break;
  }

#ifdef BLUETOOTH_SUPPORT
  if (btMgr.isActive())
  {
    btMgr.service();
  }
  else
#endif
  {
#endif // ESP32
    SIO.service();
    //httpService();
#ifdef ESP32
  }
#endif
}
