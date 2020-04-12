#include <Arduino.h>
#include <WiFi.h>

#include <SPIFFS.h>
#include <SD.h>

#include "httpService.h"
#include "printer.h"
#include "../../src/main.h"

WiFiServer server(80);
WiFiClient client;

void httpServiceSetup() {
  server.begin(); // Start the web server
}

void httpService()
{
  int in;
  // listen for incoming clients
  client = server.available();
  if (client)
  {
#ifdef DEBUG_S
    BUG_UART.println("new HTTP client");
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

          sioPrinter *currentPrinter = getCurrentPrinter();
          currentPrinter->flushOutput();

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
          switch (currentPrinter->getPaperType())
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
          client.printf("Content-Length: %u\n", currentPrinter->getOutputSize());
          //client.println("Content-Disposition: inline");
          client.printf("\n"); // critical - end of header

          bool ok = true;
          while (ok)
          {
            in = currentPrinter->readFromOutput();
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
          currentPrinter->resetOutput();
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
