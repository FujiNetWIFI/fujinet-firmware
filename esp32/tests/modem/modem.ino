/*
   ESP8266 based virtual modem
   Copyright (C) 2016 Jussi Salin <salinjus@gmail.com>

   Modified for FujiNet Atari SIO
   Copyright (C) 2019 Joe Honold <mozzwald@gmail.com>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define TEST_NAME "#FujiNet WiFi Modem"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif
#ifdef ESP32
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <SPIFFS.h>
#endif

#include <algorithm>

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT} cmdState;

// Uncomment for Serial Debug
//#define DEBUG_S

#ifdef ESP8266
#define SIO_UART Serial
#define BUG_UART Serial1
#define PIN_LED2        2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12
#define PIN_CKO         2
#define PIN_CKI        14
#endif
#ifdef ESP32
#define SIO_UART Serial2
#define BUG_UART Serial
#define PIN_LED1         2
#define PIN_LED2         4
#define PIN_INT         26
#define PIN_PROC        22
#define PIN_MTR         33
#define PIN_CMD         21
#define PIN_CKO         32
#define PIN_CKI         27
#endif

#define DELAY_T5          1500
#define READ_CMD_TIMEOUT  12
#define CMD_TIMEOUT       50
#define STATUS_SKIP       8
#define SIO_BAUD          19200

bool sioBaud = false;
unsigned long checkTimer = 0;
bool modemActive = false;
int checkCounter = 0;

byte sector[128];
File atr;

/**
   A Single command frame, both in structured and unstructured
   form.
*/
union
{
  struct
  {
    unsigned char devic;
    unsigned char comnd;
    unsigned char aux1;
    unsigned char aux2;
    unsigned char cksum;
  };
  byte cmdFrameData[5];
} cmdFrame;

#ifdef DEBUG_S
#define Debug_print(...) BUG_UART.print( __VA_ARGS__ )
#define Debug_printf(...) BUG_UART.printf( __VA_ARGS__ )
#define Debug_println(...) BUG_UART.println( __VA_ARGS__ )
#define DEBUG
#endif

// Global variables (WiFi Modem)
String cmd = "";           // Gather a new AT command to this string from serial
bool cmdMode = true;       // Are we in AT command mode or connected mode
bool telnet = true;        // Is telnet control code handling enabled
#define SWITCH_PIN 0       // GPIO0 (programmind mode pin)
#define DEFAULT_BPS 2400 // 2400 safe for all old computers including C64
#define LISTEN_PORT 23     // Listen to this if not connected. Set to zero to disable.
#define RING_INTERVAL 3000 // How often to print RING when having a new incoming connection (ms)
WiFiClient tcpClient;
WiFiServer tcpServer(LISTEN_PORT);
unsigned long lastRingMs = 0; // Time of last "RING" message (millis())
long myBps;                // What is the current BPS setting
#define MAX_CMD_LENGTH 256 // Maximum length for AT command
char plusCount = 0;        // Go to AT mode at "+++" sequence, that has to be counted
unsigned long plusTime = 0;// When did we last receive a "+++" sequence
//#define LED_PIN 2          // Status LED
#define LED_TIME 1         // How many ms to keep LED on at activity
unsigned long ledTime = 0;
#define TX_BUF_SIZE 256    // Buffer where to read from serial before writing to TCP
// (that direction is very blocking by the ESP TCP stack,
// so we can't do one byte a time.)
uint8_t txBuf[TX_BUF_SIZE];

// Telnet codes
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe


/**
   calculate 8-bit checksum.
*/
byte sio_checksum(byte* chunk, int length)
{
  int chkSum = 0;
  for (int i = 0; i < length; i++) {
    chkSum = ((chkSum + chunk[i]) >> 8) + ((chkSum + chunk[i]) & 0xff);
  }
  return (byte)chkSum;
}

/**
   ISR for falling COMMAND
*/
void ICACHE_RAM_ATTR sio_isr_cmd()
{
  cmdState = ID;
  modemActive = false;
}

/**
   Return true if valid device ID
*/
bool sio_valid_device_id()
{
  if (cmdFrame.devic == 0x31)
    return true;
  else
    return false;
}

