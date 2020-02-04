/*
MAJOR REV 2 UPDATE
            1. the FujiNet SIO 0x70 device (device slots, too)
in process  2. the SIO read/write/cmdFrame/etc. update
            3. tnfs update  so we can have more than one server
            4. R device
            5. P: update (if needed with 2. SIO update)
            6. percom inclusion in D devices
            7. HTTP server
            8. SD card support
            9. convert debug messages

status:
#2 is parially implemented: changed the command frame reading, ack & nak in the sio_process()
moved the new sio_to_peripheral and sio_to_computer over to the sioDevice
updated disk sio_status(), sio_format(),
folded in some of sio_write() but left out tnfs caching and atrConfig marked by todo
updated sio_read() to use new sectorSize and sio_to_computer() features - marked caching by todo

*/

/**
 * The load_config state is set TRUE on FujiNet power-on/reset.
 * A load_config==TRUE throws D1: requests to the FujiNet device.
 * When FujiNet sends data about whats in device slots, it sets load_config to FALSE.
 * That must allow D1: to boot from a TNFS image on Atari pwer-on/reset.
*/
#include <Arduino.h>

#include "ssid.h" // Define WIFI_SSID and WIFI_PASS in include/ssid.h. File is ignored by GIT
#include "sio.h"
#include "disk.h"
#include "tnfs.h"
#include "printer.h"
#include "modem.h"
#include "fuji.h"

#define PRINTMODE PDF

#ifdef ESP8266
#include <FS.h>
#include <ESP8266WiFi.h>
#define INPUT_PULLDOWN INPUT_PULLDOWN_16 // for motor pin
#endif

#ifdef ESP32
#include <SPIFFS.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#endif

#define TNFS_SERVER "192.168.1.12"
#define TNFS_PORT 16384

atari820 sioP;
File atr[2];
File paperf;
File tnfs;
sioDisk sioD[2];
sioModem sioR;
sioFuji theFuj;

WiFiServer server(80);
WiFiClient client;
#ifdef DEBUG_N
WiFiClient wifiDebugClient;
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
          default:
            exts = "pdf";
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
          paperf = SPIFFS.open("/paper", "w+");
          sioP.initPrinter(&paperf, PRINTMODE);
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
#ifdef DEBUG_S
  BUG_UART.begin(DEBUG_SPEED);
  BUG_UART.println();
  BUG_UART.println("FujiNet PlatformIO Started");
#endif

  // connect to wifi but DO NOT wait for it
  WiFi.begin(WIFI_SSID, WIFI_PASS);
#ifdef DEBUG_S
  if (WiFi.status() == WL_CONNECTED)
    BUG_UART.println(WiFi.localIP());
#endif

  server.begin(); // Start the web server

  SPIFFS.begin();

  SIO.addDevice(&theFuj, 0x70); // the FUJINET!
  SIO.addDevice(&sioR, 0x50);   // R:
  SIO.addDevice(&sioP, 0x40);   // P:
  paperf = SPIFFS.open("/paper", "w+");
  sioP.initPrinter(&paperf, PRINTMODE);

  SPIFFS.begin();
  atr[0] = SPIFFS.open("/autorun.atr", "r+");
  sioD[0].mount(&atr[0]);
#ifdef DEBUG_S
  BUG_UART.println("/autorun.atr");
#endif
  SIO.addDevice(&sioD[0], 0x31 + 0);
  //   for (int i = 0; i < 1; i++)
  //   {
  //     String fname = String("/file") + String(i) + String(".atr");
  // #ifdef DEBUG_S
  //     BUG_UART.println(fname);
  // #endif
  //     atr[i] = SPIFFS.open(fname, "r+");
  //     sioD[i].mount(&atr[i]);
  //     SIO.addDevice(&sioD[i], 0x31 + i);
  //   }

  TNFS.begin(TNFS_SERVER, TNFS_PORT);
  tnfs = TNFS.open("/A820.ATR", "r+");
#ifdef DEBUG_S
  BUG_UART.println("tnfs/A820.ATR");
#endif
  sioD[1].mount(&tnfs);
  SIO.addDevice(&sioD[1], 0x31 + 1);

  /*
  if(!SD.begin(5))
  {
#ifdef DEBUG
    Debug_println("SD Card Mount Failed");
#endif
    // Revert to SPIFFS
    SPIFFS.begin();
    atr[0] = SPIFFS.open("/autorun.atr", "r+");
    sioD[0].mount(&atr[0]);
  }
  else
  {
    atr[0] = SD.open("/autorun.atr", "r+");
    if (!atr[0])
    {
#ifdef DEBUG
      Debug_println("Unable to mount autorun.atr from SD Card");
#endif
      // Revert to SPIFFS
      SPIFFS.begin();
      atr[0] = SPIFFS.open("/autorun.atr", "r+");
      sioD[0].mount(&atr[0]);
    }
    else
    {
      sioD[0].mount(&atr[0]);
#ifdef DEBUG
      Debug_println("Mounted autorun.atr from SD Card");
#endif
    }
#ifdef DEBUG
    Debug_print("SD Card Type: ");
    switch (SD.cardType())
    {
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
  }
  SIO.addDevice(&sioD[0], 0x31 + 0);
*/

#ifdef DEBUG_S
  BUG_UART.print(SIO.numDevices());
  BUG_UART.println(" devices registered.");
#endif

  SIO.setup();
#ifdef DEBUG
  Debug_print("SIO Voltage: ");
  Debug_println(SIO.sio_volts());
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

  if (WiFi.status() == WL_CONNECTED)
    digitalWrite(PIN_LED1, LOW);
  else
    digitalWrite(PIN_LED1, HIGH);

  SIO.service();
  httpService();
}
