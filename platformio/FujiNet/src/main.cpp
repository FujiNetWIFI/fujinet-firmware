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
#include "httpService.h"
#include "fnSystem.h"
#include "fnWiFi.h"

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
//#include <WiFi.h>
#include <SD.h>
#endif

#include "keys.h"
#include "led.h"

#ifdef BLUETOOTH_SUPPORT
#include "bluetooth.h"
#endif

#ifdef BOARD_HAS_PSRAM
#include <esp_spiram.h>
#include <esp_himem.h>
#endif

//#define TNFS_SERVER "192.168.1.12"
//#define TNFS_PORT 16384

// sioP is declared and defined in printer.h/cpp
sioModem sioR;
sioFuji theFuji;
sioApeTime apeTime;
sioVoice sioV;
fnHttpService fnHTTPD;

#ifdef DEBUG_N
WiFiClient wifiDebugClient;
#endif

KeyManager keyMgr;
LedManager ledMgr;

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
#ifdef DEBUG_S
  BUG_UART.begin(DEBUG_SPEED);
#endif
#ifdef DEBUG
  Debug_println("\n--%--%--%--\nFujiNet PlatformIO Started");
  Debug_printf("Starting heap: %u\n", fnSystem.get_free_heap_size());  
  #ifdef BOARD_HAS_PSRAM
  Debug_printf("PsramSize %u\n", ESP.getPsramSize());
  //Debug_printf("spiram size %u\n", esp_spiram_get_size());
  //Debug_printf("himem free %u\n", esp_himem_get_free_size());
  Debug_printf("himem phys %u\n", esp_himem_get_phys_size());
  Debug_printf("himem reserved %u\n", esp_himem_reserved_area_size());
  #endif
#endif
  // connect to wifi but DO NOT wait for it
  //WiFi.begin(WIFI_SSID, WIFI_PASS);
  fnWiFi.setup();
  // fnWiFi.start(WIFI_SSID, WIFI_PASS);

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


  // PRINTERS!!!!!!!!!!!
  // for 820 or 822 - need special pointer back so printer can access SIO aux value
  //atari820* P = new(atari820);
  //atari822* P = new(atari822);
  //P->setDevice(&sioP);
  //sioP.connect_printer(P);
  
  // atari 1027
  sioP.connect_printer(new(atari1027));

  // png printer for ClausB's GRANTIC screen dump
  //sioP.connect_printer(new(pngPrinter));
  
  // file printer for printer SIO capture
  //sioP.connect_printer(new(filePrinter));

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

  SIO.addDevice(&sioV, 0x43); // P3:

  if (fnWiFi.connected())
  {
#ifdef DEBUG
    Debug_printf("IP address obtained: %s\n", fnSystem.Net.get_ip4_address_str().c_str());
#endif
    UDP.begin(16384);
  }

#ifdef DEBUG
  Debug_printf("%d devices registered\n", SIO.numDevices());
#endif

  SIO.setup();
#if defined(DEBUG) && defined(ESP32)
  Debug_print("SIO Voltage: ");
  Debug_println(SIO.sio_volts());
#endif

  keyMgr.setup();
  ledMgr.setup();

  void sio_flush();

#ifdef DEBUG
    Debug_printf("Available heap: %u\n", fnSystem.get_free_heap_size());
#endif
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
  if (fnWiFi.connected())
  {
    ledMgr.set(eLed::LED_WIFI, true);
    if(!fnHTTPD.running())
      fnHTTPD.start();
  }
  else
  {
    ledMgr.set(eLed::LED_WIFI, false);
    if(fnHTTPD.running())
      fnHTTPD.stop();
  }

  switch (keyMgr.getKeyStatus(eKey::OTHER_KEY))
  {
    case eKeyStatus::LONG_PRESSED:
#ifdef DEBUG
      Debug_println("O_KEY: LONG PRESS");
#endif
      break;
    case eKeyStatus::SHORT_PRESSED:
#ifdef DEBUG
      Debug_println("O_KEY: SHORT PRESS");
#endif
      break;
    default:
      break;
  }

  switch (keyMgr.getKeyStatus(eKey::BOOT_KEY))
  {
  case eKeyStatus::LONG_PRESSED:
#ifdef DEBUG
    Debug_println("B_KEY: LONG PRESS");
#endif
#ifdef BLUETOOTH_SUPPORT
    if (btMgr.isActive())
    {
      btMgr.stop();
#ifdef BOARD_HAS_PSRAM
      ledMgr.set(eLed::LED_BT, false);
#else
      ledMgr.set(eLed::LED_SIO, false);
#endif
    }
    else
    {
#ifdef BOARD_HAS_PSRAM
      ledMgr.set(eLed::LED_BT, true);
#else
      ledMgr.set(eLed::LED_SIO, true); // SIO LED always ON in Bluetooth mode
#endif
      btMgr.start();
    }
#endif
    break;
  case eKeyStatus::SHORT_PRESSED:
#ifdef DEBUG
    Debug_println("B_KEY: SHORT PRESS");
  #ifdef BOARD_HAS_PSRAM
    ledMgr.blink(eLed::LED_BT); // blink to confirm a button press
  #else
    ledMgr.blink(eLed::LED_SIO); // blink to confirm a button press
  #endif
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
#ifdef ESP32
  }
#endif
}