/**
   Get the whole command frame
*/

void sio_get_cmd_frame()
{
  int i = 0;
  bool madeit = false;

  if (!sioBaud)
  {
    sioBaud = true;
    SIO_UART.updateBaudRate(SIO_BAUD);
#ifdef DEBUG
    Debug_println("BAUD: SIO");
#endif
  }

  //while( !digitalRead(PIN_CMD) ) // until PIN_CMD goes high again
  while ( i < 5 ) // until we've read 5 bytes from serial
  {
    switch (i)
    {
      case 0: // DEVICE ID
        while (!SIO_UART.available()) { delayMicroseconds(1); }
        cmdFrame.devic = SIO_UART.read();
#ifdef DEBUG
        Debug_print("CMD DEVC: ");
        Debug_println(cmdFrame.devic, HEX);
#endif
        break;
      case 1: // COMMAND
        while (!SIO_UART.available()) { delayMicroseconds(1); }
        cmdFrame.comnd = SIO_UART.read();
#ifdef DEBUG
        Debug_print("CMD CMD: ");
        Debug_println(cmdFrame.comnd, HEX);
#endif
        break;
      case 2: // AUX1
        while (!SIO_UART.available()) { delayMicroseconds(1); }
        cmdFrame.aux1 = SIO_UART.read();
#ifdef DEBUG
        Debug_print("CMD AUX1: ");
        Debug_println(cmdFrame.aux1, HEX);
#endif
        break;
      case 3: // AUX2
        while (!SIO_UART.available()) { delayMicroseconds(1); }
        cmdFrame.aux2 = SIO_UART.read();
#ifdef DEBUG
        Debug_print("CMD AUX2: ");
        Debug_println(cmdFrame.aux2, HEX);
#endif
        break;
      case 4: // CHECKSUM
        while (!SIO_UART.available()) { delayMicroseconds(1); }
        cmdFrame.cksum = SIO_UART.read();
        madeit = true;
#ifdef DEBUG
        Debug_print("CMD CKSUM: ");
        Debug_println(cmdFrame.cksum, HEX);
#endif
        break;
    }
    i++;
    yield();
  }
  if (!madeit) // we didn't get all the bits, junk this command frame
    cmdState = WAIT;
  else
    sio_process();
}

