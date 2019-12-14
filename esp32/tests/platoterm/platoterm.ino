/**
   Test #23 - PLATOTERM (ESP32)
*/

#include <WiFi.h>
#include <SPIFFS.h>
#include <WiFiUdp.h>

enum {ID, COMMAND, AUX1, AUX2, CHECKSUM, ACK, NAK, PROCESS, WAIT} cmdState;

// Uncomment for Debug on USB UART
#define DEBUG_S

// Uncomment for Debug on TCP/6502 to DEBUG_HOST
// Run:  `nc -vk -l 6502` on DEBUG_HOST
//#define DEBUG_N
//#define DEBUG_HOST "192.168.1.117"

#define SIO_UART Serial2
#define BUG_UART Serial

#define PIN_LED1         2
#define PIN_LED2         4
#define PIN_INT         26
#define PIN_PROC        22
#define PIN_MTR         33
#define PIN_CMD         21

#define DELAY_T5          1500
#define READ_CMD_TIMEOUT  12
#define CMD_TIMEOUT       50

#define STATUS_SKIP       8

File atr;
File plato;

WiFiClient sioclient;

byte recv_buf[4096];

char packet[256];
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

char atr_fd = 0xFF; // boot internal by default.
int firstCachedSector = 1024;

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
    cmdState = ID;
    cmdTimer = millis();
  }
}

/**
   Get ID
*/
void sio_get_id()
{
  cmdFrame.devic = SIO_UART.read();
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
  ck = SIO_UART.read(); // Read checksum
  SIO_UART.write('A'); // Write ACK

  if (ck == sio_checksum(netConfig.rawData, 96))
  {
    delayMicroseconds(DELAY_T5);
    SIO_UART.write('C');
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
    digitalWrite(PIN_LED1, LOW); // turn on led
  }
  else
  {
    digitalWrite(PIN_LED1, HIGH); // turn off led
  }

  ck = sio_checksum((byte *)&wifiStatus, 1);

  delayMicroseconds(1500); // t5 delay
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
  BUG_UART.printf("receiving 128b data frame from computer.\n");
#endif

  SIO_UART.readBytes(sector, 128);
  ck = SIO_UART.read(); // Read checksum
  //delayMicroseconds(350);
  SIO_UART.write('A'); // Write ACK

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
      plato.seek(offset, SeekSet);
      plato.write(sector, 128);
      plato.flush();
    }
    SIO_UART.write('C');
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
  BUG_UART.printf("We faked a format.\n");
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
    case 'c':
      sio_tcp_connect();
      break;
    case 'd':
      sio_tcp_disconnect();
      break;
    case 'r':
      sio_tcp_read();
      break;
    case 's':
      sio_tcp_status();
      break;
    case 'w':
      sio_tcp_write();
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
    plato.seek(offset, SeekSet);
    plato.read(sector, 128);
  }

  ck = sio_checksum((byte *)&sector, 128);

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
   TCP read
