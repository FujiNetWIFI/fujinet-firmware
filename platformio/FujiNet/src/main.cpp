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

#define PRINTMODE PDF

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

#define TNFS_SERVER "192.168.1.12"
#define TNFS_PORT 16384

// DEBUGGING MACROS /////////////////////////////////////////////////////////////////////////
#ifdef DEBUG_S
#define Debug_print(...) BUG_UART.print( __VA_ARGS__ )
#define Debug_printf(...) BUG_UART.printf( __VA_ARGS__ )
#define Debug_println(...) BUG_UART.println( __VA_ARGS__ )
#define DEBUG
#endif
#ifdef DEBUG_N
#define Debug_print(...) wificlient.print( __VA_ARGS__ )
#define Debug_printf(...) wificlient.printf( __VA_ARGS__ )
#define Debug_println(...) wificlient.println( __VA_ARGS__ )
#define DEBUG
#endif
#ifndef DEBUG
#define Debug_print(...)
#define Debug_printf(...)
#define Debug_println(...)
#endif
/////////////////////////////////////////////////////////////////////////////////////////////


atari820 sioP;
File atr[2];
File paperf;
File tnfs;
sioDisk sioD[2];
sioModem sioR;

WiFiServer server(80);
WiFiClient client;

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
  BUG_UART.println("atariwifi started");
#endif

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(10);
  }
  BUG_UART.println(WiFi.localIP());
  server.begin();

  SPIFFS.begin();

  SIO.addDevice(&sioR, 0x50); // R:
  SIO.addDevice(&sioP, 0x40); // P:
  paperf = SPIFFS.open("/paper", "w+");
  sioP.initPrinter(&paperf, PRINTMODE);

  for (int i = 0; i < 1; i++)
  {
    String fname = String("/file") + String(i) + String(".atr");
#ifdef DEBUG_S
    BUG_UART.println(fname);
#endif
    atr[i] = SPIFFS.open(fname, "r+");
    sioD[i].mount(&atr[i]);
    SIO.addDevice(&sioD[i], 0x31 + i);
  }

  TNFS.begin(TNFS_SERVER, TNFS_PORT);
  tnfs = TNFS.open("/A820.ATR", "r+");
#ifdef DEBUG_S
  BUG_UART.println("tnfs/A820.ATR");
#endif
  sioD[1].mount(&tnfs);
  SIO.addDevice(&sioD[1], 0x31 + 1);

#ifdef DEBUG_S
  BUG_UART.print(SIO.numDevices());
  BUG_UART.println(" devices registered.");
#endif

  SIO.setup();
}

void loop()
{
  SIO.service();
  httpService();
}