/**
   Status
*/
void sio_status()
{
  byte status[4] = {0x10, 0xFF, 0xFE, 0x00};
  byte ck;

  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();
  delayMicroseconds(200);
  //delay(1);

  // Write data frame
  for (int i = 0; i < 4; i++)
    SIO_UART.write(status[i]);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
   Read
*/
void sio_read()
{
  byte ck;
  int offset = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  offset *= 128;
  offset -= 128;
  offset += 16; // skip 16 byte ATR Header

  atr.seek(offset, SeekSet);
  atr.read(sector, 128);


  ck = sio_checksum((byte *)&sector, 128);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(sector, 128);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
#ifdef DEBUG
  Debug_print("SIO READ OFFSET: ");
  Debug_print(offset);
  Debug_print(" - ");
  Debug_println((offset + 128));
#endif
}

/**
   Write, called for both W and P commands.
*/
void sio_write()
{
  byte ck;
  int offset = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  offset *= 128;
  offset -= 128;
  offset += 16; // skip 16 byte ATR Header

#ifdef DEBUG
  Debug_println("RECV 128b data frame");
#endif

  SIO_UART.readBytes(sector, 128); // blocking
  while (!SIO_UART.available()) { delayMicroseconds(1); } // wait for it...
  ck = SIO_UART.read(); // Read checksum

  if (ck == sio_checksum(sector, 128))
  {
    SIO_UART.write('A'); // Write ACK
    delayMicroseconds(DELAY_T5);

    atr.seek(offset, SeekSet);
    atr.write(sector, 128);
    atr.flush();

    SIO_UART.write('C');
    yield();
  }
  else
    SIO_UART.write('N'); // Write NAK
}

/**
   format (fake)
*/
void sio_format()
{
  byte ck;

  for (int i = 0; i < 128; i++)
    sector[i] = 0;

  sector[0] = 0xFF; // no bad sectors.
  sector[1] = 0xFF;

  ck = sio_checksum((byte *)&sector, 128);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(sector, 128);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
#ifdef DEBUG
  Debug_printf("We faked a format.\n");
#endif
}

/**
   Send an acknowledgement
*/
void sio_ack()
{
  delayMicroseconds(500);
  SIO_UART.write('A');
  SIO_UART.flush();
}

/**
   Send a non-acknowledgement
*/
void sio_nak()
{
  delayMicroseconds(500);
  SIO_UART.write('N');
  SIO_UART.flush();
  cmdState = WAIT;
}

void sio_process()
{
  if (sio_valid_device_id() && (sio_checksum((byte *)&cmdFrame.cmdFrameData, 4) == cmdFrame.cksum))
  {
    sio_ack(); // Valid Device and Good Checksum
    switch (cmdFrame.comnd)
    {
      case 'P':
      case 'W':
        sio_write();
        break;
      case 'R':
        sio_read();
        break;
      case 'S':
        sio_status();
        break;
      case '!':
        sio_format();
        break;
    }
  }
  else
    sio_nak(); // Invalid Device or Bad Checksum

  SIO_UART.flush();
  cmdState = WAIT;
}

/**
   Arduino main init function
*/
void setup()
{
#ifdef DEBUG_S
  BUG_UART.begin(115200);
  BUG_UART.println();
  BUG_UART.println(TEST_NAME);
#endif

#ifdef DEBUG
#ifdef ESP32
  Debug_print("CPU Speed: ");
  Debug_println(getCpuFrequencyMhz()); //Get CPU clock
#endif
#endif

  SPIFFS.begin();
  atr = SPIFFS.open("/autorun.atr", "r+");

  // Set up pins
  pinMode(PIN_INT, OUTPUT);
  digitalWrite(PIN_INT, HIGH);
  pinMode(PIN_PROC, OUTPUT);
  digitalWrite(PIN_PROC, HIGH);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);
  pinMode(PIN_CKI, OUTPUT);
  digitalWrite(PIN_CKI, LOW);
#ifdef ESP32
  pinMode(PIN_CKO, INPUT);
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  digitalWrite(PIN_LED1, HIGH); // off
  digitalWrite(PIN_LED2, HIGH); // off
#endif

  // Attach COMMAND interrupt.
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_isr_cmd, FALLING);
  cmdState = WAIT; // Start in wait state

  // Setup Serial, start in SIO baud/mode
  SIO_UART.begin(SIO_BAUD);
  sioBaud = true;
#ifdef ESP8266
  SIO_UART.swap();
#endif
  myBps = DEFAULT_BPS;

#ifdef ESP32
  digitalWrite(PIN_LED2, HIGH);
#endif

  // Lastly, try connecting to WiFi. Should autoconnect to last ssid/pass
#ifdef ESP32
  WiFi.begin();
#endif
}

/**
   Turn on the LED and store the time, so the LED will be shortly after turned off
*/
void led_on(void)
{
#ifdef ESP32
  digitalWrite(PIN_LED2, LOW);
#endif
  ledTime = millis();
}


