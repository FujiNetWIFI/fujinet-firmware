/**
   Test #25 - Midimaze
*/

#define TEST_NAME "#FujiNet MidiMaze"

#define MIDIMAZE_PORT 9876
#define BUFFER_SIZE 8192
#define PACKET_TIMEOUT 5
#define UART_BAUD 31250

#ifdef ESP8266
#include <ESP8266WiFi.h>
#endif
#ifdef ESP32
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include "analogWrite.h"
#endif

#include <FS.h>

#include <WiFiUdp.h>

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT, MIDIMAZE} cmdState;

// Uncomment for Debug on 2nd UART (GPIO 2)
//#define DEBUG_S

// Uncomment for Debug on TCP/6502 to DEBUG_HOST
// Run:  `nc -vk -l 6502` on DEBUG_HOST
// #define DEBUG_N
// #define DEBUG_HOST "192.168.1.7"

#ifdef ESP8266
#define SIO_UART Serial
#define BUG_UART Serial1
#define PIN_LED         2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12
#define PIN_CKO         2
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
#endif

#define DELAY_T5          1500
#define READ_CMD_TIMEOUT  12
#define CMD_TIMEOUT       50

#define STATUS_SKIP       8

WiFiUDP UDP;
File atr;
File midimaze;

uint8_t buf1[BUFFER_SIZE];
uint8_t i1=0;

uint8_t buf2[BUFFER_SIZE];
uint8_t i2=0;

unsigned long cmdTimer = 0;

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

/**
   A single SSID entry
*/
union
{
  struct
  {
    char ssid[32];
    char rssi;
  };
  unsigned char rawData[33];
} ssidInfo;

/**
   Network Configuration
*/
union
{
  struct
  {
    char ssid[32];
    char password[64];
  };
  unsigned char rawData[96];
} netConfig;

byte sector[128];
bool load_config = true;
char udpHost[64];
IPAddress remoteIp;

#ifdef DEBUG_N
WiFiClient wificlient;
#endif

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
  if (digitalRead(PIN_CMD) == LOW)
  {
    if (cmdState == MIDIMAZE)
    {
      analogWriteFrequency(0); // turn off clock
      analogWrite(PIN_CKO, 0); // turn off clock
      SIO_UART.begin(19200); // reset the baud rate if needed.
    }

    cmdState = ID;
    cmdTimer = millis();
#ifdef ESP32
    digitalWrite(PIN_LED2, LOW); // on
#endif
  }
}

/**
   Return true if valid device ID
*/
bool sio_valid_device_id()
{
  if (cmdFrame.devic == 0x31)
    return true;
  else if (cmdFrame.devic == 0x70)
    return true;
  else
    return false;
}

/**
   Get ID
*/
void sio_get_id()
{
  cmdFrame.devic = SIO_UART.read();
  if (sio_valid_device_id())
    cmdState = COMMAND;
  else
  {
    cmdState = WAIT;
    cmdTimer = 0;
  }

#ifdef DEBUG
  Debug_print("CMD DEVC: ");
  Debug_println(cmdFrame.devic, HEX);
#endif
}


void sio_get_command()
{
  cmdFrame.comnd = SIO_UART.read();
  cmdState = AUX1;

#ifdef DEBUG
  Debug_print("CMD CMND: ");
  Debug_println(cmdFrame.comnd, HEX);
#endif
}

/**
   Get aux1
*/
void sio_get_aux1()
{
  cmdFrame.aux1 = SIO_UART.read();
  cmdState = AUX2;

#ifdef DEBUG
  Debug_print("CMD AUX1: ");
  Debug_println(cmdFrame.aux1, HEX);
#endif
}

/**
   Get aux2
*/
void sio_get_aux2()
{
  cmdFrame.aux2 = SIO_UART.read();
  cmdState = CHECKSUM;

#ifdef DEBUG
  Debug_print("CMD AUX2: ");
  Debug_println(cmdFrame.aux2, HEX);
#endif
}

