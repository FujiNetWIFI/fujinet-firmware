/**
   Test #21 - TIC-TAC-TOE
*/

#include <ESP8266WiFi.h>
#include <FS.h>
#include <WiFiUdp.h>

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT} cmdState;

// Uncomment for Debug on 2nd UART (GPIO 2)
// #define DEBUG_S

// Uncomment for Debug on TCP/6502 to DEBUG_HOST
// Run:  `nc -vk -l 6502` on DEBUG_HOST
#define DEBUG_N
#define DEBUG_HOST "192.168.1.7"


#define PIN_LED         2
#define PIN_INT         5
#define PIN_PROC        4
#define PIN_MTR        16
#define PIN_CMD        12

#define DELAY_T5          1500
#define READ_CMD_TIMEOUT  12
#define CMD_TIMEOUT       50

#define STATUS_SKIP       8

WiFiUDP UDP;
File atr;
File tictactoe;

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

/**
   A UDP packet
*/
byte udpPacket[512];
char udpHost[64];
int udpPort;

byte sectorCache[2560];
byte sector[128];

char atr_fd = 0xFF; // boot internal by default.
char tnfs_dir_fd;
int firstCachedSector = 1024;

#ifdef DEBUG_N
WiFiClient wificlient;
#endif

#ifdef DEBUG
#define Debug_print(...) Debug_print( __VA_ARGS__ )
#define Debug_printf(...) Debug_printf( __VA_ARGS__ )
#define Debug_println(...) Debug_println( __VA_ARGS__ )
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
    cmdState = ID;
    cmdTimer = millis();
  }
}

/**
   Get ID
*/
void sio_get_id()
{
  cmdFrame.devic = Serial.read();
  if (cmdFrame.devic == 0x31 || cmdFrame.devic == 0x70)
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
  cmdFrame.comnd = Serial.read();
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
  cmdFrame.aux1 = Serial.read();
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
  cmdFrame.aux2 = Serial.read();
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
  cmdFrame.cksum = Serial.read();
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

  Serial.write('C');     // Completed command
  Serial.flush();

  delayMicroseconds(1500); // t5 delay

  // Write data frame
  Serial.write(ret, 4);

  // Write data frame checksum
  Serial.write(ck);
  Serial.flush();

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
  Serial.write('C');     // Completed command
  Serial.flush();

  // Write data frame
  Serial.write(ssidInfo.rawData, 33);

  // Write data frame checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);
}

/**
   SIO set SSID/Password
*/
void sio_set_ssid()
{
  byte ck;

  Serial.readBytes(netConfig.rawData, 96);
  ck = Serial.read(); // Read checksum
  Serial.write('A'); // Write ACK

  if (ck == sio_checksum(netConfig.rawData, 96))
  {
    delayMicroseconds(DELAY_T5);
    Serial.write('C');
    WiFi.begin(netConfig.ssid, netConfig.password);
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
    atr_fd = 0x00; // tic tac toe;
  }

  ck = sio_checksum((byte *)&wifiStatus, 1);

  delayMicroseconds(1500); // t5 delay
  Serial.write('C');     // Completed command
  Serial.flush();

  // Write data frame
  Serial.write(wifiStatus);

  // Write data frame checksum
  Serial.write(ck);
  Serial.flush();
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

  Serial.readBytes(sector, 128);
  ck = Serial.read(); // Read checksum
  //delayMicroseconds(350);
  Serial.write('A'); // Write ACK

  if (ck == sio_checksum(sector, 128))
  {
    delayMicroseconds(DELAY_T5);

    if (atr_fd == 0xFF)
    {
      atr.seek(offset, SeekSet);
      atr.write(sector, 128);
      atr.flush();
    }
    else
    {
      tictactoe.seek(offset, SeekSet);
      tictactoe.write(sector, 128);
      tictactoe.flush();
    }
    Serial.write('C');
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
  Serial.write('C'); // Completed command
  Serial.flush();

  // Write data frame
  Serial.write(sector, 128);

  // Write data frame checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);
#ifdef DEBUG_S
  Serial1.printf("We faked a format.\n");
#endif
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
    case 0xD0:
      sio_udp_begin();
      break;
    case 0xD1:
      sio_udp_status();
      break;
    case 0xD2:
      sio_udp_read();
      break;
    case 0xD3:
      sio_udp_write();
      break;
    case 0xD4:
      sio_udp_connect();
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
  }

  cmdState = WAIT;
  cmdTimer = 0;
}

