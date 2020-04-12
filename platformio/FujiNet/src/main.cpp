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

//#include <WiFiUdp.h>

#define PRINTMODE RAW

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

sioPrinter sioP;
File paperf;
FS *paperFS;

sioModem sioR;

sioFuji theFuji;

sioApeTime apeTime;

WiFiServer server(80);
WiFiClient client;
#ifdef DEBUG_N
WiFiClient wifiDebugClient;
#endif

#ifdef ESP32
KeyManager keyMgr;
#endif

#ifdef BLUETOOTH_SUPPORT
BluetoothManager btMgr;
#endif

void httpService()
{
  int in;
  // listen for incoming clients
  client = server.available();
  if (client)
  {
#ifdef DEBUG_S
    BUG_UART.println("new client");
#endif
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected())
    {
      if (client.available())
      {
        char c = client.read();
#ifdef DEBUG_S
        BUG_UART.write(c);
#endif
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank)
        {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");

          sioP.pageEject();
          paperf.flush();
          paperf.seek(0);

          client.println("Connection: close"); // the connection will be closed after completion of the response
          //client.println("Content-Type: application/pdf");
          client.println("Server: FujiNet");
          // // client.println("Refresh: 5");  // refresh the page automatically every 5 sec
          // client.println();
          // client.println("<!DOCTYPE HTML>");
          // client.println("<html>");
          // client.println("Hello World!");
          // client.println("</html>");

          std::string exts;
          switch (sioP.getPaperType())
          {
          case RAW:
            exts = "bin";
            break;
          case TRIM:
            exts = "atascii";
            break;
          case ASCII:
            exts = "txt";
            break;
          case PDF:
            exts = "pdf";
            break;
          case SVG:
            exts = "svg";
            break;
          default:
            exts = "bin";
          }

          client.println("Content-Type: application/octet-stream");
          client.printf("Content-Disposition: attachment; filename=\"test.%s\"\n", exts.c_str());
          client.printf("Content-Length: %u\n", paperf.size());
          //client.println("Content-Disposition: inline");
          client.printf("\n"); // critical - end of header

          bool ok = true;
          while (ok)
          {
            in = paperf.read();
            if (in == -1)
            {
              ok = false;
            }
            else
            {
              client.write(byte(in));
#ifdef DEBUG_S
              BUG_UART.write(byte(in));
#endif
            }
          }
          paperf.close();
          paperf = paperFS->open("/paper", "w+");
          sioP.setPaper(PRINTMODE);
          sioP.initPrinter(&paperf);
          break;
        }
        if (c == '\n')
        {
          // you're starting a new line
          currentLineIsBlank = true;
        }
        else if (c != '\r')
        {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();
#ifdef DEBUG_S
    BUG_UART.println("client disconnected");
#endif
  }
}

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

  SIO.addDevice(&sioP, 0x40); // P:

  if (SD.cardType() != CARD_NONE)
  {
     paperFS = &SD;
     Debug_println("using SD card for printer");
  }
  else
  {
   paperFS = &SPIFFS;
   Debug_println("using SPIFFS  for printer");
  }
  paperf = paperFS->open("/paper", "w+");
  if (paperf)
  {
    Debug_println("printer output file opened");
  }
  else
  {
    Debug_println("error opening printer file");
  }

  sioP.setPaper(PRINTMODE);
  sioP.initPrinter(&paperf);
  server.begin(); // Start the web server

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
    httpService();
#ifdef ESP32
  }
#endif
}