/**
   Get Checksum, and compare
*/
void sio_get_checksum()
{
  byte ck;
  cmdFrame.cksum = SIO_UART.read();
  ck = sio_checksum((byte *)&cmdFrame.cmdFrameData, 4);

#ifdef DEBUG
  Debug_print("CMD CKSM: ");
  Debug_print(cmdFrame.cksum, HEX);
#endif

  if (ck == cmdFrame.cksum)
  {
#ifdef DEBUG
    Debug_println(", ACK");
#endif
    sio_ack();
  }
  else
  {
#ifdef DEBUG
    Debug_println(", NAK");
#endif
    sio_nak();
  }
}

/**
   Send an acknowledgement
*/
void sio_ack()
{
  delayMicroseconds(500);
  SIO_UART.write('A');
  SIO_UART.flush();
  //cmdState = PROCESS;
  sio_process();
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
  cmdTimer = 0;
}

/**
   Basically we're here while in MidiMaze mode.
   The COMMAND line pulling LOW will interrupt this state.
*/
void sio_midimaze()
{
  // if there’s data available, read a packet
  int packetSize = UDP.parsePacket();
  if (packetSize > 0)
  {
    remoteIp = UDP.remoteIP(); // store the ip of the remote device
    UDP.read(buf1, BUFFER_SIZE);
    // now send to UART:
    SIO_UART.write(buf1, packetSize);
#ifdef DEBUG
    Debug_print("MIDI-IN: ");
    Debug_println((char*)buf1);
#endif
    SIO_UART.flush();
  }

  if (SIO_UART.available()) {
    // read the data until pause:
    if (digitalRead(PIN_MTR) == LOW || digitalRead(PIN_CMD) == LOW)
    {
      SIO_UART.read(); // Toss the data if motor or command is asserted
    }
    else
    {
      while (1)
      {
        if (SIO_UART.available())
        {
          buf2[i2] = (char)SIO_UART.read(); // read char from UART
#ifdef DEBUG
          Debug_print("MIDI-OUT: ");
          Debug_println(buf2[i2]);
#endif
          if (i2 < BUFFER_SIZE - 1) i2++;
        }
        else
        {
          delay(PACKET_TIMEOUT);
          if (!SIO_UART.available())
            break;
        }
      }

      // now send to WiFi:
      UDP.beginPacket(udpHost, MIDIMAZE_PORT); // remote IP and port
      UDP.write(buf2, i2);
      UDP.endPacket();
      i2 = 0;
    }
  }
}

/**
   The SIO State machine
*/
void sio_incoming() {
  switch (cmdState)
  {
    case ID:
      sio_get_id();
      break;
    case COMMAND:
      sio_get_command();
      break;
    case AUX1:
      sio_get_aux1();
      break;
    case AUX2:
      sio_get_aux2();
      break;
    case CHECKSUM:
      sio_get_checksum();
      break;
    case ACK:
      sio_ack();
      break;
    case NAK:
      sio_nak();
      break;
    case PROCESS:
      sio_process();
      break;
    case WAIT:
      SIO_UART.read(); // Toss it for now
      cmdTimer = 0;
      break;
    case MIDIMAZE:
      sio_midimaze();
      break;
  }
}

/**
   Specify destination host
*/
void sio_udp_connect()
{
  byte ck;
  int packetSize = 64;
  byte tmp[64];

  SIO_UART.readBytes(tmp, packetSize);
  while (SIO_UART.available() == 0) {
    delayMicroseconds(200);
  }
  ck = SIO_UART.read(); // Read checksum

  if (ck == sio_checksum(tmp, packetSize))
  {
    SIO_UART.write('A');
    delayMicroseconds(DELAY_T5);
    memcpy(udpHost, tmp, 64);
    SIO_UART.write('C');
    yield();
  }
  else
  {
    SIO_UART.write('N');
    yield();
    return;
  }
#ifdef DEBUG
  Debug_printf("Receiving %d bytes from computer\n", packetSize);
  Debug_printf("UDP host set to: %s", udpHost);
#endif
}

/**
   Start Midimaze mode (SIO command 0xF0)
*/
void sio_start_midimaze()
{
  SIO_UART.write('C'); // We are now done with SIO...

  // Reset UART for MIDIMATE mode
  SIO_UART.begin(31250);
  analogWriteFrequency(33125); // Set PWM Clock for MIDI, closer to actual frequency
  analogWrite(PIN_CKO, 511); // Turn on PWM @ 50% duty cycle
  UDP.begin(MIDIMAZE_PORT);
  cmdState = MIDIMAZE; // We are now in MIDIMAZE mode.
}