/**
   Read
*/
void sio_read()
{
  byte ck;
  int sectorNum = (256 * cmdFrame.aux2) + cmdFrame.aux1;
  int cacheOffset = 0;
  int offset;

  if (atr_fd == 0xFF) // config mounted.
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
    tictactoe.seek(offset, SeekSet);
    tictactoe.read(sector, 128);
  }

  ck = sio_checksum((byte *)&sector, 128);

  Serial.write('C'); // Completed command
  Serial.flush();

  // Write data frame
  Serial.write(sector, 128);

  // Write data frame checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);
#ifdef DEBUG
  Debug_print("SIO READ OFFSET: ");
  Debug_print(offset);
  Debug_print(" - ");
  Debug_println((offset + 128));
#endif
}

/**
   UDP read, UDP status must have happened.
*/
void sio_udp_read()
{
  byte ck;
  int packetSize = (256 * cmdFrame.aux2) + cmdFrame.aux1;

  memset(udpPacket, 0, sizeof(udpPacket));
  UDP.read(udpPacket, packetSize);

  ck = sio_checksum((byte *)&udpPacket, packetSize);

  delayMicroseconds(DELAY_T5); // t5 delay
  Serial.write('C'); // Command always completes.
  Serial.flush();
  delayMicroseconds(200);

  // Write data frame
  for (int i = 0; i < packetSize; i++)
    Serial.write(udpPacket[i]);

  // Write checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);

#ifdef DEBUG
  Debug_printf("Sending packet data size %d\n", packetSize);
  for (int i = 0; i < packetSize; i++)
  {
    Debug_printf("%02x ", udpPacket[i]);
  }
  Debug_printf("\n");
#endif

}

/**
   Create UDP listening port (must be called first!)
*/
void sio_udp_begin()
{
  byte ck;
  int packetSize = 2; // 16-bit port number
  byte portL;
  byte portH;

  portL = cmdFrame.aux1;
  portH = cmdFrame.aux2;

  udpPort = (256 * portH) + portL;
  UDP.begin(udpPort);
  delayMicroseconds(DELAY_T5);

#ifdef DEBUG
  Debug_printf("Listening for UDP on port %d", udpPort);
#endif

  Serial.write('C');
}

/**
   Write a UDP packet
*/
void sio_udp_write()
{
  byte ck;
  int packetSize = (256 * cmdFrame.aux2) + cmdFrame.aux1;

  Serial.readBytes(udpPacket, packetSize);
  ck = Serial.read(); // Read checksum

  if (ck == sio_checksum(udpPacket, packetSize))
  {
    Serial.write('A');
    UDP.beginPacket(udpHost, udpPort);
    UDP.write(udpPacket, packetSize);
    UDP.endPacket();
    Serial.write('C');
    yield();
  }
  else
  {
    Serial.write('N');
    return;
  }
#ifdef DEBUG
  Debug_printf("Received %d bytes from computer - ", packetSize);
  for (int i = 0; i < packetSize; i++)
  {
    Debug_printf("%02x ", udpPacket[i]);
  }
  Debug_printf("\n");
#endif
}

/**
   Specify destination host
*/
void sio_udp_connect()
{
  byte ck;
  int packetSize = 64;
  byte tmp[64];

  Serial.readBytes(tmp, packetSize);
  ck = Serial.read(); // Read checksum

  if (ck == sio_checksum(tmp, packetSize))
  {
    Serial.write('A');
    delayMicroseconds(DELAY_T5);
    memcpy(udpHost, tmp, 64);
    Serial.write('C');
    yield();
  }
  else
  {
    Serial.write('N');
    return;
  }
#ifdef DEBUG
  Debug_printf("Receiving %d bytes from computer\n", packetSize);
  Debug_printf("UDP host set to: %s", udpHost);
#endif
}