/**
   Arduino main loop function
*/
void loop()
{
  /**** Get the SIO Command ****/
  if (cmdState != WAIT)
  { // CMD Line Interrupt, get the whole command frame...
    sio_get_cmd_frame();
  }
  else if (!modemActive)
  { // get incoming serial and look for trigger to enter modem mode
    if (SIO_UART.available())
    {
      char chr = SIO_UART.read();

      if (chr == '\r' && checkCounter == 1)
      { // we got 'f' and '\r', begin switch to modem mode
        SIO_UART.printf("Switching to %i BAUD in 3 seconds..\n\r", myBps);
        checkTimer = millis();
        checkCounter = 0;
#ifdef DEBUG
        Debug_println("MODEM ACTIVATION ACCEPTED");
#endif
      }
      else if (chr == 'f')
      { // We got the first character ('f') of modem activation sequence
        checkCounter = 1;
#ifdef DEBUG
        Debug_println("MODEM ACTIVE STEP 1");
#endif
      }
      else
      { // this character isn't for us to activate modem, forget about it and start over
        checkCounter = 0;
#ifdef DEBUG
        Debug_println("MODEM CHECK OVER");
#endif
      }
    }
    if (checkTimer != 0 && millis() - checkTimer > 3000)
    { // Entering modem mode after 3 second delay
      modemActive = true;
      checkTimer = 0;
      sioBaud = false;
      SIO_UART.updateBaudRate(myBps);
#ifdef DEBUG
      Debug_printf("BAUD: %i\n", myBps);
      Debug_println("MODEM ACTIVE");
#endif
    }
  }

  /**** AT command mode ****/
  if (cmdMode == true && modemActive)
  {
    // In command mode but new unanswered incoming connection on server listen socket
    if ((LISTEN_PORT > 0) && (tcpServer.hasClient()))
    {
      // Print RING every now and then while the new incoming connection exists
      if ((millis() - lastRingMs) > RING_INTERVAL)
      {
        SIO_UART.println("RING");
        lastRingMs = millis();
      }
    }

    // In command mode - don't exchange with TCP but gather characters to a string
    if (SIO_UART.available())
    {
      char chr = SIO_UART.read();

      // Return, enter, new line, carriage return.. anything goes to end the command
      if ((chr == '\n') || (chr == '\r'))
      {
#ifdef DEBUG
        Debug_print(cmd);
        Debug_println(" | CR");
#endif
        command();
      }
      // Backspace or delete deletes previous character
      else if ((chr == 8) || (chr == 127))
      {
        cmd.remove(cmd.length() - 1);
        // We don't assume that backspace is destructive
        // Clear with a space
        SIO_UART.write(8);
        SIO_UART.write(' ');
        SIO_UART.write(8);
      }
      else
      {
        if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
        SIO_UART.print(chr);
      }
    }
  }
  /**** Connected mode ****/
  else if ( modemActive )
  {
    // Transmit from terminal to TCP
    if (SIO_UART.available())
    {
      led_on();

      // In telnet in worst case we have to escape every byte
      // so leave half of the buffer always free
      int max_buf_size;
      if (telnet == true)
        max_buf_size = TX_BUF_SIZE / 2;
      else
        max_buf_size = TX_BUF_SIZE;

      // Read from serial, the amount available up to
      // maximum size of the buffer
      size_t len = std::min(SIO_UART.available(), max_buf_size);
      SIO_UART.readBytes(&txBuf[0], len);

      // Disconnect if going to AT mode with "+++" sequence
      for (int i = 0; i < (int)len; i++)
      {
        if (txBuf[i] == '+') plusCount++; else plusCount = 0;
        if (plusCount >= 3)
        {
          plusTime = millis();
        }
        if (txBuf[i] != '+')
        {
          plusCount = 0;
        }
      }

      // Double (escape) every 0xff for telnet, shifting the following bytes
      // towards the end of the buffer from that point
      if (telnet == true)
      {
        for (int i = len - 1; i >= 0; i--)
        {
          if (txBuf[i] == 0xff)
          {
            for (int j = TX_BUF_SIZE - 1; j > i; j--)
            {
              txBuf[j] = txBuf[j - 1];
            }
            len++;
          }
        }
      }

      // Write the buffer to TCP finally
      tcpClient.write(&txBuf[0], len);
      yield();
    }

    // Transmit from TCP to terminal
    if (tcpClient.available())
    {
      led_on();
      char buf[128];
      int avail = tcpClient.available();
      int i;

      if (avail > 128)
      {
        tcpClient.readBytes(buf, 128);
        for (i = 0; i < 128; i++)
          SIO_UART.write(buf[i]);
      }
      else
      {
        tcpClient.readBytes(buf, avail);
        for (i = 0; i < avail; i++)
          SIO_UART.write(buf[i]);
      }
    }
  }

  // If we have received "+++" as last bytes from serial port and there
  // has been over a second without any more bytes, disconnect
  if (plusCount >= 3)
  {
    if (millis() - plusTime > 1000)
    {
      tcpClient.stop();
      plusCount = 0;
    }
  }

  // Go to command mode if TCP disconnected and not in command mode
  if ((!tcpClient.connected()) && (cmdMode == false))
  {
    cmdMode = true;
    SIO_UART.println("NO CARRIER");
    if (LISTEN_PORT > 0) tcpServer.begin();
  }

  // Turn off tx/rx led if it has been lit long enough to be visible
#ifdef ESP32
  if (millis() - ledTime > LED_TIME) digitalWrite(PIN_LED2, HIGH);
#endif
}