/**
   scan for networks
*/
void sio_scan_networks()
{
  byte ck;
  char totalSSIDs;
  char ret[4] = {0, 0, 0, 0};

  WiFi.mode(WIFI_STA);
  totalSSIDs = WiFi.scanNetworks();
  ret[0] = totalSSIDs;

#ifdef DEBUG
  Debug_printf("Scan networks returned: %d\n\n", totalSSIDs);
#endif
  ck = sio_checksum((byte *)&ret, 4);

  SIO_UART.write('C');     // Completed command
  SIO_UART.flush();

  delayMicroseconds(1500); // t5 delay

  // Write data frame
  SIO_UART.write((byte *)&ret, 4);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();

#ifdef DEBUG
  Debug_printf("Wrote data packet/Checksum: $%02x $%02x $%02x $%02x/$02x\n\n", ret[0], ret[1], ret[2], ret[3], ck);
#endif
  delayMicroseconds(200);
}

/**
   Return scanned network entry
*/
void sio_scan_result()
{
  byte ck;

  strcpy(ssidInfo.ssid, WiFi.SSID(cmdFrame.aux1).c_str());
  ssidInfo.rssi = (char)WiFi.RSSI(cmdFrame.aux1);

  ck = sio_checksum((byte *)&ssidInfo.rawData, 33);

  delayMicroseconds(1500); // t5 delay
  SIO_UART.write('C');     // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(ssidInfo.rawData, 33);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
   SIO set SSID/Password
*/
void sio_set_ssid()
{
  byte ck;

  SIO_UART.readBytes(netConfig.rawData, 96);
  while (SIO_UART.available() == 0) {
    delayMicroseconds(200);
  }
  ck = SIO_UART.read(); // Read checksum
  SIO_UART.write('A'); // Write ACK

  if (ck == sio_checksum(netConfig.rawData, 96))
  {
    SIO_UART.write('C');
    WiFi.begin(netConfig.ssid, netConfig.password);
    load_config=false;
#ifdef DEBUG
    Debug_printf("connecting to %s with %s.\n", netConfig.ssid, netConfig.password);
#endif
    yield();
  }
  else
  {
    SIO_UART.write('E');
    yield();
  }
}

/**
   SIO get WiFi Status
*/
void sio_get_wifi_status()
{
  byte ck;
  char wifiStatus;

  wifiStatus = WiFi.status();

  if (wifiStatus == WL_CONNECTED)
  {
#ifdef ESP8266
    digitalWrite(PIN_LED, LOW); // turn on LED
  }
  else
  {
    digitalWrite(PIN_LED, HIGH); // turn off LED
  }
#elif defined(ESP32)
    digitalWrite(PIN_LED1, LOW); // turn on LED
  }
  else
  {
    digitalWrite(PIN_LED1, HIGH); // turn off LED
  }
#endif

  ck = sio_checksum((byte *)&wifiStatus, 1);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C');     // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(wifiStatus);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
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

#ifdef DEBUG_S
  Serial1.printf("receiving 128b data frame from computer.\n");
#endif

  SIO_UART.readBytes(sector, 128);
  while (SIO_UART.available() == 0) {
    delayMicroseconds(200);
  }
  ck = SIO_UART.read(); // Read checksum
  //delayMicroseconds(350);
  SIO_UART.write('A'); // Write ACK

  if (ck == sio_checksum(sector, 128))
  {
    if (load_config == true)
    {
      atr.seek(offset, SeekSet);
      atr.write(sector, 128);
      atr.flush();
    }
    SIO_UART.write('C');
    yield();
  }
  else
  {
    SIO_UART.write('E');
    yield();
  }
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
#ifdef DEBUG_S
  Serial1.printf("We faked a format.\n");
#endif
}

