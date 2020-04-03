#ifndef MODEM_H
#define MODEM_H
#include <Arduino.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif
#ifdef ESP32
#include <WiFi.h>
#endif

#include "sio.h"

#define RING_INTERVAL 3000    // How often to print RING when having a new incoming connection (ms)
#define MAX_CMD_LENGTH 256    // Maximum length for AT command
#define TX_BUF_SIZE 256       // Buffer where to read from serial before writing to TCP (that direction is very blocking by the ESP TCP stack, so we can't do one byte a time.)

class sioModem : public sioDevice
{
  private:
    long modemBaud = 2400;          // Holds modem baud rate, Default 2400
    bool DTR = false;
    bool RTS = false;
    bool XMT = false;

    /* Modem Active Variables */
    String cmd = "";                  // Gather a new AT command to this string from serial
    bool cmdMode = true;              // Are we in AT command mode or connected mode
    bool cmdAtascii = false;          // last CMD contained an ATASCII EOL?
    bool telnet = false;              // Is telnet control code handling enabled
    unsigned short listenPort = 0;              // Listen to this if not connected. Set to zero to disable.
    WiFiClient tcpClient;             // Modem client
    WiFiServer tcpServer;           // Modem server
    unsigned long lastRingMs = 0;     // Time of last "RING" message (millis())
    char plusCount = 0;               // Go to AT mode at "+++" sequence, that has to be counted
    unsigned long plusTime = 0;       // When did we last receive a "+++" sequence
    uint8_t txBuf[TX_BUF_SIZE];
    bool blockWritePending = false;   // is a BLOCK WRITE pending for the modem?
    byte* blockPtr;                   // pointer in the block write (points somewhere in sector)

    void sio_relocator();             // $21, '!', Booter/Relocator download
    void sio_handler();               // $26, '&', Handler download
    void sio_poll_1();                // $3F, '?', Type 1 Poll
    void sio_control();               // $41, 'A', Control
    void sio_config();                // $42, 'B', Configure
    void sio_listen();                // $4C, 'L', Listen
    void sio_unlisten();              // $4D, 'M', Unlisten
    void sio_status() override;       // $53, 'S', Status
    void sio_write();                 // $57, 'W', Write
    void sio_stream();                // $58, 'X', Concurrent/Stream
    void sio_process() override;      // Process the command

    void modemCommand();              // Execute modem AT command

    // CR/EOL aware println() functions for AT mode
    void at_cmd_println(const char* s);
    void at_cmd_println(long int i);
    void at_cmd_println(String s);
    void at_cmd_println(IPAddress ipa);
  public:
#ifdef ESP8266
    void sioModem();
#endif
    bool modemActive = false;         // If we are in modem mode or not
    void sio_handle_modem();          // Handle incoming & outgoing data for modem
};

#endif