/**
   Convert remote IP of last packet to string
*/
String convert_remote_ip_to_string()
{
  return String(UDP.remoteIP()[0]) + String(".") +
         String(UDP.remoteIP()[1]) + String(".") +
         String(UDP.remoteIP()[2]) + String(".") +
         String(UDP.remoteIP()[3]);
}

/**
   UDP status
*/
void sio_udp_status()
{
  byte status[4] = {0x00, 0x00, 0x00, 0x00};
  byte ck;
  short l;
  short p;

  if (cmdFrame.aux1 == 0x01)      // Remote IP Address
  {
    status[0] = UDP.remoteIP()[0];
    status[1] = UDP.remoteIP()[1];
    status[2] = UDP.remoteIP()[2];
    status[3] = UDP.remoteIP()[3];
  }
  else if (cmdFrame.aux1 == 0x02) // remote port #
  {
    p = UDP.remotePort();
    status[0] = p & 0xFF;
    status[1] = p >> 8;
  }
  else                       // # of bytes waiting (call first)
  {
    l = UDP.parsePacket();
    strcpy(udpHost,convert_remote_ip_to_string().c_str());
    udpPort = UDP.remotePort();
    status[0] = l & 0xFF;
    status[1] = l >> 8;
  }

  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(DELAY_T5); // t5 delay
  Serial.write('C'); // Command always completes.
  Serial.flush();
  delayMicroseconds(200);
  //delay(1);

  // Write data frame
  for (int i = 0; i < 4; i++)
    Serial.write(status[i]);

  // Write checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);

#ifdef DEBUG
  Debug_printf("UDP packet buffer size: %d\n", l);
  Debug_printf("Sent status packet: %02x %02x %02x %02x\n", status[0], status[1], status[2], status[3]);
#endif
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
  Serial.write('C'); // Command always completes.
  Serial.flush();
  delayMicroseconds(200);
  //delay(1);

  // Write data frame
  for (int i = 0; i < 4; i++)
    Serial.write(status[i]);

  // Write checksum
  Serial.write(ck);
  Serial.flush();
  delayMicroseconds(200);
}

/**
   Send an acknowledgement
*/
void sio_ack()
{
  delayMicroseconds(500);
  Serial.write('A');
  Serial.flush();
  //cmdState = PROCESS;
  sio_process();
}

/**
   Send a non-acknowledgement
*/
void sio_nak()
{
  delayMicroseconds(500);
  Serial.write('N');
  Serial.flush();
  cmdState = WAIT;
  cmdTimer = 0;
}

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
      Serial.read(); // Toss it for now
      cmdTimer = 0;
      break;
  }
}

void setup()
{
  SPIFFS.begin();

  atr = SPIFFS.open("/autorun.atr", "r+");
  tictactoe = SPIFFS.open("/tictactoe.atr", "r+");

  // Set up pins
#ifdef DEBUG_S
  Serial1.begin(19200);
  Debug_println();
  Debug_println("#FujiNet TIC-TAC-TOE UDP Test");
#else
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
#endif
  pinMode(PIN_INT, OUTPUT); // thanks AtariGeezer.
  pinMode(PIN_PROC, OUTPUT);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);

  // Set up serial
  Serial.begin(19200);
  Serial.swap();

  // Attach COMMAND interrupt.
  attachInterrupt(digitalPinToInterrupt(PIN_CMD), sio_isr_cmd, FALLING);
  cmdState = WAIT; // Start in wait state
}

void loop()
{
#ifdef DEBUG_N
  /* Connect to debug server if we aren't and WiFi is connected */
  if ( !wificlient.connected() && WiFi.status() == WL_CONNECTED )
  {
    wificlient.connect(DEBUG_HOST, 6502);
    wificlient.println("#FujiNet TIC-TAC-TOE (UDP) Test");
  }
#endif

  if (Serial.available() > 0)
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
}
