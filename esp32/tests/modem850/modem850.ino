/*
   ESP8266 based virtual modem
   Copyright (C) 2016 Jussi Salin <salinjus@gmail.com>

   Modified for FujiNet / Atari SIO
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

#define TEST_NAME "#FujiNet WiFi Modem 850"

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <FS.h>
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
bool commanderMark = false;

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
  commanderMark = true; // Enter the secret city
  modemActive = false;
}

/**
   Return true if valid device ID
*/
bool sio_valid_device_id(byte device = NULL);

bool sio_valid_device_id(byte device)
{
  if ( device == NULL )
  {
    device = cmdFrame.devic;
  }

  if (device == 0x31)
    return true;
  else if (device == 0x50)
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

  while ( i < 5 ) // until we've read 5 bytes from serial
  {
    switch (i)
    {
      case 0: // DEVICE ID
        while (!SIO_UART.available()) { delayMicroseconds(1); }
        cmdFrame.devic = SIO_UART.read();
        break;
      case 1: // COMMAND
        while (!SIO_UART.available()) { delayMicroseconds(1); }
        cmdFrame.comnd = SIO_UART.read();
        break;
      case 2: // AUX1
        while (!SIO_UART.available()) { delayMicroseconds(1); }
        cmdFrame.aux1 = SIO_UART.read();
        break;
      case 3: // AUX2
        while (!SIO_UART.available()) { delayMicroseconds(1); }
        cmdFrame.aux2 = SIO_UART.read();
        break;
      case 4: // CHECKSUM
        while (!SIO_UART.available()) { delayMicroseconds(1); }
        cmdFrame.cksum = SIO_UART.read();
        madeit = true;
        break;
    }
    i++;
    yield();
  }

#ifdef DEBUG
  Debug_print("CMD Frame: ");
  Debug_print(cmdFrame.devic, HEX);
  Debug_print(", ");
  Debug_print(cmdFrame.comnd, HEX);
  Debug_print(", ");
  Debug_print(cmdFrame.aux1, HEX);
  Debug_print(", ");
  Debug_print(cmdFrame.aux2, HEX);
  Debug_print(", ");
  Debug_println(cmdFrame.cksum, HEX);
#endif
  if (!madeit) // we didn't get all the bits, junk this command frame
    commanderMark = false;
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
   850 Status 0x53
*/
void sio_R_status()
{
  byte status[2] = {0x00, 0x00};
  byte ck;

  ck = sio_checksum((byte *)&status, 2);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();
  delayMicroseconds(200);

  // Write data frame
  for (int i = 0; i < 2; i++)
    SIO_UART.write(status[i]);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
#ifdef DEBUG
  Debug_println("R:Status: Sent 0,0,C");
#endif
}

/**
   850 Write
*/
void sio_R_write()
{
  // for now, just agree/complete
  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();
#ifdef DEBUG
  Debug_println("R:Write: Sent Complete");
#endif
}

/**
 ** 850 Control 0x41

   DTR/RTS/XMT
  D7 Enable DTR (Data Terminal Ready) change
  D5 Enable RTS (Request To Send) change
  D1 Enable XMT (Transmit) change
      0 No change
      1 Change state
  D6 New DTR state (if D7 set)
  D4 New RTS state (if D5 set)
  D0 New XMT state (if D1 set)
      0 Negate / space
*/
void sio_R_control()
{
  // for now, just complete
  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();
#ifdef DEBUG
  Debug_println("R:Control: Sent Complete");
#endif
}

/**
   850 Configure (0x42)
*/
void sio_R_config()
{
  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();

  byte newBaud = 0x0F & cmdFrame.aux1; // Get baud rate
  byte wordSize = 0x30 & cmdFrame.aux1; // Get word size
  byte stopBit = (1 << 7) & cmdFrame.aux1; // Get stop bits, 0x80 = 2, 0 = 1

  switch (newBaud)
  {
    case 0x08:
      myBps = 300;
      break;
    case 0x09:
      myBps = 600;
      break;
    case 0xA:
      myBps = 1200;
      break;
    case 0x0B:
      myBps = 1800;
      break;
    case 0x0C:
      myBps = 2400;
      break;
    case 0x0D:
      myBps = 4800;
      break;
    case 0x0E:
      myBps = 9600;
      break;
    case 0x0F:
      myBps = 19200;
      break;
  }

#ifdef DEBUG
  Debug_printf("R:Config: %i, %X, ", wordSize, stopBit);
  Debug_println(myBps);
#endif

}

/**
   850 Concurrent mode
*/
void sio_R_concurrent()
{
  //byte ck;
  //char response[9]; // 9 bytes to setup POKEY

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();

  switch (myBps)
  {
    case 300:
      { char response[] = {0xA0, 0xA0, 0x0B, 0xA0, 0xA0, 0xA0, 0x0B, 0xA0, 0x78};
        byte ck = sio_checksum((byte *)response, 9);
        // Write data frame
        SIO_UART.write((byte *)response, 9);
        SIO_UART.write(ck); // Write data frame checksum
        break;
      }
    case 1200:
      { char response[] = {0xE3, 0xA0, 0x02, 0xA0, 0xE3, 0xA0, 0x02, 0xA0, 0x78};
        byte ck = sio_checksum((byte *)response, 9);
        // Write data frame
        SIO_UART.write((byte *)response, 9);
        SIO_UART.write(ck); // Write data frame checksum
        break;
      }
    case 2400:
      { char response[] = {0x6E, 0xA0, 0x01, 0xA0, 0x6E, 0xA0, 0x01, 0xA0, 0x78};
        byte ck = sio_checksum((byte *)response, 9);
        // Write data frame
        SIO_UART.write((byte *)response, 9);
        SIO_UART.write(ck); // Write data frame checksum
        break;
      }
    case 4800:
      { char response[] = {0xB3, 0xA0, 0x00, 0xA0, 0xB3, 0xA0, 0x00, 0xA0, 0x78};
        byte ck = sio_checksum((byte *)response, 9);
        // Write data frame
        SIO_UART.write((byte *)response, 9);
        SIO_UART.write(ck); // Write data frame checksum
        break;
      }
    case 9600:
      { char response[] = {0x56, 0xA0, 0x00, 0xA0, 0x56, 0xA0, 0x00, 0xA0, 0x78};
        byte ck = sio_checksum((byte *)response, 9);
        // Write data frame
        SIO_UART.write((byte *)response, 9);
        SIO_UART.write(ck); // Write data frame checksum
        break;
      }
  }

  SIO_UART.flush();
#ifdef DEBUG
  Debug_println("R:Stream: Start");
#endif

  modemActive = true;
  sioBaud = false;
  SIO_UART.updateBaudRate(myBps);
#ifdef DEBUG
  Debug_printf("BAUD: %i\n", myBps);
  Debug_println("MODEM ACTIVE");
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
#ifdef DEBUG
  Debug_println("ACK!");
#endif
}

/**
   Send a non-acknowledgement
*/
void sio_nak()
{
  delayMicroseconds(500);
  SIO_UART.write('N');
  SIO_UART.flush();
  commanderMark = false;
#ifdef DEBUG
  Debug_println("NAK!");
#endif
}

void sio_process()
{
  bool process = false;
  bool shifted = false;
  
  // Try to process CMD frame as is, if it fails, try to shift the frame bytes
  if (sio_valid_device_id() && (sio_checksum((byte *)&cmdFrame.cmdFrameData, 4) == cmdFrame.cksum))
  {
    process = true;
  }
  else if (sio_valid_device_id(cmdFrame.comnd))
  {
    shifted = true;
    //byte tempByte = cmdFrame.devic;
    cmdFrame.devic = cmdFrame.comnd;
    cmdFrame.comnd = cmdFrame.aux1;
    cmdFrame.aux1 = cmdFrame.aux2;
    cmdFrame.aux2 = cmdFrame.cksum;
    while (!SIO_UART.available()){ delayMicroseconds(1); }
    cmdFrame.cksum = SIO_UART.read();
    if (sio_checksum((byte *)&cmdFrame.cmdFrameData, 4) == cmdFrame.cksum)
      process = true;
  }
  else if (sio_valid_device_id(cmdFrame.aux1))
  {
    shifted = true;
    //byte tempByte1 = cmdFrame.devic;
    //byte tempByte2 = cmdFrame.comnd;
    cmdFrame.devic = cmdFrame.aux1;
    cmdFrame.comnd = cmdFrame.aux2;
    cmdFrame.aux1 = cmdFrame.cksum;
    while (SIO_UART.available()<2){ delayMicroseconds(1); }
    cmdFrame.aux1 = SIO_UART.read();
    cmdFrame.cksum = SIO_UART.read();
    if (sio_checksum((byte *)&cmdFrame.cmdFrameData, 4) == cmdFrame.cksum)
      process = true;
   }

  if (shifted)
  {
#ifdef DEBUG
    Debug_print("SHIFTED CMD Frame: ");
    Debug_print(cmdFrame.devic, HEX);
    Debug_print(", ");
    Debug_print(cmdFrame.comnd, HEX);
    Debug_print(", ");
    Debug_print(cmdFrame.aux1, HEX);
    Debug_print(", ");
    Debug_print(cmdFrame.aux2, HEX);
    Debug_print(", ");
    Debug_println(cmdFrame.cksum, HEX);
#endif
  }

  if(process)
  {
    while (!digitalRead(PIN_CMD)) { delayMicroseconds(1); }
    sio_ack(); // Valid Device and Good Checksum
    switch (cmdFrame.comnd)
    {
      case 'P': // 0x50
      case 'W': // 0x57
        if (cmdFrame.devic == 0x50) // R: Device
          sio_R_write();
        else
          sio_write();
        break;
      case 'R': // 0x52
        sio_read();
        break;
      case 'S': // 0x53
        if (cmdFrame.devic == 0x50) // R: Device
          sio_R_status();
        else
          sio_status();
        break;
      case '!': // 0x21
        sio_format();
        break;
      // Atari 850 Commands
      case 'B': // 0x42
        sio_R_config();
        break;
      case 'A': // 0x41
        sio_R_control();
        break;
      case 'X': // 0x58
        sio_R_concurrent();
        break;
    }
  }
  else
  {
/*    
    if (SIO_UART.available() > 0 && digitalRead(PIN_CMD))
    {
      int avail = SIO_UART.available();
      int i;

#ifdef DEBUG
      Debug_print("TOSS: ");
      Debug_println(avail);
#endif

      for (i = 0; i < avail; i++)
        SIO_UART.read(); // Toss it, toss it aLL!!!
    }
*/
    sio_nak(); // Invalid Device or Bad Checksum
  }
  SIO_UART.flush();
  commanderMark = false;
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
  tcpServer.begin();
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
  if (commanderMark) // Entering the secret city
  { // CMD Line Interrupt, get the whole command frame...
    sio_get_cmd_frame();
  }
  else if(!modemActive)
    SIO_UART.read(); // Throw out the trash

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