/**
   Read
*/
void sio_read()
{
  byte ck;
  unsigned char deviceSlot = cmdFrame.devic - 0x31;
  int sectorNum = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  int cacheOffset = 0;
  int offset;

  if (load_config == true) // no TNFS ATR mounted.
  {
    offset = sectorNum;
    offset *= 128;
    offset -= 128;
    offset += 16;
    atr.seek(offset, SeekSet);
    atr.read(sector, 128);
  }
  else
  {
    offset = sectorNum;
    offset *= 128;
    offset -= 128;
    offset += 16;
    midimaze.seek(offset, SeekSet);
    midimaze.read(sector, 128);
  }

  ck = sio_checksum((byte *)&sector, 128);

  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write(sector, 128);
  SIO_UART.flush();

  // Write data frame checksum
  delayMicroseconds(200);
  SIO_UART.write(ck);
  SIO_UART.flush();
#ifdef DEBUG
  Debug_print("SIO READ OFFSET: ");
  Debug_print(offset);
  Debug_print(" - ");
  Debug_println((offset + 128));
#endif

  if (sectorNum==0x384) // Sector 900 was just read...
  {
#ifdef DEBUG
    Debug_printf("Last sector of MIDIMAZE read, switching to MidiMaze mode!");
#endif
    sio_start_midimaze();  
  }

}

/**
   Status
*/
void sio_status()
{
  byte status[4] = {0x00, 0xFF, 0xFE, 0x00};
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
   Process command
*/

void sio_process()
{
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
    case 0xFD:
      sio_scan_networks();
      break;
    case 0xFC:
      sio_scan_result();
      break;
    case 0xFB:
      sio_set_ssid();
      break;
    case 0xFA:
      sio_get_wifi_status();
      break;
    case 0xF0:
      sio_start_midimaze();
      break;
    case 0xD4:
      sio_udp_connect();
      break;
  }

  cmdState = WAIT;
  cmdTimer = 0;
}

void setup() {
#ifdef DEBUG_S
  BUG_UART.begin(115200);
  Debug_println();
  Debug_println(TEST_NAME);
#endif
  SPIFFS.begin();
  atr = SPIFFS.open("/autorun.atr", "r+");
  midimaze = SPIFFS.open("/midimaze.atr", "r+");

  // Set up pins
  pinMode(PIN_INT, OUTPUT); // thanks AtariGeezer
  digitalWrite(PIN_INT, HIGH);
  pinMode(PIN_PROC, OUTPUT); // thanks AtariGeezer
  digitalWrite(PIN_PROC, HIGH);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);
  pinMode(PIN_CKO, OUTPUT);
#ifdef ESP8266
  pinMode(PIN_LED, INPUT);
  digitalWrite(PIN_LED, HIGH); // off
#elif defined(ESP32)
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  digitalWrite(PIN_LED1, HIGH); // off
  digitalWrite(PIN_LED2, HIGH); // off
#endif

#ifdef DEBUG_N
  /* Get WiFi started, but don't wait for it otherwise SIO
     powered FujiNet fails to boot
  */
  WiFi.begin(DEBUG_SSID, DEBUG_PASSWORD);
#endif

  // Set up serial
  SIO_UART.begin(19200);
#ifdef ESP8266
  SIO_UART.swap();
#endif

  // Attach COMMAND interrupt.
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_isr_cmd, FALLING);
  cmdState = WAIT; // Start in wait state
}



void loop() {
#ifdef DEBUG_N
  /* Connect to debug server if we aren't and WiFi is connected */
  if ( !wificlient.connected() && WiFi.status() == WL_CONNECTED )
  {
    wificlient.connect(DEBUG_HOST, 6502);
    wificlient.println(TEST_NAME);
  }
#endif

  if (SIO_UART.available() > 0)
  {
    sio_incoming();
  }

  if (millis() - cmdTimer > CMD_TIMEOUT && cmdState != WAIT)
  {
#ifdef DEBUG
    Debug_print("SIO CMD TIMEOUT: ");
    Debug_println(cmdState);
#endif
    cmdState = WAIT;
    cmdTimer = 0;
  }

#ifdef ESP32
  if (cmdState == WAIT && digitalRead(PIN_LED2) == LOW)
  {
    digitalWrite(PIN_LED2, HIGH); // Turn off SIO LED
  }
#endif
}