*/
void sio_tcp_read()
{
  byte ck;
  int l=(256*cmdFrame.aux2)+cmdFrame.aux1;
  byte b;

  memset(&packet, 0x00, sizeof(packet));

#ifdef DEBUG
  Debug_printf("Sending RX buffer. %d bytes\n", (256 * cmdFrame.aux2) + cmdFrame.aux1);
#endif

  sioclient.read((byte *)&packet,l);
  ck = sio_checksum((byte *)&packet, l);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Completed command
  SIO_UART.flush();

  // Write data frame
  SIO_UART.write((byte *)&packet, l);

  // Write data frame checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
   TCP status
*/
void sio_tcp_status()
{
  int available;
  byte status[4];
  byte ck;

  available=sioclient.available();
  
  status[0] = available & 0xFF;
  status[1] = available >> 8;
  status[2] = WiFi.status();
  status[3] = 0x00;

  ck = sio_checksum((byte *)&status, 4);

  delayMicroseconds(DELAY_T5); // t5 delay
  SIO_UART.write('C'); // Command always completes.
  SIO_UART.flush();
  delayMicroseconds(200);

  // Write data frame
  for (int i = 0; i < 4; i++)
    SIO_UART.write(status[i]);

  // Write checksum
  SIO_UART.write(ck);
  SIO_UART.flush();
  delayMicroseconds(200);
}

/**
   SIO Connect TCP
*/
void sio_tcp_connect(void)
{
  byte ck;
  char* thn;
  char* tpn; // hostname and port # tokens
  int port;

  memset(&packet, 0x00, sizeof(packet));

#ifdef DEBUG
  Debug_println("Receiving 256b frame from computer");
#endif

  SIO_UART.readBytes(packet, 256);
  while (SIO_UART.available()==0) { delayMicroseconds(100); }
  ck = SIO_UART.read(); // Read checksum

  if (ck != sio_checksum((byte *)&packet, 256))
  {
#ifdef DEBUG
    Debug_println("Bad Checksum");
#endif
    SIO_UART.write('N'); // NAK
    return;
  }
#ifdef DEBUG
  Debug_println("ACK");
#endif
  SIO_UART.write('A');   // ACK

  // Tokenize the connection string.
  thn = strtok(packet, ":");
  tpn = strtok(NULL, ":");
  port = atoi(tpn);

#ifdef DEBUG
  Debug_printf("thn: %s tpn: %s port %d", thn, tpn, port);
#endif

  if (sioclient.connect(thn, port) == true)
  {
#ifdef DEBUG
    Debug_print("COMPLETE");
#endif
    SIO_UART.write('C');
  }
  else
  {
#ifdef DEBUG
    Debug_print("ERROR");
#endif
    SIO_UART.write('E');
  }
}

/**
   SIO Disconnect TCP
*/
void sio_tcp_disconnect(void)
{
  byte ck;

  memset(&packet, 0x00, sizeof(packet));

#ifdef DEBUG
  Debug_println("Receiving 1b frame from computer");
#endif

  SIO_UART.readBytes(packet, 1);
  ck = SIO_UART.read(); // Read checksum

  if (ck != sio_checksum((byte *)&packet, 1))
  {
    SIO_UART.write('N'); // NAK
    return;
  }

  SIO_UART.write('A');   // ACK
  sioclient.stop();

  SIO_UART.write('C');
}

/**
   SIO Write TCP string
*/
void sio_tcp_write(void)
{
  byte ck;
  int l=cmdFrame.aux1;
  
  memset(&packet, 0x00, sizeof(packet));

#ifdef DEBUG
  Debug_printf("Receiving %d byte frame from computer\n",l);
#endif

  SIO_UART.readBytes(packet, l);
  while (SIO_UART.available()==0) { delayMicroseconds(100); }
  ck = SIO_UART.read(); // Read checksum

#ifdef DEBUG
  for (int i=0;i<l;i++)
  {
    Debug_printf("%02x ",packet[i]);  
  }
  Debug_printf("\n\n");
#endif 

  delayMicroseconds(DELAY_T5);

  if (ck != sio_checksum((byte *)&packet, l))
  {
#ifdef DEBUG
    Debug_println("Bad Checksum");
#endif
    SIO_UART.write('N'); // NAK
    return;
  }
#ifdef DEBUG
  Debug_println("ACK");
#endif
  SIO_UART.write('A');   // ACK

  if (sioclient.write(packet, 1) == 1)
  {
#ifdef DEBUG
    Debug_print("COMPLETE\n");
#endif
    SIO_UART.write('C');
  }
  else
  {
#ifdef DEBUG
    Debug_print("ERROR\n");
#endif
    SIO_UART.write('E');
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
  }
}

void setup()
{
  SPIFFS.begin();
  atr = SPIFFS.open("/autorun.atr", "r+");
  plato = SPIFFS.open("/plato.atr", "r+");

  // Set up pins
#ifdef DEBUG_S
  BUG_UART.begin(19200);
  BUG_UART.println();
  BUG_UART.println("#FujiNet PLATOTERM Test");
#endif
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  pinMode(PIN_INT, OUTPUT); // thanks AtariGeezer.
  pinMode(PIN_PROC, OUTPUT);
  pinMode(PIN_MTR, INPUT);
  pinMode(PIN_CMD, INPUT);

  digitalWrite(PIN_LED1, HIGH); // off
  digitalWrite(PIN_LED2, HIGH); // off
  digitalWrite(PIN_PROC,HIGH);
  digitalWrite(PIN_INT, HIGH);

  // Set up serial
  SIO_UART.begin(19200);

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
    wificlient.println("#FujiNet PLATOTERM Test");
  }
#endif

  if (sioclient.connected() && (sioclient.available()>0) && (digitalRead(PIN_PROC)==HIGH))
  {
    digitalWrite(PIN_PROC,LOW);
  }
  else if (sioclient.connected() && (sioclient.available()==0) && (digitalRead(PIN_PROC)==LOW))
  {
    digitalWrite(PIN_PROC,HIGH);  
  }
  
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
}