/**
   Perform a command given in command mode
*/
void command()
{
  cmd.trim();
  if (cmd == "") return;
  SIO_UART.println();
  String upCmd = cmd;
  upCmd.toUpperCase();

  long newBps = 0;

  /**** Just AT ****/
  if (upCmd == "AT") SIO_UART.println("OK");

  /**** Dial to host ****/
  else if ((upCmd.indexOf("ATDT") == 0) || (upCmd.indexOf("ATDP") == 0) || (upCmd.indexOf("ATDI") == 0))
  {
    int portIndex = cmd.indexOf(":");
    String host, port;
    if (portIndex != -1)
    {
      host = cmd.substring(4, portIndex);
      port = cmd.substring(portIndex + 1, cmd.length());
    }
    else
    {
      host = cmd.substring(4, cmd.length());
      port = "23"; // Telnet default
    }
    SIO_UART.print("Connecting to ");
    SIO_UART.print(host);
    SIO_UART.print(":");
    SIO_UART.println(port);
    char *hostChr = new char[host.length() + 1];
    host.toCharArray(hostChr, host.length() + 1);
    int portInt = port.toInt();
    tcpClient.setNoDelay(true); // Try to disable naggle
    if (tcpClient.connect(hostChr, portInt))
    {
      tcpClient.setNoDelay(true); // Try to disable naggle
      SIO_UART.print("CONNECT ");
      SIO_UART.println(myBps);
      cmdMode = false;
      SIO_UART.flush();
      if (LISTEN_PORT > 0) tcpServer.stop();
    }
    else
    {
      SIO_UART.println("NO CARRIER");
    }
    delete hostChr;
  }

  /**** Connect to WIFI ****/
  else if (upCmd.indexOf("ATWIFI") == 0)
  {
    int keyIndex = cmd.indexOf(",");
    String ssid, key;
    if (keyIndex != -1)
    {
      ssid = cmd.substring(6, keyIndex);
      key = cmd.substring(keyIndex + 1, cmd.length());
    }
    else
    {
      ssid = cmd.substring(6, cmd.length());
      key = "";
    }
    char *ssidChr = new char[ssid.length() + 1];
    ssid.toCharArray(ssidChr, ssid.length() + 1);
    char *keyChr = new char[key.length() + 1];
    key.toCharArray(keyChr, key.length() + 1);
    SIO_UART.print("Connecting to ");
    SIO_UART.print(ssid);
    SIO_UART.print("/");
    SIO_UART.println(key);
    WiFi.begin(ssidChr, keyChr);
    for (int i = 0; i < 100; i++)
    {
      delay(100);
      if (WiFi.status() == WL_CONNECTED)
      {
        SIO_UART.println("OK");
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED)
    {
      SIO_UART.println("ERROR");
    }
    delete ssidChr;
    delete keyChr;
  }

  /**** Change baud rate from default ****/
  else if (upCmd == "AT300") newBps = 300;
  else if (upCmd == "AT1200") newBps = 1200;
  else if (upCmd == "AT2400") newBps = 2400;
  else if (upCmd == "AT4800") newBps = 4800;
  else if (upCmd == "AT9600") newBps = 9600;
  else if (upCmd == "AT19200") newBps = 19200;
  else if (upCmd == "AT38400") newBps = 38400;
  else if (upCmd == "AT57600") newBps = 57600;
  else if (upCmd == "AT115200") newBps = 115200;

  /**** Change telnet mode ****/
  else if (upCmd == "ATNET0")
  {
    telnet = false;
    SIO_UART.println("OK");
  }
  else if (upCmd == "ATNET1")
  {
    telnet = true;
    SIO_UART.println("OK");
  }

  /**** Answer to incoming connection ****/
  else if ((upCmd == "ATA") && tcpServer.hasClient())
  {
    tcpClient = tcpServer.available();
    tcpClient.setNoDelay(true); // try to disable naggle
    tcpServer.stop();
    SIO_UART.print("CONNECT ");
    SIO_UART.println(myBps);
    cmdMode = false;
    SIO_UART.flush();
  }

  /**** See my IP address ****/
  else if (upCmd == "ATIP")
  {
    SIO_UART.println(WiFi.localIP());
    SIO_UART.println("OK");
  }

  /**** Print Help ****/
  else if (upCmd == "AT?")
  {
    SIO_UART.println("FujiNet Virtual Modem");
    SIO_UART.println("=====================");
    SIO_UART.println();
    SIO_UART.println("Connect to WIFI: ATWIFI<ssid>,<key>");
    SIO_UART.println("Change Baud Rate: AT<baud>");
    SIO_UART.println("Connect by TCP: ATDT<host>:<port>");
    SIO_UART.println("See my IP address: ATIP");
    SIO_UART.println("Disable telnet command handling: ATNET0");
    SIO_UART.println("HTTP GET: ATGET<URL>");
    SIO_UART.println();
    if (LISTEN_PORT > 0)
    {
      SIO_UART.print("Listening to connections at port ");
      SIO_UART.print(LISTEN_PORT);
      SIO_UART.println(", which result in RING and you can answer with ATA.");
      tcpServer.begin();
    }
    else
    {
      SIO_UART.println("Incoming connections are disabled.");
    }
    SIO_UART.println("");
    SIO_UART.println("OK");
  }

  /**** HTTP GET request ****/
  else if (upCmd.indexOf("ATGET") == 0)
  {
    // From the URL, aquire required variables
    // (12 = "ATGEThttp://")
    int portIndex = cmd.indexOf(":", 12); // Index where port number might begin
    int pathIndex = cmd.indexOf("/", 12); // Index first host name and possible port ends and path begins
    int port;
    String path, host;
    if (pathIndex < 0)
    {
      pathIndex = cmd.length();
    }
    if (portIndex < 0)
    {
      port = 80;
      portIndex = pathIndex;
    }
    else
    {
      port = cmd.substring(portIndex + 1, pathIndex).toInt();
    }
    host = cmd.substring(12, portIndex);
    path = cmd.substring(pathIndex, cmd.length());
    if (path == "") path = "/";
    char *hostChr = new char[host.length() + 1];
    host.toCharArray(hostChr, host.length() + 1);

    // Debug
    SIO_UART.print("Getting path ");
    SIO_UART.print(path);
    SIO_UART.print(" from port ");
    SIO_UART.print(port);
    SIO_UART.print(" of host ");
    SIO_UART.print(host);
    SIO_UART.println("...");

    // Establish connection
    if (!tcpClient.connect(hostChr, port))
    {
      SIO_UART.println("NO CARRIER");
    }
    else
    {
      SIO_UART.print("CONNECT ");
      SIO_UART.println(myBps);
      cmdMode = false;

      // Send a HTTP request before continuing the connection as usual
      String request = "GET ";
      request += path;
      request += " HTTP/1.1\r\nHost: ";
      request += host;
      request += "\r\nConnection: close\r\n\r\n";
      tcpClient.print(request);
    }
    delete hostChr;
  }

  /**** Unknown command ****/
  else SIO_UART.println("ERROR");

  /**** Tasks to do after command has been parsed ****/
  if (newBps)
  {
    SIO_UART.println("OK");
    delay(150); // Sleep enough for 4 bytes at any previous baud rate to finish ("\nOK\n")
    SIO_UART.begin(newBps);
#ifdef ESP8266
    SIO_UART.swap();
#endif
    myBps = newBps;
  }

  cmd = "";
}

