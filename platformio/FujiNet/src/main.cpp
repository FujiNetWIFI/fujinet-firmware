#include <Arduino.h>

#include "ssid.h" // Define WIFI_SSID and WIFI_PASS in include/ssid.h. File is ignored by GIT
#include "sio.h"
#include "disk.h"
#include "tnfs.h"
#include "printer.h"
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

//File tnfs;
atari1027 sioP;
File atr[2];
File paperf;
File tnfs;
sioDisk sioD[2];

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
          
          // client.println("Connection: close");  // the connection will be closed after completion of the response
          client.println("Content-Type: application/pdf");
          //client.println("Server: FujiNet");
          client.printf("Content-Length: %u\n",paperf.size());
          // // client.println("Refresh: 5");  // refresh the page automatically every 5 sec
          // client.println();
          // client.println("<!DOCTYPE HTML>");
          // client.println("<html>");
          // client.println("Hello World!");
          // client.println("</html>");



          //client.println("Content-Type: application/octet-stream");
          //client.println("Content-Disposition: attachment; filename=\"test.pdf\"");
          client.println("Content-Disposition: inline");
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
  tnfs = TNFS.open("/printers.atr", "r");
#ifdef DEBUG_S
  BUG_UART.println("tnfs/printers.atr");
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
